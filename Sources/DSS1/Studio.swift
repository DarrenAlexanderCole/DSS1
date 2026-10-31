import Accelerate
import AppKit
import AVFoundation
import SwiftUI

// The studio chain shared by both players (the library player and the chop station):
//
//     … → 20-band EQ → main mixer ──(meters + live recorder tap here)──→ volume mixer → speakers
//
// The recorder listens before the volume knob, so takes are recorded at full level
// however loud you happen to be listening.

/// The 20 half-octave bands of the graphic EQ.
enum EQBands {
    static let frequencies: [Float] = [31.5, 45, 63, 90, 125, 180, 250, 355, 500, 710,
                                       1000, 1400, 2000, 2800, 4000, 5600, 8000, 11200, 16000, 20000]
    static let labels = ["31", "45", "63", "90", "125", "180", "250", "355", "500", "710",
                         "1k", "1.4k", "2k", "2.8k", "4k", "5.6k", "8k", "11k", "16k", "20k"]
    static let range: Float = 12
}

/// One set of EQ settings applied to every engine's EQ unit, and remembered between launches.
@MainActor
final class MasterEQ: ObservableObject {
    static let shared = MasterEQ()

    /// Gain per band in dB, −12…+12.
    @Published private(set) var gains: [Float]
    @Published var enabled: Bool {
        didSet {
            UserDefaults.standard.set(enabled, forKey: "eqEnabled")
            applyAll()
        }
    }

    private struct WeakUnit { weak var unit: AVAudioUnitEQ? }
    private var units: [WeakUnit] = []

    private init() {
        let saved = (UserDefaults.standard.array(forKey: "eqGains") as? [Double])?.map(Float.init) ?? []
        gains = saved.count == EQBands.frequencies.count ? saved : Array(repeating: 0, count: EQBands.frequencies.count)
        enabled = UserDefaults.standard.object(forKey: "eqEnabled") as? Bool ?? true
    }

    /// Hooks an engine's EQ unit up to the shared settings.
    func register(_ unit: AVAudioUnitEQ) {
        for (i, band) in unit.bands.enumerated() where i < EQBands.frequencies.count {
            band.filterType = .parametric
            band.frequency = EQBands.frequencies[i]
            band.bandwidth = 0.4
            band.bypass = false
        }
        unit.globalGain = 0
        units.removeAll { $0.unit == nil }
        units.append(WeakUnit(unit: unit))
        apply(to: unit)
    }

    func setGain(_ band: Int, _ db: Float) {
        guard gains.indices.contains(band) else { return }
        gains[band] = min(max(db, -EQBands.range), EQBands.range)
        UserDefaults.standard.set(gains.map(Double.init), forKey: "eqGains")
        applyAll()
    }

    func flatten() {
        gains = Array(repeating: 0, count: gains.count)
        UserDefaults.standard.set(gains.map(Double.init), forKey: "eqGains")
        applyAll()
    }

    private func applyAll() {
        units.removeAll { $0.unit == nil }
        for u in units { if let unit = u.unit { apply(to: unit) } }
    }

    private func apply(to unit: AVAudioUnitEQ) {
        for (i, band) in unit.bands.enumerated() where i < gains.count { band.gain = gains[i] }
        unit.bypass = !enabled
    }
}

/// Wires the end of an engine: main mixer (tapped for meters and the recorder) → volume mixer → speakers.
/// The Radio deck's compressor on the library player (not the chop station or performance pad).
/// Apple's DynamicsProcessor underneath: threshold, make-up gain and attack on the panel;
/// release (150 ms) and headroom (10 dB) fixed for a musical, glue-style squeeze.
@MainActor
final class LibraryCompressor: ObservableObject {
    static let shared = LibraryCompressor()

    /// dB, −40…0.
    @Published var threshold: Float { didSet { save(); apply() } }
    /// Make-up gain in dB, 0…+24.
    @Published var gain: Float { didSet { save(); apply() } }
    /// Attack in milliseconds, 0.1…200.
    @Published var attack: Float { didSet { save(); apply() } }
    @Published var enabled: Bool { didSet { save(); apply() } }

    static let thresholdRange: ClosedRange<Float> = -40...0
    static let gainRange: ClosedRange<Float> = 0...24
    static let attackRange: ClosedRange<Float> = 0.1...200

    private weak var unit: AVAudioUnitEffect?
    private let defaults = UserDefaults.standard

    private init() {
        threshold = defaults.object(forKey: "compThreshold") as? Float ?? -18
        gain = defaults.object(forKey: "compGain") as? Float ?? 4
        attack = defaults.object(forKey: "compAttack") as? Float ?? 10
        enabled = defaults.object(forKey: "compEnabled") as? Bool ?? false
    }

    /// A fresh DynamicsProcessor for an engine; the shared settings follow it.
    func makeUnit() -> AVAudioUnitEffect {
        let desc = AudioComponentDescription(componentType: kAudioUnitType_Effect,
                                             componentSubType: kAudioUnitSubType_DynamicsProcessor,
                                             componentManufacturer: kAudioUnitManufacturer_Apple,
                                             componentFlags: 0, componentFlagsMask: 0)
        let unit = AVAudioUnitEffect(audioComponentDescription: desc)
        self.unit = unit
        apply()
        return unit
    }

    /// How hard it's squeezing right now, in dB (0 when idle).
    var reduction: Float {
        guard let au = unit?.audioUnit, enabled else { return 0 }
        var v: AudioUnitParameterValue = 0
        AudioUnitGetParameter(au, kDynamicsProcessorParam_CompressionAmount, kAudioUnitScope_Global, 0, &v)
        return abs(v)
    }

    private func apply() {
        guard let unit else { return }
        unit.bypass = !enabled
        let au = unit.audioUnit
        func set(_ p: AudioUnitParameterID, _ v: Float) { AudioUnitSetParameter(au, p, kAudioUnitScope_Global, 0, v, 0) }
        set(kDynamicsProcessorParam_Threshold, threshold)
        set(kDynamicsProcessorParam_HeadRoom, 10)
        set(kDynamicsProcessorParam_AttackTime, attack / 1000)
        set(kDynamicsProcessorParam_ReleaseTime, 0.15)
        set(kDynamicsProcessorParam_OverallGain, gain)
        set(kDynamicsProcessorParam_ExpansionRatio, 1)
        set(kDynamicsProcessorParam_ExpansionThreshold, -100)
    }

    private func save() {
        defaults.set(threshold, forKey: "compThreshold")
        defaults.set(gain, forKey: "compGain")
        defaults.set(attack, forKey: "compAttack")
        defaults.set(enabled, forKey: "compEnabled")
    }
}

enum MasterBus {
    @MainActor
    static func install(in engine: AVAudioEngine, volume: AVAudioMixerNode, tap: @escaping AVAudioNodeTapBlock) {
        engine.attach(volume)
        let main = engine.mainMixerNode
        let rate = engine.outputNode.outputFormat(forBus: 0).sampleRate
        let stereo = AVAudioFormat(standardFormatWithSampleRate: rate > 0 ? rate : 48000, channels: 2)
        engine.disconnectNodeOutput(main)
        engine.connect(main, to: volume, format: stereo)
        engine.connect(volume, to: engine.outputNode, format: stereo)
        main.installTap(onBus: 0, bufferSize: 1024, format: nil, block: tap)
        setLowLatency(UserDefaults.standard.object(forKey: "lowLatency") as? Bool ?? true, on: engine)
    }

    /// 256-frame hardware buffer (about 5 ms at 48 kHz) so pads and MIDI answer quickly;
    /// off goes back to 1024 for weaker machines.
    @MainActor
    static func setLowLatency(_ on: Bool, on engine: AVAudioEngine) {
        guard let unit = engine.outputNode.audioUnit else { return }
        var frames: UInt32 = on ? 256 : 1024
        AudioUnitSetProperty(unit, kAudioDevicePropertyBufferFrameSize, kAudioUnitScope_Global, 0,
                             &frames, UInt32(MemoryLayout<UInt32>.size))
    }

    /// The hardware buffer an engine is running with, in frames.
    @MainActor
    static func bufferFrames(of engine: AVAudioEngine) -> UInt32 {
        guard let unit = engine.outputNode.audioUnit else { return 0 }
        var frames: UInt32 = 0
        var size = UInt32(MemoryLayout<UInt32>.size)
        AudioUnitGetProperty(unit, kAudioDevicePropertyBufferFrameSize, kAudioUnitScope_Global, 0, &frames, &size)
        return frames
    }
}

/// The last few tracks decoded into memory, so reopening one in Chop or Perform is instant.
final class DecodedCache: @unchecked Sendable {
    static let shared = DecodedCache()
    private let lock = NSLock()
    private var entries: [(path: String, buffer: AVAudioPCMBuffer)] = []
    private let keep = 3

    func buffer(for url: URL) -> AVAudioPCMBuffer? {
        lock.lock(); defer { lock.unlock() }
        guard let i = entries.firstIndex(where: { $0.path == url.path }) else { return nil }
        let hit = entries.remove(at: i)
        entries.append(hit)
        return hit.buffer
    }

    func store(_ buffer: AVAudioPCMBuffer, for url: URL) {
        lock.lock(); defer { lock.unlock() }
        entries.removeAll { $0.path == url.path }
        entries.append((url.path, buffer))
        if entries.count > keep { entries.removeFirst(entries.count - keep) }
    }

    /// Decodes (or reuses) the whole file.
    func decode(_ url: URL) -> AVAudioPCMBuffer? {
        if let cached = buffer(for: url) { return cached }
        guard let file = try? AVAudioFile(forReading: url),
              let buffer = AVAudioPCMBuffer(pcmFormat: file.processingFormat, frameCapacity: AVAudioFrameCount(file.length)),
              (try? file.read(into: buffer)) != nil else { return nil }
        store(buffer, for: url)
        return buffer
    }
}

// MARK: - Live recorder

enum AudioSource: Int, Sendable {
    case library, chop
}

/// Catches whatever the system is playing (the last player that started), effects and EQ included,
/// and writes it to a WAV. Fed from the audio taps, so it's thread-safe.
final class LiveRecorder: @unchecked Sendable {
    static let shared = LiveRecorder()

    private let lock = NSLock()
    private var source: AudioSource = .library
    private var recording = false
    private var url: URL?
    private var file: AVAudioFile?
    private var frames: AVAudioFramePosition = 0
    private var rate: Double = 48000
    private var peakL: Float = 0
    private var peakR: Float = 0
    private var failure: String?
    /// Input pad: attenuation applied to everything recorded (and shown on the meters).
    private var padGain: Float = 1
    /// Peak level per ~21 ms of the take, for the waveform readout.
    private var wave: [Float] = []
    static let waveStep = 1024
    private var waveAccum: Float = 0
    private var waveCount = 0
    private var scratch: AVAudioPCMBuffer?

    func setPad(dB: Double) {
        lock.lock(); defer { lock.unlock() }
        padGain = Float(pow(10, -abs(dB) / 20))
    }

    /// Whichever player started most recently is the one being heard, and recorded.
    func setSource(_ s: AudioSource) {
        lock.lock(); defer { lock.unlock() }
        source = s
    }

    func start(writingTo url: URL) {
        lock.lock(); defer { lock.unlock() }
        self.url = url
        file = nil
        frames = 0
        wave = []
        waveAccum = 0
        waveCount = 0
        failure = nil
        recording = true
    }

    /// Stops and closes the file.
    func finish() -> (seconds: Double, error: String?) {
        lock.lock(); defer { lock.unlock() }
        recording = false
        file = nil
        return (Double(frames) / rate, failure)
    }

    /// The take's waveform so far (peak per 1024 frames).
    func waveform() -> [Float] {
        lock.lock(); defer { lock.unlock() }
        return wave
    }

    var seconds: Double {
        lock.lock(); defer { lock.unlock() }
        return Double(frames) / rate
    }

    /// Input level (peak, 0…1) for the deck's meters, recording or not.
    func levels() -> (left: Float, right: Float) {
        lock.lock(); defer { lock.unlock() }
        return (peakL, peakR)
    }

    private func padded(_ buffer: AVAudioPCMBuffer) -> AVAudioPCMBuffer {
        guard padGain < 0.9999, let src = buffer.floatChannelData else { return buffer }
        if scratch == nil || scratch!.format != buffer.format || scratch!.frameCapacity < buffer.frameLength {
            scratch = AVAudioPCMBuffer(pcmFormat: buffer.format, frameCapacity: max(buffer.frameLength, 4096))
        }
        guard let out = scratch, let dst = out.floatChannelData else { return buffer }
        out.frameLength = buffer.frameLength
        let n = vDSP_Length(buffer.frameLength)
        var g = padGain
        let stride = buffer.format.isInterleaved ? 1 : 1
        let channels = buffer.format.isInterleaved ? 1 : Int(buffer.format.channelCount)
        let count = buffer.format.isInterleaved ? n * vDSP_Length(buffer.format.channelCount) : n
        for c in 0..<channels { vDSP_vsmul(src[c], stride, &g, dst[c], stride, count) }
        return out
    }

    func ingest(_ buffer: AVAudioPCMBuffer, from s: AudioSource) {
        lock.lock(); defer { lock.unlock() }
        guard s == source, buffer.floatChannelData != nil, buffer.frameLength > 0 else { return }
        let n = Int(buffer.frameLength)
        // The tap's buffer is shared with the other meters, so the pad works on a copy.
        let buffer = padded(buffer)
        guard let data = buffer.floatChannelData else { return }
        let l = vDSP.maximumMagnitude(UnsafeBufferPointer(start: data[0], count: n))
        let r = buffer.format.channelCount > 1 ? vDSP.maximumMagnitude(UnsafeBufferPointer(start: data[1], count: n)) : l
        peakL = max(l, peakL * 0.7)
        peakR = max(r, peakR * 0.7)

        guard recording else { return }
        if file == nil {
            guard let url else { return }
            let settings: [String: Any] = [
                AVFormatIDKey: kAudioFormatLinearPCM,
                AVSampleRateKey: buffer.format.sampleRate,
                AVNumberOfChannelsKey: buffer.format.channelCount,
                AVLinearPCMBitDepthKey: 24,
                AVLinearPCMIsFloatKey: false,
                AVLinearPCMIsBigEndianKey: false,
            ]
            do {
                file = try AVAudioFile(forWriting: url, settings: settings, commonFormat: buffer.format.commonFormat,
                                       interleaved: buffer.format.isInterleaved)
                rate = buffer.format.sampleRate
            } catch {
                failure = "Couldn't start recording: \(error.localizedDescription)"
                recording = false
                return
            }
        }
        guard let file, buffer.format.sampleRate == file.processingFormat.sampleRate,
              buffer.format.channelCount == file.processingFormat.channelCount else { return }
        do {
            try file.write(from: buffer)
            frames += AVAudioFramePosition(buffer.frameLength)
            let peak = max(vDSP.maximumMagnitude(UnsafeBufferPointer(start: data[0], count: n)),
                           buffer.format.channelCount > 1 ? vDSP.maximumMagnitude(UnsafeBufferPointer(start: data[1], count: n)) : 0)
            waveAccum = max(waveAccum, peak)
            waveCount += n
            while waveCount >= Self.waveStep {
                wave.append(waveAccum)
                waveCount -= Self.waveStep
                if waveCount < Self.waveStep { waveAccum = 0 }
            }
        } catch {
            failure = "Recording stopped: \(error.localizedDescription)"
            recording = false
        }
    }
}

/// The recording deck's controls: record, stop, play the take back, and save it to the live recordings folder.
@MainActor
final class LiveDeck: ObservableObject {
    static let shared = LiveDeck()

    enum State: Equatable { case idle, recording, ready, playing }

    @Published private(set) var state: State = .idle
    @Published private(set) var takeSeconds = 0.0
    @Published private(set) var status = "Ready to record what's playing"
    @Published private(set) var lastSaved: URL?
    @Published private(set) var takeSaved = false
    @Published var destination: URL {
        didSet { UserDefaults.standard.set(destination.path, forKey: "liveRecordingsFolder") }
    }
    /// Input pad in dB below full level: 0, 10, 30 or 60.
    static let padSteps = [0, 10, 30, 60]
    @Published var pad: Int {
        didSet {
            UserDefaults.standard.set(pad, forKey: "livePad")
            LiveRecorder.shared.setPad(dB: Double(pad))
        }
    }

    private var takeURL: URL?
    private var takeName = "Live"
    /// The finished take's waveform (peak per 1024 frames).
    private(set) var takeWave: [Float] = []
    /// Where playback starts from, as a fraction of the take, when it isn't running.
    @Published private(set) var cue = 0.0
    private var player: AVAudioPlayer?
    private var watcher: Task<Void, Never>?

    private init() {
        let savedPad = UserDefaults.standard.integer(forKey: "livePad")
        let startPad = Self.padSteps.contains(savedPad) ? savedPad : 0
        pad = startPad
        LiveRecorder.shared.setPad(dB: Double(startPad))
        if let path = UserDefaults.standard.string(forKey: "liveRecordingsFolder") {
            destination = URL(fileURLWithPath: path)
        } else if let library = AppModel.shared.destination {
            destination = library.appendingPathComponent("Live Playback Records", isDirectory: true)
        } else {
            destination = FileManager.default.urls(for: .musicDirectory, in: .userDomainMask)[0]
                .appendingPathComponent("Crate Live", isDirectory: true)
        }
    }

    var hasTake: Bool { takeURL != nil && (state == .ready || state == .playing) }

    func record() {
        guard state != .recording else { return }
        stopPlayback()
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("Crate Live", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let url = dir.appendingPathComponent("take-\(UUID().uuidString).wav")
        takeName = currentTitle()
        takeURL = url
        takeSaved = false
        takeWave = []
        cue = 0
        LiveRecorder.shared.start(writingTo: url)
        state = .recording
        status = "Recording what's playing: \(takeName)"
    }

    func stop() {
        switch state {
        case .recording:
            let result = LiveRecorder.shared.finish()
            if let error = result.error {
                state = .idle
                status = error
                return
            }
            takeSeconds = result.seconds
            takeWave = LiveRecorder.shared.waveform()
            cue = 0
            if result.seconds > 0.05 {
                state = .ready
                status = "Take ready (\(clock(takeSeconds))). PLAY to hear it, SAVE to keep it"
            } else {
                state = .idle
                takeURL = nil
                status = "Nothing was recorded"
            }
        case .playing:
            stopPlayback()
            cue = 0
            state = .ready
        case .ready:
            cue = 0
        default:
            break
        }
    }

    /// PLAY: plays the take from the playhead; pressed while playing, it pauses there.
    func play() {
        if state == .playing {
            cue = playbackFraction
            stopPlayback()
            state = .ready
            return
        }
        guard hasTake, let url = takeURL, let p = try? AVAudioPlayer(contentsOf: url) else {
            status = state == .recording ? "Stop recording first" : "Record something first"
            return
        }
        p.isMeteringEnabled = true
        p.currentTime = cue >= 0.999 ? 0 : cue * p.duration
        player = p
        p.play()
        state = .playing
        status = "Playing back the take"
        watcher?.cancel()
        watcher = Task {
            while !Task.isCancelled {
                try? await Task.sleep(for: .milliseconds(100))
                if player?.isPlaying != true {
                    if state == .playing {
                        state = .ready
                        cue = 0
                    }
                    break
                }
            }
        }
    }

    /// Moves the playhead through the take (dragged on the waveform). Playback carries on from there.
    func scrub(to fraction: Double) {
        guard hasTake else { return }
        let f = min(max(fraction, 0), 1)
        if state == .playing, let player {
            player.currentTime = f * player.duration
        }
        cue = f
    }

    /// Where the playhead is, 0…1.
    var playbackFraction: Double {
        if state == .playing, let player, player.duration > 0 { return player.currentTime / player.duration }
        return cue
    }

    /// Playback level per channel (0…1, linear peak), for the monitor's lights.
    func playbackLevels() -> (left: Float, right: Float) {
        guard state == .playing, let player else { return (0, 0) }
        player.updateMeters()
        func lin(_ db: Float) -> Float { pow(10, db / 20) }
        let l = lin(player.peakPower(forChannel: 0))
        let r = player.numberOfChannels > 1 ? lin(player.peakPower(forChannel: 1)) : l
        return (l, r)
    }

    func save() {
        guard hasTake, let url = takeURL else {
            status = state == .recording ? "Stop recording first" : "Record something first"
            return
        }
        guard !takeSaved else {
            status = "Already saved to \(destination.lastPathComponent)"
            return
        }
        let fm = FileManager.default
        do {
            try fm.createDirectory(at: destination, withIntermediateDirectories: true)
            let stamp = Self.stampFormatter.string(from: .now)
            var target = destination.appendingPathComponent("\(takeName) (Live \(stamp)).wav")
            var n = 2
            while fm.fileExists(atPath: target.path) {
                target = destination.appendingPathComponent("\(takeName) (Live \(stamp)) \(n).wav")
                n += 1
            }
            try fm.copyItem(at: url, to: target)
            takeSaved = true
            lastSaved = target
            status = "Saved to \(destination.lastPathComponent)"
        } catch {
            status = "Couldn't save: \(error.localizedDescription)"
        }
    }

    func chooseDestination() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.prompt = "Save Takes Here"
        panel.directoryURL = destination.deletingLastPathComponent()
        if panel.runModal() == .OK, let url = panel.url { destination = url }
    }

    func revealLastSaved() {
        if let lastSaved { NSWorkspace.shared.activateFileViewerSelecting([lastSaved]) }
        else { NSWorkspace.shared.open(destination) }
    }

    /// Seconds on the deck's counter: the take so far while recording, else the take's length.
    var counterSeconds: Double {
        state == .recording ? LiveRecorder.shared.seconds : (state == .playing ? (player?.currentTime ?? 0) : takeSeconds)
    }

    /// Seconds at the playhead.
    var playheadSeconds: Double {
        state == .playing ? (player?.currentTime ?? 0) : cue * takeSeconds
    }

    private func stopPlayback() {
        player?.stop()
        player = nil
        watcher?.cancel()
    }

    private func currentTitle() -> String {
        let model = AppModel.shared
        let title: String
        if let chop = model.chop, chop.isPlaying {
            title = chop.track.info.title
        } else {
            title = model.player.current?.title ?? "Crate"
        }
        let cleaned = title.components(separatedBy: CharacterSet(charactersIn: "/:\\?%*|\"<>")).joined(separator: "-")
        return String(cleaned.prefix(60)).trimmingCharacters(in: .whitespaces)
    }

    private static let stampFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "yyyy-MM-dd HH.mm.ss"
        return f
    }()
}
