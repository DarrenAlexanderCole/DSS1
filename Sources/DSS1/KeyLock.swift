import AVFoundation
import CRubberBand
import os

/// Key-lock for the chop station: the tape head changes speed (pitch fader, direction lever),
/// and Rubber Band's live shifter shifts the pitch back by the same ratio, so the tempo moves
/// while the key stays put. Runs on the audio thread, after the tape head.
///
/// Rubber Band works in fixed blocks, so audio is queued in and out of ring buffers; that adds
/// about one block plus the shifter's own delay of latency while key-lock is on.
final class KeyLock: @unchecked Sendable {
    private struct Settings {
        var enabled = false
        var scale = 1.0
    }

    private let settings = OSAllocatedUnfairLock(initialState: Settings())
    private let shifter: OpaquePointer
    private let channels: Int
    let blockSize: Int
    private var appliedScale = 1.0
    private var wasEnabled = false

    // Ring buffers per channel (audio thread only).
    private let capacity = 1 << 15
    private var inRing: [[Float]]
    private var outRing: [[Float]]
    private var inCount = 0, inRead = 0, inWrite = 0
    private var outCount = 0, outRead = 0, outWrite = 0
    private var blockIn: [UnsafeMutablePointer<Float>]
    private var blockOut: [UnsafeMutablePointer<Float>]

    init(sampleRate: Double, channels: Int) {
        self.channels = max(1, channels)
        shifter = rb_live_new(Int32(sampleRate), Int32(self.channels))
        blockSize = Int(rb_live_block_size(shifter))
        inRing = Array(repeating: [Float](repeating: 0, count: 1 << 15), count: self.channels)
        outRing = inRing
        blockIn = (0..<self.channels).map { _ in .allocate(capacity: 4096) }
        blockOut = (0..<self.channels).map { _ in .allocate(capacity: 4096) }
    }

    deinit {
        rb_live_free(shifter)
        blockIn.forEach { $0.deallocate() }
        blockOut.forEach { $0.deallocate() }
    }

    /// Called from the main thread whenever the tape's speed or the switch changes.
    /// `rate` is the tape's signed speed; the shifter undoes |rate|.
    func update(enabled: Bool, rate: Double) {
        let speed = max(0.25, min(4, abs(rate)))
        settings.withLock {
            $0.enabled = enabled
            $0.scale = 1 / speed
        }
    }

    /// Latency added while key-lock is on, in frames.
    var latency: Int { blockSize + Int(rb_live_start_delay(shifter)) }

    /// Processes `frames` frames in place (non-interleaved float buffers).
    func process(_ list: UnsafeMutableAudioBufferListPointer, frames: Int) {
        let s = settings.withLock { $0 }
        if !s.enabled {
            if wasEnabled { resetQueues() }
            wasEnabled = false
            return
        }
        if !wasEnabled {
            resetQueues()
            rb_live_reset(shifter)
            // Prime the output with one block of silence so there's always something to hand back.
            for c in 0..<channels { for i in 0..<blockSize { outRing[c][i] = 0 } }
            outCount = blockSize
            outWrite = blockSize
            wasEnabled = true
        }
        if s.scale != appliedScale {
            rb_live_set_pitch_scale(shifter, s.scale)
            appliedScale = s.scale
        }

        let buffers = list.map { $0.mData?.assumingMemoryBound(to: Float.self) }
        let mask = capacity - 1

        // Queue the input.
        for i in 0..<frames {
            for c in 0..<channels {
                let src = buffers[min(c, buffers.count - 1)]
                inRing[c][(inWrite + i) & mask] = src?[i] ?? 0
            }
        }
        inWrite = (inWrite + frames) & mask
        inCount += frames

        // Shift whole blocks.
        while inCount >= blockSize {
            for c in 0..<channels {
                for i in 0..<blockSize { blockIn[c][i] = inRing[c][(inRead + i) & mask] }
            }
            inRead = (inRead + blockSize) & mask
            inCount -= blockSize
            blockIn.withUnsafeBufferPointer { ins in
                blockOut.withUnsafeBufferPointer { outs in
                    ins.baseAddress!.withMemoryRebound(to: UnsafePointer<Float>?.self, capacity: channels) { i in
                        outs.baseAddress!.withMemoryRebound(to: UnsafeMutablePointer<Float>?.self, capacity: channels) { o in
                            rb_live_shift(shifter, i, o)
                        }
                    }
                }
            }
            for c in 0..<channels {
                for i in 0..<blockSize { outRing[c][(outWrite + i) & mask] = blockOut[c][i] }
            }
            outWrite = (outWrite + blockSize) & mask
            outCount += blockSize
        }

        // Hand back what's ready (silence if the queue ever runs dry).
        for i in 0..<frames {
            let have = i < outCount
            for (c, dst) in buffers.enumerated() {
                guard let dst else { continue }
                dst[i] = have ? outRing[min(c, channels - 1)][(outRead + i) & mask] : 0
            }
        }
        let used = min(frames, outCount)
        outRead = (outRead + used) & mask
        outCount -= used
    }

    private func resetQueues() {
        inCount = 0; inRead = 0; inWrite = 0
        outCount = 0; outRead = 0; outWrite = 0
    }
}
