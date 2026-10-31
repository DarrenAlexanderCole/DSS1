import Accelerate
import AVFoundation
import SwiftUI

/// Turns whatever is playing into 32 log-spaced frequency bands plus an overall level.
/// Written from the audio thread, read by the visualizer every frame.
final class LevelMeter: @unchecked Sendable {
    static let bandCount = 32
    private let size = 1024
    private let lock = NSLock()
    private var bands = [Float](repeating: 0, count: bandCount)
    private var level: Float = 0
    private var left: Float = 0
    private var right: Float = 0
    private let fft: vDSP.FFT<DSPSplitComplex>
    private let window: [Float]
    /// The graphic EQ's 20 bands, measured with a longer FFT so the low end is resolved.
    private var spectrumBands = [Float](repeating: 0, count: EQBands.frequencies.count)
    private let longFFT: vDSP.FFT<DSPSplitComplex>
    private let longWindow: [Float]
    private let longSize = 4096

    init() {
        fft = vDSP.FFT(log2n: 10, radix: .radix2, ofType: DSPSplitComplex.self)!
        window = vDSP.window(ofType: Float.self, usingSequence: .hanningDenormalized, count: 1024, isHalfWindow: false)
        longFFT = vDSP.FFT(log2n: 12, radix: .radix2, ofType: DSPSplitComplex.self)!
        longWindow = vDSP.window(ofType: Float.self, usingSequence: .hanningDenormalized, count: 4096, isHalfWindow: false)
    }

    /// Level in each of the EQ's 20 bands (0…1), for the spectrum analyzer.
    func spectrum() -> [Float] {
        lock.lock()
        defer { lock.unlock() }
        return spectrumBands
    }

    private static func magnitudes(_ input: UnsafeBufferPointer<Float>, window: [Float], fft: vDSP.FFT<DSPSplitComplex>) -> [Float] {
        let size = input.count, half = size / 2
        let windowed = vDSP.multiply(input, window)
        var real = [Float](repeating: 0, count: half)
        var imag = [Float](repeating: 0, count: half)
        var mags = [Float](repeating: 0, count: half)
        real.withUnsafeMutableBufferPointer { rp in
            imag.withUnsafeMutableBufferPointer { ip in
                var split = DSPSplitComplex(realp: rp.baseAddress!, imagp: ip.baseAddress!)
                windowed.withUnsafeBufferPointer { wp in
                    wp.baseAddress!.withMemoryRebound(to: DSPComplex.self, capacity: half) { cp in
                        vDSP_ctoz(cp, 2, &split, 1, vDSP_Length(half))
                    }
                }
                fft.forward(input: split, output: &split)
                mags.withUnsafeMutableBufferPointer { mp in
                    vDSP_zvabs(&split, 1, mp.baseAddress!, 1, vDSP_Length(half))
                }
            }
        }
        return mags
    }

    func snapshot() -> (bands: [Float], level: Float) {
        lock.lock()
        defer { lock.unlock() }
        return (bands, level)
    }

    /// Left and right loudness (RMS), for the VU meters.
    func stereo() -> (left: Float, right: Float) {
        lock.lock()
        defer { lock.unlock() }
        return (left, right)
    }

    func process(_ buffer: AVAudioPCMBuffer) {
        guard let channel = buffer.floatChannelData?[0], Int(buffer.frameLength) >= size else { return }
        let n = Int(buffer.frameLength)
        let l = vDSP.rootMeanSquare(UnsafeBufferPointer(start: channel, count: n))
        let r = buffer.format.channelCount > 1
            ? vDSP.rootMeanSquare(UnsafeBufferPointer(start: buffer.floatChannelData![1], count: n))
            : l
        let input = UnsafeBufferPointer(start: channel, count: size)
        let rms = vDSP.rootMeanSquare(input)
        let windowed = vDSP.multiply(input, window)

        let half = size / 2
        var real = [Float](repeating: 0, count: half)
        var imag = [Float](repeating: 0, count: half)
        var mags = [Float](repeating: 0, count: half)
        real.withUnsafeMutableBufferPointer { rp in
            imag.withUnsafeMutableBufferPointer { ip in
                var split = DSPSplitComplex(realp: rp.baseAddress!, imagp: ip.baseAddress!)
                windowed.withUnsafeBufferPointer { wp in
                    wp.baseAddress!.withMemoryRebound(to: DSPComplex.self, capacity: half) { cp in
                        vDSP_ctoz(cp, 2, &split, 1, vDSP_Length(half))
                    }
                }
                fft.forward(input: split, output: &split)
                mags.withUnsafeMutableBufferPointer { mp in
                    vDSP_zvabs(&split, 1, mp.baseAddress!, 1, vDSP_Length(half))
                }
            }
        }

        var fresh = [Float](repeating: 0, count: Self.bandCount)
        let top = Float(half - 1)
        for b in 0..<Self.bandCount {
            let lo = Int(pow(top, Float(b) / Float(Self.bandCount)))
            let hi = min(half, max(lo + 1, Int(pow(top, Float(b + 1) / Float(Self.bandCount)))))
            var sum: Float = 0
            for k in lo..<hi { sum += mags[k] }
            let avg = sum / Float(hi - lo)
            let db = 20 * log10(avg / Float(size) + 1e-7)
            fresh[b] = min(max((db + 62) / 52, 0), 1)
        }

        // The EQ display: half-octave bands around each EQ frequency.
        let longInput = n >= longSize ? UnsafeBufferPointer(start: channel, count: longSize) : nil
        let specMags = longInput.map { Self.magnitudes($0, window: longWindow, fft: longFFT) } ?? mags
        let specSize = longInput == nil ? size : longSize
        let binHz = Float(buffer.format.sampleRate) / Float(specSize)
        var spec = [Float](repeating: 0, count: EQBands.frequencies.count)
        for (i, fc) in EQBands.frequencies.enumerated() {
            let k0 = max(1, Int(fc / 1.19 / binHz))
            let k1 = min(specMags.count, max(k0 + 1, Int((fc * 1.19 / binHz).rounded(.up))))
            guard k0 < k1 else { continue }
            var sum: Float = 0
            for k in k0..<k1 { sum += specMags[k] }
            let db = 20 * log10(sum / Float(k1 - k0) / Float(specSize) + 1e-7)
            spec[i] = min(max((db + 66) / 54, 0), 1)
        }

        lock.lock()
        for i in 0..<spec.count { spectrumBands[i] = max(spec[i], spectrumBands[i] * 0.82) }
        for b in 0..<Self.bandCount { bands[b] = max(fresh[b], bands[b] * 0.8) }
        level = max(min(rms * 3, 1), level * 0.85)
        left = l
        right = r
        lock.unlock()
    }
}

@MainActor
final class AudioPlayer: ObservableObject {
    @Published private(set) var tracks: [URL] = []
    @Published private(set) var index = 0
    @Published private(set) var isPlaying = false
    @Published private(set) var currentTime: Double = 0
    @Published private(set) var duration: Double = 0
    /// 0…1, remembered between launches.
    @Published var volume: Double = UserDefaults.standard.object(forKey: "volume") as? Double ?? 0.8 {
        didSet {
            applyVolume()
            UserDefaults.standard.set(volume, forKey: "volume")
        }
    }

    /// LEVEL MATCH: plays every track at about the same loudness (files aren't touched).
    @Published var levelMatch = UserDefaults.standard.object(forKey: "levelMatch") as? Bool ?? true {
        didSet {
            UserDefaults.standard.set(levelMatch, forKey: "levelMatch")
            applyLevel()
        }
    }
    /// The level-match gain on the current track, in dB.
    @Published private(set) var levelGainDB: Float = 0
    /// Where loudness measurements come from (the library's analysis).
    var analysisFor: ((URL) -> TrackAnalyzer.Result?)?

    let meter = LevelMeter()
    /// 1 normally; −1 while REW is playing the track backwards at normal speed.
    @Published private(set) var direction = 1.0
    /// 1.5 while FF is running fast, otherwise 1.
    @Published private(set) var speed = 1.0 {
        didSet { varispeed.rate = Float(speed) }
    }

    private let engine = AVAudioEngine()
    private let node = AVAudioPlayerNode()
    private let levelStage = AVAudioMixerNode()
    private let varispeed = AVAudioUnitVarispeed()
    private let compressor = LibraryCompressor.shared.makeUnit()
    private let opto = OptoComp.shared.makeEffect()
    private let eq = AVAudioUnitEQ(numberOfBands: EQBands.frequencies.count)
    private let volumeMixer = AVAudioMixerNode()
    private var file: AVAudioFile?
    private var startFrame: AVAudioFramePosition = 0
    /// Bumped whenever playback is restarted, so stale completion callbacks are ignored.
    private var token = 0
    private var markedPlayed = false
    private var ticks = 0

    var currentURL: URL? { tracks.indices.contains(index) ? tracks[index] : nil }
    var current: TrackInfo? { currentURL.map(TrackInfo.init(url:)) }

    init() {
        engine.attach(node)
        engine.attach(levelStage)
        engine.attach(varispeed)
        engine.attach(eq)
        engine.attach(compressor)
        engine.attach(opto)
        // Level stage → EQ → compressor → main is wired once, in stereo at the output rate; each track only
        // reconnects the player into the level stage (a mixer, so it converts any format).
        // Rewiring the whole chain while the engine runs can hang it between tracks.
        let rate = engine.outputNode.outputFormat(forBus: 0).sampleRate
        let stereo = AVAudioFormat(standardFormatWithSampleRate: rate > 0 ? rate : 48000, channels: 2)
        engine.connect(node, to: levelStage, format: nil)
        engine.connect(levelStage, to: varispeed, format: stereo)
        engine.connect(varispeed, to: eq, format: stereo)
        engine.connect(eq, to: compressor, format: stereo)
        engine.connect(compressor, to: opto, format: stereo)
        engine.connect(opto, to: engine.mainMixerNode, format: stereo)
        MasterBus.install(in: engine, volume: volumeMixer, tap: Self.makeTap(meter))
        MasterEQ.shared.register(eq)
        applyVolume()
        Task { [weak self] in
            while true {
                try? await Task.sleep(for: .milliseconds(120))
                self?.tick()
            }
        }
    }

    // Built outside the main actor: these run on audio threads.
    nonisolated private static func makeTap(_ meter: LevelMeter) -> AVAudioNodeTapBlock {
        { buffer, _ in
            meter.process(buffer)
            LiveRecorder.shared.ingest(buffer, from: .library)
        }
    }

    nonisolated private static func makeCompletion(_ token: Int) -> AVAudioPlayerNodeCompletionHandler {
        { _ in Task { @MainActor in AppModel.shared.player.segmentFinished(token: token) } }
    }

    /// Plays `queue[index]`, then carries on down the queue.
    /// With `autoplay: false` the track is cued at `time`, ready for start.
    func play(queue: [URL], index: Int, from time: Double = 0, autoplay: Bool = true) {
        tracks = queue
        start(index, from: time, autoplay: autoplay)
    }

    /// Swaps in a re-ordered queue without interrupting the current track.
    func reorder(_ queue: [URL]) {
        guard let current = currentURL, let i = queue.firstIndex(of: current) else { return }
        tracks = queue
        index = i
    }

    func toggle() {
        guard file != nil else {
            start(index)
            return
        }
        if !isPlaying, direction < 0 {
            forward()
            speed = 1
            return
        }
        if !isPlaying { speed = 1 }
        if isPlaying {
            node.pause()
            isPlaying = false
            savePosition()
        } else {
            if !engine.isRunning { try? engine.start() }
            node.play()
            isPlaying = true
            LiveRecorder.shared.setSource(.library)
        }
    }

    /// REW: plays the track backwards from here at normal speed (a reversed copy of everything before this point).
    func rewind() {
        guard let f = file, let url = currentURL else { return }
        let rate = f.processingFormat.sampleRate
        let end = AVAudioFramePosition(currentTime * rate)
        guard end > 0 else { return }
        token += 1
        let mine = token
        node.stop()
        direction = -1
        speed = 1
        Task { [weak self] in
            let reversed = await Task.detached(priority: .userInitiated) { Self.reversed(url, upTo: end) }.value
            guard let self, self.token == mine else { return }
            guard let reversed else { self.direction = 1; return }
            self.startFrame = end
            self.node.scheduleBuffer(reversed, at: nil, options: [], completionCallbackType: .dataPlayedBack,
                                     completionHandler: Self.makeReverseCompletion(mine))
            if !self.engine.isRunning { try? self.engine.start() }
            self.node.play()
            self.isPlaying = true
            LiveRecorder.shared.setSource(.library)
        }
    }

    /// PLAY: forward at normal speed, from wherever it is (stopped, rewinding or fast-forwarding).
    func play1x() {
        if direction < 0 { forward() }
        speed = 1
        if !isPlaying { toggle() }
    }

    /// FF: plays forward at 1.5x from here (from reverse, or from a stop). PLAY brings it back to 1x.
    func forward() {
        guard let f = file else { start(index); return }
        if direction > 0 {
            if !isPlaying { toggle() }
            speed = 1.5
            return
        }
        speed = 1.5
        let frame = AVAudioFramePosition(currentTime * f.processingFormat.sampleRate)
        token += 1
        node.stop()
        direction = 1
        schedule(from: min(frame, max(f.length - 1, 0)))
        if !engine.isRunning { try? engine.start() }
        node.play()
        isPlaying = true
        LiveRecorder.shared.setSource(.library)
    }

    /// The first `end` frames of a track, last frame first.
    nonisolated private static func reversed(_ url: URL, upTo end: AVAudioFramePosition) -> AVAudioPCMBuffer? {
        guard let source = DecodedCache.shared.decode(url), let data = source.floatChannelData else { return nil }
        let n = Int(min(end, AVAudioFramePosition(source.frameLength)))
        guard n > 0, let out = AVAudioPCMBuffer(pcmFormat: source.format, frameCapacity: AVAudioFrameCount(n)),
              let dest = out.floatChannelData else { return nil }
        for c in 0..<Int(source.format.channelCount) {
            memcpy(dest[c], data[c], n * MemoryLayout<Float>.size)
            vDSP_vrvrs(dest[c], 1, vDSP_Length(n))
        }
        out.frameLength = AVAudioFrameCount(n)
        return out
    }

    nonisolated private static func makeReverseCompletion(_ token: Int) -> AVAudioPlayerNodeCompletionHandler {
        { _ in Task { @MainActor in AppModel.shared.player.reverseFinished(token: token) } }
    }

    /// Rewound all the way: park at the start.
    fileprivate func reverseFinished(token finished: Int) {
        guard finished == token, direction < 0 else { return }
        token += 1
        node.stop()
        direction = 1
        isPlaying = false
        schedule(from: 0)
        currentTime = 0
        savePosition()
    }

    func next() {
        if index + 1 < tracks.count { start(index + 1, autoplay: isPlaying || file == nil) }
    }

    func previous() {
        if currentTime > 3 || index == 0 { seek(to: 0) } else { start(index - 1, autoplay: isPlaying || file == nil) }
    }

    func seek(to fraction: Double) {
        guard let f = file else { return }
        direction = 1
        speed = 1
        let frame = AVAudioFramePosition(Double(f.length) * min(max(fraction, 0), 0.999))
        token += 1
        node.stop()
        schedule(from: frame)
        currentTime = Double(frame) / f.processingFormat.sampleRate
        if isPlaying { node.play() }
        savePosition()
    }

    private func start(_ i: Int, from time: Double = 0, autoplay: Bool = true) {
        guard tracks.indices.contains(i) else { return }
        token += 1
        node.stop()
        direction = 1
        speed = 1
        markedPlayed = false
        do {
            let f = try AVAudioFile(forReading: tracks[i])
            let rate = f.processingFormat.sampleRate
            file = f
            index = i
            duration = Double(f.length) / rate
            if node.outputFormat(forBus: 0) != f.processingFormat {
                engine.disconnectNodeOutput(node)
                engine.connect(node, to: levelStage, format: f.processingFormat)
            }
            applyLevel()
            if !engine.isRunning { try engine.start() }
            let frame = AVAudioFramePosition(min(max(time, 0), max(duration - 1, 0)) * rate)
            schedule(from: frame)
            currentTime = Double(frame) / rate
            if autoplay {
                node.play()
                isPlaying = true
                LiveRecorder.shared.setSource(.library)
            } else {
                isPlaying = false
            }
            savePosition()
        } catch {
            if autoplay, i + 1 < tracks.count { start(i + 1) } else { isPlaying = false }
        }
    }

    private func schedule(from frame: AVAudioFramePosition) {
        guard let f = file, f.length - frame > 0 else { return }
        startFrame = frame
        node.scheduleSegment(f, startingFrame: frame, frameCount: AVAudioFrameCount(f.length - frame),
                             at: nil, completionCallbackType: .dataPlayedBack,
                             completionHandler: Self.makeCompletion(token))
    }

    fileprivate func segmentFinished(token finished: Int) {
        guard finished == token else { return }
        if let url = currentURL { AppModel.shared.library.markPlayed(url) }
        if index + 1 < tracks.count {
            start(index + 1)
        } else {
            // End of the crate: stop, parked on the last track.
            token += 1
            node.stop()
            file = nil
            currentTime = 0
            isPlaying = false
        }
    }

    private func tick() {
        guard isPlaying, let f = file, let nodeTime = node.lastRenderTime,
              let playerTime = node.playerTime(forNodeTime: nodeTime) else { return }
        let rate = f.processingFormat.sampleRate
        currentTime = direction < 0
            ? max(Double(startFrame - playerTime.sampleTime) / rate, 0)
            : min(Double(startFrame + playerTime.sampleTime) / rate, duration)
        if !markedPlayed, currentTime > 20, let url = currentURL {
            markedPlayed = true
            AppModel.shared.library.markPlayed(url)
        }
        ticks += 1
        if ticks % 16 == 0 { savePosition() }
        // The measurement may land after the track started.
        if ticks % 8 == 0 { applyLevel() }
    }

    /// The hardware buffer size the player runs with (frames).
    var bufferFrames: UInt32 { MasterBus.bufferFrames(of: engine) }

    func applyLevel() {
        #if TESTHOOKS
        if ProcessInfo.processInfo.environment["CRATE_NOLEVEL"] != nil { return }
        #endif
        let db = levelMatch ? (currentURL.flatMap { analysisFor?($0) }.flatMap(LevelMatch.gainDB) ?? 0) : 0
        guard db != levelGainDB || levelStage.outputVolume != LevelMatch.linear(db) else { return }
        levelGainDB = db
        levelStage.outputVolume = LevelMatch.linear(db)
    }

    /// Squared so the knob feels even to the ear.
    private func applyVolume() {
        volumeMixer.outputVolume = Float(volume * volume)
    }

    /// Remembered so the app reopens where you left off.
    private func savePosition() {
        guard let url = currentURL else { return }
        UserDefaults.standard.set(url.path, forKey: "lastTrack")
        UserDefaults.standard.set(currentTime, forKey: "lastTime")
    }
}

/// Loudness matching: how much to turn a track up or down to land near the target,
/// never boosting past its own peak.
enum LevelMatch {
    /// LUFS. Hot enough for club masters to need little cut; quiet tracks come up to meet them.
    static let target = -11.0

    static func gainDB(_ r: TrackAnalyzer.Result) -> Float? {
        // Unmeasured, or silence: leave it alone.
        guard let lufs = r.loudness, lufs > -60 else { return nil }
        let headroom = min(9, -(r.peak ?? -9) - 0.3)
        return Float(min(max(target - lufs, -12), max(headroom, 0)))
    }

    static func linear(_ db: Float) -> Float { pow(10, db / 20) }
}
