import AudioKit
import AVFoundation
import SoundpipeAudioKit

import os

/// The effects on the chop station's pedal.
enum FXKind: String, CaseIterable, Identifiable, Codable {
    case delay, echo, lofi, lpf, hpf, reverb, spaceEcho, stutter

    var id: String { rawValue }

    var label: String {
        switch self {
        case .delay: return "DELAY"
        case .echo: return "ECHO"
        case .lofi: return "LO-FI"
        case .lpf: return "LPF"
        case .hpf: return "HPF"
        case .reverb: return "REVERB"
        case .spaceEcho: return "SPACE ECHO"
        case .stutter: return "STUTTER"
        }
    }

    var detail: String {
        switch self {
        case .delay: return "1/8 SYNC"
        case .echo: return "1/4 SYNC"
        case .lofi: return "CRUSH"
        case .lpf: return "LOW PASS"
        case .hpf: return "HIGH PASS"
        case .reverb: return "HALL"
        case .spaceEcho: return "TAPE 3/16"
        case .stutter: return "REPEAT"
        }
    }
}

struct FXState: Equatable {
    var on = false
    var depth = 0.5
}

extension FXState {
    /// Stutter size from the knob: 1/4, 1/8, 1/16 or 1/32 of a beat.
    static func stutterDivision(_ depth: Double) -> Int {
        [4, 8, 16, 32][min(3, Int(depth * 4))]
    }
}

/// A tape head running over the whole track in memory: reads at any speed, forwards or backwards,
/// wraps inside a loop, and can stutter (repeat a tiny slice while the tape keeps moving underneath).
/// Speed and level glide rather than jump, so the direction lever never clicks.
final class TapeHead: @unchecked Sendable {
    private struct State {
        var position = 0.0          // where the tape is
        var targetRate = 1.0        // signed speed: negative is reverse
        var rate = 1.0
        var active = false          // motor on
        var gain = 0.0
        var loop: Range<Int>?
        var stutterLength = 0       // > 0 while stuttering
        var stutterStart = 0.0
        var stutterPos = 0.0
        var hitEdge = false         // ran off either end of the track
    }

    private let lock = OSAllocatedUnfairLock(initialState: State())
    private let channels: [UnsafeMutablePointer<Float>]
    let frames: Int
    private let source: AVAudioPCMBuffer   // kept alive for `channels`

    init(buffer: AVAudioPCMBuffer) {
        source = buffer
        frames = Int(buffer.frameLength)
        channels = (0..<Int(buffer.format.channelCount)).map { buffer.floatChannelData![$0] }
    }

    var position: Int { lock.withLock { Int($0.position) } }
    var hitEdge: Bool { lock.withLock { $0.hitEdge } }

    func start(at frame: Int, loop: Range<Int>?) {
        lock.withLock {
            $0.position = Double(frame)
            $0.loop = loop
            $0.active = true
            $0.hitEdge = false
            $0.stutterLength = 0
        }
    }

    func stop() { lock.withLock { $0.active = false } }

    func setRate(_ rate: Double) { lock.withLock { $0.targetRate = rate } }

    func setStutter(length: Int) {
        lock.withLock {
            if length > 0, $0.stutterLength == 0 {
                $0.stutterStart = $0.position
                $0.stutterPos = $0.position
            }
            $0.stutterLength = max(0, length)
        }
    }

    /// Fills the audio buffer. Runs on the audio thread.
    func render(frameCount: Int, into list: UnsafeMutableAudioBufferListPointer) {
        lock.withLock { st in
            let outs = list.map { $0.mData?.assumingMemoryBound(to: Float.self) }
            let n = channels.count
            for i in 0..<frameCount {
                st.rate += (st.targetRate - st.rate) * 0.0015
                st.gain += ((st.active ? 1 : 0) - st.gain) * 0.004
                if st.gain < 0.0005 && !st.active {
                    for (c, out) in outs.enumerated() where out != nil { out![i] = 0; _ = c }
                    continue
                }

                // What we hear: the stutter slice if one is held, otherwise the tape itself.
                let readAt = st.stutterLength > 0 ? st.stutterPos : st.position
                let base = Int(readAt.rounded(.down))
                let frac = Float(readAt - Double(base))
                for (c, out) in outs.enumerated() {
                    guard let out else { continue }
                    let ch = channels[min(c, n - 1)]
                    var v: Float = 0
                    if base >= 0 && base + 1 < frames {
                        v = ch[base] + (ch[base + 1] - ch[base]) * frac
                    }
                    out[i] = v * Float(st.gain)
                }

                // Move the tape (it keeps moving under a stutter).
                st.position += st.rate
                if let loop = st.loop, loop.count > 1 {
                    let lo = Double(loop.lowerBound), len = Double(loop.count)
                    if st.position >= lo + len { st.position -= len }
                    if st.position < lo { st.position += len }
                } else if st.position >= Double(frames - 1) || st.position < 0 {
                    st.position = min(max(st.position, 0), Double(frames - 1))
                    st.active = false
                    st.hitEdge = true
                }
                if st.stutterLength > 0 {
                    st.stutterPos += st.rate
                    let len = Double(st.stutterLength)
                    if st.stutterPos >= st.stutterStart + len { st.stutterPos -= len }
                    if st.stutterPos < st.stutterStart { st.stutterPos += len }
                }
            }
        }
    }
}

/// The chop station's audio: a tape head over the track, run through the effects pedal.
///
///     tape head (speed, direction, loop, stutter) → HPF → Moog LPF → bit crusher → delay → echo
///         → tape echo (with wow and flutter) → Costello reverb → master EQ → out
///
/// The filters, lo-fi, tape echo and reverb are AudioKit's (Soundpipe DSP); delay and echo are Apple's.
@MainActor
final class Sampler {
    private let engine = AVAudioEngine()
    // AudioKit effects are built around a placeholder input; we wire their audio units into our own engine.
    private static let placeholder = Mixer()
    private let hpf = HighPassButterworthFilter(Sampler.placeholder, cutoffFrequency: 20)
    private let lpf = MoogLadder(Sampler.placeholder, cutoffFrequency: 20000, resonance: 0.4)
    private let crusher = BitCrusher(Sampler.placeholder, bitDepth: 16, sampleRate: 20000, dryWetMix: 1)
    private let delay = AVAudioUnitDelay()
    private let echo = AVAudioUnitDelay()
    private let tape = VariableDelay(Sampler.placeholder, time: 0.5, feedback: 0.6, maximumTime: 4, dryWetMix: 0.3)
    private let reverb = CostelloReverb(Sampler.placeholder, balance: 0.4, feedback: 0.75, cutoffFrequency: 9000)
    /// Takes any track (mono or stereo) and hands stereo to the effects.
    private let inlet = AVAudioMixerNode()
    /// The tape echo's set time; wow and flutter wobble around it.
    private var tapeTime: AUValue = 0.5
    private var wobble: Task<Void, Never>?
    /// Auto make-up gain after the effects, so switching effects on changes the colour, not the volume.
    private let makeup = AVAudioMixerNode()
    private let dryLevel = LevelFollower()
    private let wetLevel = LevelFollower()
    private var anyFXOn = false
    private var levelMatcher: Task<Void, Never>?
    private let masterEQ = AVAudioUnitEQ(numberOfBands: EQBands.frequencies.count)
    private let volumeMixer = AVAudioMixerNode()
    private var head: TapeHead?
    private var sourceNode: AVAudioSourceNode?

    private var pitch = 0.0
    private var direction = 1.0
    private(set) var isPlaying = false
    /// What's coming out (after the effects), for the tape deck's VU meters.
    let meter = LevelMeter()

    var volume: Float = 1 {
        didSet { volumeMixer.outputVolume = volume }
    }

    nonisolated private static func meterTap(_ meter: LevelMeter) -> AVAudioNodeTapBlock {
        { buffer, _ in
            meter.process(buffer)
            LiveRecorder.shared.ingest(buffer, from: .chop)
        }
    }

    init() {
        for node in [inlet, hpf.avAudioNode, lpf.avAudioNode, crusher.avAudioNode, delay, echo,
                     tape.avAudioNode, reverb.avAudioNode, makeup, masterEQ] {
            engine.attach(node)
        }
        MasterBus.install(in: engine, volume: volumeMixer, tap: Self.meterTap(meter))
        MasterEQ.shared.register(masterEQ)
        for unit in [delay, echo] { unit.bypass = true }
        for fx in [hpf, lpf, crusher, tape, reverb] as [Node] { fx.bypass() }
    }

    nonisolated private static func renderBlock(_ head: TapeHead, _ keyLock: KeyLock) -> AVAudioSourceNodeRenderBlock {
        { _, _, frameCount, list in
            let buffers = UnsafeMutableAudioBufferListPointer(list)
            head.render(frameCount: Int(frameCount), into: buffers)
            keyLock.process(buffers, frames: Int(frameCount))
            return noErr
        }
    }

    /// Key-lock (Rubber Band): tempo follows the pitch fader and lever, the key doesn't.
    private var keyLock: KeyLock?
    private var keyLockOn = false

    func setKeyLock(_ on: Bool) {
        keyLockOn = on
        applyRate()
    }

    func load(_ url: URL, buffer: AVAudioPCMBuffer) throws {
        let head = TapeHead(buffer: buffer)
        self.head = head
        // Rewiring a running engine can hang it; stop it for the moment it takes.
        if engine.isRunning { engine.stop() }
        if let old = sourceNode { engine.detach(old) }
        let format = buffer.format
        let lock = KeyLock(sampleRate: format.sampleRate, channels: Int(format.channelCount))
        keyLock = lock
        let node = AVAudioSourceNode(format: format, renderBlock: Self.renderBlock(head, lock))
        engine.attach(node)
        sourceNode = node
        let stereo = AVAudioFormat(standardFormatWithSampleRate: format.sampleRate, channels: 2)
        let chain: [AVAudioNode] = [inlet, hpf.avAudioNode, lpf.avAudioNode, crusher.avAudioNode, delay, echo,
                                    tape.avAudioNode, reverb.avAudioNode, makeup, masterEQ, engine.mainMixerNode]
        for n in chain.dropLast() { engine.disconnectNodeOutput(n) }
        engine.connect(node, to: inlet, format: format)
        for (a, b) in zip(chain, chain.dropFirst()) { engine.connect(a, to: b, format: stereo) }
        inlet.removeTap(onBus: 0)
        reverb.avAudioNode.removeTap(onBus: 0)
        inlet.installTap(onBus: 0, bufferSize: 1024, format: nil, block: dryLevel.tap)
        reverb.avAudioNode.installTap(onBus: 0, bufferSize: 1024, format: nil, block: wetLevel.tap)
        startLevelMatching()
        applyRate()
        if !engine.isRunning { try engine.start() }
    }

    /// Starts playing at `frame` and carries on to the end of the track (or the start, in reverse).
    func play(from frame: Int) {
        guard let head else { return }
        if !engine.isRunning { try? engine.start() }
        head.start(at: min(max(frame, 0), head.frames - 1), loop: nil)
        isPlaying = true
        LiveRecorder.shared.setSource(.chop)
    }

    /// Repeats `range` seamlessly until something else is played.
    func playLoop(_ range: Range<Int>) {
        guard let head else { return }
        if !engine.isRunning { try? engine.start() }
        head.start(at: range.lowerBound, loop: range)
        isPlaying = true
        LiveRecorder.shared.setSource(.chop)
    }

    /// Stops (with a short fade) and returns where it got to.
    @discardableResult
    func pause() -> Int? {
        guard let head else { return nil }
        head.stop()
        isPlaying = false
        return head.position
    }

    /// The frame under the head while playing.
    var position: Int? {
        guard isPlaying, let head else { return nil }
        return head.position
    }

    /// True once playback has run off the end (or, in reverse, the start) of the track.
    var reachedEdge: Bool { head?.hitEdge ?? false }

    /// Turntable-style pitch: changes speed and pitch together.
    func setPitch(_ percent: Double) {
        pitch = percent
        applyRate()
    }

    /// The direction lever: 1 is normal forward, 0 is stopped, −1 is full-speed reverse.
    func setDirection(_ value: Double) {
        direction = value
        applyRate()
    }

    private func applyRate() {
        let rate = direction * (1 + pitch / 100)
        head?.setRate(rate)
        keyLock?.update(enabled: keyLockOn, rate: rate)
    }

    /// Applies the pedal. Delay times and stutter size follow the track's tempo.
    ///
    /// With `xy` (the performance pad, 0…1 each way, y up) the switched-on effects follow the pad
    /// instead of their knobs: Y is the amount (up to the knob's depth) and X is each effect's character.
    func apply(_ fx: [FXKind: FXState], bpm: Double?, sampleRate: Double, xy: CGPoint? = nil, wet: Double = 1) {
        let beat = 60 / (bpm ?? 100)
        func state(_ k: FXKind) -> FXState {
            var s = fx[k] ?? FXState()
            s.depth *= wet
            return s
        }
        let x = xy.map { min(max(Double($0.x), 0), 1) }
        let y = xy.map { min(max(Double($0.y), 0), 1) }
        func amount(_ s: FXState) -> Double { y.map { $0 * s.depth } ?? s.depth }
        /// X in four steps, for tempo-synced choices.
        func step<T>(_ options: [T]) -> T? { x.map { options[min(options.count - 1, Int($0 * Double(options.count)))] } }

        // Butterworth high-pass: 20 Hz → 3 kHz. Pad: X sweeps it, Y reaches higher (up to 6 kHz).
        let h = state(.hpf)
        h.on ? hpf.start() : hpf.bypass()
        if let x, let y {
            hpf.cutoffFrequency = AUValue(20 * pow(pow(300, 0.5 + 0.5 * y * h.depth), x * wet))
        } else {
            hpf.cutoffFrequency = AUValue(20 * pow(150, h.depth))
        }

        // Moog ladder low-pass: 20 kHz → 300 Hz, with resonance rising as it closes.
        // Pad: X opens it left to right, Y is the resonance.
        let l = state(.lpf)
        l.on ? lpf.start() : lpf.bypass()
        if let x {
            lpf.cutoffFrequency = AUValue(20000 * pow(250.0 / 20000, (1 - x) * wet))
            lpf.resonance = AUValue(0.1 + 0.8 * amount(l))
        } else {
            lpf.cutoffFrequency = AUValue(20000 * pow(0.015, l.depth))
            lpf.resonance = AUValue(0.25 + 0.55 * l.depth)
        }

        // Bit crusher: 16-bit / 20 kHz down to 4-bit / 2 kHz. Pad: X crushes the sample rate, Y the bits.
        let c = state(.lofi)
        c.on ? crusher.start() : crusher.bypass()
        let crush = amount(c)
        crusher.bitDepth = AUValue(16 - 12 * crush)
        crusher.sampleRate = AUValue(20000 * pow(0.1, x ?? c.depth))
        crusher.dryWetMix = AUValue(0.35 + 0.65 * (y == nil ? c.depth : max(crush, x ?? 0)))

        // Delay: an eighth by default. Pad: X picks 1/16, 1/8, 3/16 or 1/4; Y the feedback and mix.
        let d = state(.delay)
        delay.bypass = !d.on
        let da = amount(d)
        delay.delayTime = min(2, beat * (step([0.25, 0.5, 0.75, 1.0]) ?? 0.5))
        delay.feedback = Float(20 + da * (y == nil ? 45 : 60))
        delay.lowPassCutoff = 15000
        delay.wetDryMix = Float(da * (y == nil ? 50 : 60))

        // Echo: a quarter by default. Pad: X picks 1/8, 1/4, 3/8 or 1/2 bar-ish lengths; Y the feedback and mix.
        let e = state(.echo)
        echo.bypass = !e.on
        let ea = amount(e)
        echo.delayTime = min(2, beat * (step([0.5, 1.0, 1.5, 2.0]) ?? 1.0))
        echo.feedback = Float(min(88, 35 + ea * (y == nil ? 40 : 52)))
        echo.lowPassCutoff = 7000
        echo.wetDryMix = Float(ea * (y == nil ? 50 : 55))

        // Tape echo: a dotted-eighth delay whose time wanders slightly (wow and flutter).
        // Pad: X slides the tape speed (the repeats warp in pitch as you move), Y drives it toward runaway.
        let t = state(.spaceEcho)
        t.on ? tape.start() : tape.bypass()
        let ta = amount(t)
        tapeTime = AUValue(min(3.5, beat * (x.map { 0.25 + 0.75 * $0 } ?? 0.75)))
        tape.time = tapeTime
        tape.feedback = AUValue(y == nil ? min(0.88, 0.5 + ta * 0.38) : min(0.93, 0.4 + ta * 0.53))
        tape.dryWetMix = AUValue(y == nil ? ta * 0.6 : 0.15 + ta * 0.5)
        t.on ? startWobble() : stopWobble()

        // Costello reverb: a dense, lush hall; depth sets the size and the mix. Pad: X the size, Y the mix.
        let r = state(.reverb)
        r.on ? reverb.start() : reverb.bypass()
        reverb.feedback = AUValue(0.62 + 0.3 * (x ?? r.depth))
        reverb.cutoffFrequency = 9000
        reverb.balance = AUValue(amount(r) * (y == nil ? 0.7 : 0.85))

        // Stutter. Pad: X picks the repeat size (1/4 → 1/32 beat).
        anyFXOn = fx.values.contains { $0.on }

        let stutter = state(.stutter)
        let division = x.map { FXState.stutterDivision($0) } ?? FXState.stutterDivision(stutter.depth)
        let slice = stutter.on ? Int(beat / Double(division) * sampleRate) : 0
        head?.setStutter(length: slice)
    }

    /// Wow (slow pitch drift) and flutter (fast shimmer), by nudging the tape echo's delay time.
    private func startWobble() {
        guard wobble == nil else { return }
        let started = Date()
        wobble = Task { [weak self] in
            while !Task.isCancelled {
                guard let self else { return }
                let t = Date().timeIntervalSince(started)
                let wow: Double = 0.006 * sin(2 * Double.pi * 0.55 * t)
                let flutter: Double = 0.0015 * sin(2 * Double.pi * 6.3 * t)
                self.tape.time = self.tapeTime * AUValue(1 + wow + flutter)
                try? await Task.sleep(for: .milliseconds(30))
            }
        }
    }

    private func stopWobble() {
        wobble?.cancel()
        wobble = nil
    }

    /// Glides the make-up gain toward dry level ÷ effected level while any effect is on:
    /// never below unity, at most +12 dB, and back to unity when everything is off.
    private func startLevelMatching() {
        guard levelMatcher == nil else { return }
        levelMatcher = Task { [weak self] in
            var gain: Float = 1
            while !Task.isCancelled {
                try? await Task.sleep(for: .milliseconds(30))
                guard let self else { return }
                var target: Float = 1
                let dry = self.dryLevel.rms, wet = self.wetLevel.rms
                if self.anyFXOn, dry > 0.002, wet > 0.0001 {
                    target = min(max(dry / wet, 1), 3.98)
                } else if self.anyFXOn {
                    target = gain   // silence: hold where we are
                }
                gain += (target - gain) * 0.12
                self.makeup.outputVolume = gain
            }
        }
    }

    /// The make-up gain right now, in dB (for tests and display).
    var makeupDB: Float { 20 * log10(max(makeup.outputVolume, 0.0001)) }

    func shutDown() {
        levelMatcher?.cancel()
        levelMatcher = nil
        stopWobble()
        head?.stop()
        engine.stop()
        isPlaying = false
    }
}

/// A smoothed RMS reading fed from an audio tap (thread-safe).
final class LevelFollower: @unchecked Sendable {
    private let lock = OSAllocatedUnfairLock(initialState: Float(0))
    var rms: Float { lock.withLock { $0 } }

    var tap: AVAudioNodeTapBlock {
        { [weak self] buffer, _ in
            guard let self, let data = buffer.floatChannelData else { return }
            let n = Int(buffer.frameLength), channels = Int(buffer.format.channelCount)
            guard n > 0 else { return }
            var sum: Float = 0
            for c in 0..<channels {
                for i in 0..<n { sum += data[c][i] * data[c][i] }
            }
            let now = (sum / Float(n * channels)).squareRoot()
            self.lock.withLock { $0 += (now - $0) * 0.3 }
        }
    }
}

/// Audio helpers shared by the sampler and the exporter.
enum ChopAudio {
    /// A copy of frames `start..<end`. With `fade`, a couple of milliseconds are eased in and out
    /// so a cut never clicks.
    static func segment(of buffer: AVAudioPCMBuffer, from start: Int, to end: Int, fade: Bool) -> AVAudioPCMBuffer? {
        let total = Int(buffer.frameLength)
        let lo = max(0, min(start, total)), hi = max(lo, min(end, total))
        let count = hi - lo
        guard count > 0, let src = buffer.floatChannelData,
              let out = AVAudioPCMBuffer(pcmFormat: buffer.format, frameCapacity: AVAudioFrameCount(count)),
              let dst = out.floatChannelData else { return nil }
        out.frameLength = AVAudioFrameCount(count)
        let channels = Int(buffer.format.channelCount)
        for c in 0..<channels {
            dst[c].update(from: src[c] + lo, count: count)
        }
        if fade {
            let rate = buffer.format.sampleRate
            let fadeIn = min(count / 4, Int(rate * 0.001))
            let fadeOut = min(count / 4, Int(rate * 0.004))
            for c in 0..<channels {
                for i in 0..<fadeIn { dst[c][i] *= Float(i) / Float(fadeIn) }
                for i in 0..<fadeOut { dst[c][count - 1 - i] *= Float(i) / Float(fadeOut) }
            }
        }
        return out
    }

    /// Writes a buffer as a 24-bit PCM WAV at its own sample rate.
    static func writeWAV(_ buffer: AVAudioPCMBuffer, to url: URL) throws {
        let settings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: buffer.format.sampleRate,
            AVNumberOfChannelsKey: buffer.format.channelCount,
            AVLinearPCMBitDepthKey: 24,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsBigEndianKey: false,
            AVLinearPCMIsNonInterleaved: false,
        ]
        let file = try AVAudioFile(forWriting: url, settings: settings,
                                   commonFormat: .pcmFormatFloat32, interleaved: false)
        try file.write(from: buffer)
    }

    /// Mono mix of the whole buffer, for analysis.
    static func mono(_ buffer: AVAudioPCMBuffer) -> [Float] {
        let n = Int(buffer.frameLength)
        guard let channels = buffer.floatChannelData else { return [] }
        var mono = Array(UnsafeBufferPointer(start: channels[0], count: n))
        for c in 1..<Int(buffer.format.channelCount) {
            for i in 0..<n { mono[i] += channels[c][i] }
        }
        return mono
    }
}
