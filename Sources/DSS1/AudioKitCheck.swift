#if TESTHOOKS
import Accelerate
import AudioKit
import AVFoundation
import SoundpipeAudioKit

/// Debug aid (CRATE_AK_OFFLINE=1): renders test tones through the AudioKit effects offline
/// and reports what each one did, so the effects can be checked without listening.
enum AudioKitCheck {
    static func run() {
        let placeholder = Mixer()
        let rate = 48000.0

        /// Renders `seconds` of `signal` through `unit` and returns the left channel.
        func render(_ unit: AVAudioNode, seconds: Double, signal: @escaping (Int) -> Float) -> [Float] {
            let engine = AVAudioEngine()
            let format = AVAudioFormat(standardFormatWithSampleRate: rate, channels: 2)!
            let total = Int(seconds * rate)
            let source = AVAudioSourceNode(format: format) { _, _, frames, list in
                let buffers = UnsafeMutableAudioBufferListPointer(list)
                for f in 0..<Int(frames) {
                    let v = signal(Counter.shared.next())
                    for b in buffers { b.mData!.assumingMemoryBound(to: Float.self)[f] = v }
                }
                return noErr
            }
            Counter.shared.reset()
            engine.attach(source)
            engine.attach(unit)
            engine.connect(source, to: unit, format: format)
            engine.connect(unit, to: engine.mainMixerNode, format: format)
            try! engine.enableManualRenderingMode(.offline, format: format, maximumFrameCount: 4096)
            try! engine.start()
            let out = AVAudioPCMBuffer(pcmFormat: engine.manualRenderingFormat, frameCapacity: 4096)!
            var result: [Float] = []
            while result.count < total {
                let n = AVAudioFrameCount(min(4096, total - result.count))
                _ = try? engine.renderOffline(n, to: out)
                result += Array(UnsafeBufferPointer(start: out.floatChannelData![0], count: Int(out.frameLength)))
            }
            engine.stop()
            engine.detach(unit)
            return result
        }

        func db(_ x: Double) -> Double { 20 * log10(max(x, 1e-9)) }
        func rms(_ a: ArraySlice<Float>) -> Double { Double(vDSP.rootMeanSquare(Array(a))) }
        let sine: (Int) -> Float = { Float(0.5 * sin(2 * .pi * 1000 * Double($0) / rate)) }
        let burst: (Int) -> Float = { $0 < Int(0.1 * rate) ? Float(0.5 * sin(2 * .pi * 440 * Double($0) / rate)) : 0 }

        /// How much of the output isn't the 1 kHz tone (distortion + noise), in dB below the tone.
        func purity(_ x: [Float]) -> Double {
            let n = 8192
            let s = Array(x[(x.count - n)...])
            let fft = vDSP.FFT(log2n: 13, radix: .radix2, ofType: DSPSplitComplex.self)!
            let w = vDSP.window(ofType: Float.self, usingSequence: .blackman, count: n, isHalfWindow: false)
            let win = vDSP.multiply(s, w)
            var re = [Float](repeating: 0, count: n / 2), im = re, mags = re
            re.withUnsafeMutableBufferPointer { rp in im.withUnsafeMutableBufferPointer { ip in
                var split = DSPSplitComplex(realp: rp.baseAddress!, imagp: ip.baseAddress!)
                win.withUnsafeBufferPointer { wp in wp.baseAddress!.withMemoryRebound(to: DSPComplex.self, capacity: n / 2) {
                    vDSP_ctoz($0, 2, &split, 1, vDSP_Length(n / 2)) } }
                fft.forward(input: split, output: &split)
                mags.withUnsafeMutableBufferPointer { vDSP_zvmags(&split, 1, $0.baseAddress!, 1, vDSP_Length(n / 2)) }
            } }
            let k = Int(1000.0 / rate * Double(n))
            let tone = mags[(k - 6)...(k + 6)].reduce(0, +)
            let rest = mags.reduce(0, +) - tone
            return 10 * log10(Double(rest) / Double(tone))
        }

        let dry = render(AVAudioMixerNode(), seconds: 1, signal: sine)
        NSLog("CRATETEST offline dry 1 kHz: residue %.1f dB", purity(dry))

        let crusher = BitCrusher(placeholder, bitDepth: 4, sampleRate: 2000, dryWetMix: 1)
        NSLog("CRATETEST offline LO-FI full (4-bit, 2 kHz): residue %.1f dB", purity(render(crusher.avAudioNode, seconds: 1, signal: sine)))
        let crushMid = BitCrusher(placeholder, bitDepth: 10, sampleRate: 6324, dryWetMix: 0.67)
        NSLog("CRATETEST offline LO-FI half: residue %.1f dB", purity(render(crushMid.avAudioNode, seconds: 1, signal: sine)))

        let moog = MoogLadder(placeholder, cutoffFrequency: 300, resonance: 0.8)
        let lp = render(moog.avAudioNode, seconds: 1, signal: sine)
        NSLog("CRATETEST offline LPF @300 Hz on 1 kHz tone: %.1f dB", db(rms(lp[24000...])) - db(rms(dry[24000...])))

        let dryBurst = render(AVAudioMixerNode(), seconds: 2, signal: burst)
        let tail = 24000..<96000
        let tape = VariableDelay(placeholder, time: 0.35, feedback: 0.8, maximumTime: 4, dryWetMix: 0.6)
        let t = render(tape.avAudioNode, seconds: 2, signal: burst)
        NSLog("CRATETEST offline TAPE ECHO tail after a 0.1 s burst: %.1f dBFS (dry %.1f)", db(rms(t[tail])), db(rms(dryBurst[tail])))
        let rev = CostelloReverb(placeholder, balance: 0.7, feedback: 0.92, cutoffFrequency: 9000)
        let r = render(rev.avAudioNode, seconds: 2, signal: burst)
        NSLog("CRATETEST offline REVERB tail after a 0.1 s burst: %.1f dBFS (dry %.1f)", db(rms(r[tail])), db(rms(dryBurst[tail])))
        let hp = HighPassButterworthFilter(placeholder, cutoffFrequency: 3000)
        NSLog("CRATETEST offline HPF @3 kHz on 1 kHz tone: %.1f dB",
              db(rms(render(hp.avAudioNode, seconds: 1, signal: sine)[24000...])) - db(rms(dry[24000...])))
        // Key-lock: a 1 kHz tone played at `rate` comes out at 1000 × rate Hz; key-lock should bring it back to 1 kHz.
        func keyLockTest(rate: Double, on: Bool) -> (hz: Double, level: Double) {
            let lock = KeyLock(sampleRate: rate == 0 ? 48000 : 48000, channels: 2)
            lock.update(enabled: on, rate: rate)
            let fmt = AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 2)!
            let buf = AVAudioPCMBuffer(pcmFormat: fmt, frameCapacity: 512)!
            buf.frameLength = 512
            var out: [Float] = []
            var phase = 0.0
            for _ in 0..<(48000 * 2 / 512) {
                for i in 0..<512 {
                    let v = Float(0.5 * sin(phase))
                    phase += 2 * .pi * 1000 * rate / 48000
                    buf.floatChannelData![0][i] = v
                    buf.floatChannelData![1][i] = v
                }
                lock.process(UnsafeMutableAudioBufferListPointer(buf.mutableAudioBufferList), frames: 512)
                out += Array(UnsafeBufferPointer(start: buf.floatChannelData![0], count: 512))
            }
            let tail = Array(out[48000...])   // the second second, well past the start-up delay
            var crossings = 0
            for i in 1..<tail.count where (tail[i - 1] < 0) != (tail[i] < 0) { crossings += 1 }
            return (Double(crossings) / 2, db(rms(tail[...])) - db(0.5 / 2.0.squareRoot()))
        }
        for (rate, on) in [(1.08, false), (1.08, true), (0.92, true), (1.16, true), (1.5, true), (-1.0, true)] {
            let r = keyLockTest(rate: rate, on: on)
            NSLog("CRATETEST key lock %@ at speed %.2f: tone %.0f Hz (level %+.1f dB)", on ? "ON " : "OFF", rate, r.hz, r.level)
        }
        let probe = KeyLock(sampleRate: 48000, channels: 2)
        probe.update(enabled: true, rate: 1.08)
        NSLog("CRATETEST key lock block %d frames, latency %.0f ms", probe.blockSize, Double(probe.latency) / 48)
        NSLog("CRATETEST offline done")
    }
}

/// Sample counter for the test signal (render callbacks run on another thread).
final class Counter: @unchecked Sendable {
    static let shared = Counter()
    private var i = 0
    func reset() { i = 0 }
    func next() -> Int { defer { i += 1 }; return i }
}
#endif
