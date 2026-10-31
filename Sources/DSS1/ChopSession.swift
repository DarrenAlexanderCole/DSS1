import AppKit
import AVFoundation
import CryptoKit
import SwiftUI
import UniformTypeIdentifiers

/// A saved loop: where it starts, how long it runs, and how many bars that is.
struct LoopMemory: Codable, Equatable {
    var start: Int
    var length: Int
    var bars: Double
}

/// "1/4", "1 BAR", "8 BARS".
func barLabel(_ bars: Double) -> String {
    if bars < 1 { return "1/\(Int((1 / bars).rounded()))" }
    let n = Int(bars.rounded())
    return "\(n) BAR" + (n == 1 ? "" : "S")
}

/// A region of the track sitting on a pad.
struct PadSlot: Codable, Equatable {
    var start: Int
    var end: Int
}

/// Per-pixel loudness of the whole track, precomputed so the waveform redraws instantly.
struct Peaks {
    static let bucket = 64
    let levels: [Float]

    /// Loudest bucket between two frames.
    func level(from start: Int, to end: Int) -> Float {
        let lo = max(0, start / Self.bucket)
        let hi = min(levels.count, max(lo + 1, end / Self.bucket))
        guard lo < hi else { return 0 }
        var top: Float = 0
        for i in lo..<hi where levels[i] > top { top = levels[i] }
        return top
    }
}

enum ChopMode: String, CaseIterable, Identifiable {
    case transient, beat, equal
    var id: String { rawValue }
    var label: String { rawValue.uppercased() }
}

/// Everything about chopping one track: the audio, slice markers, the 128 pads, and export.
/// Markers and pads are saved per track, so a chop can be picked up later.
@MainActor
final class ChopSession: ObservableObject {
    nonisolated static let padCount = 128
    nonisolated static let bankLetters = Array("ABCDEFGH")

    let track: LibraryTrack
    let analysis: TrackAnalyzer.Result?

    @Published private(set) var buffer: AVAudioPCMBuffer?
    @Published private(set) var peaks: Peaks?
    @Published private(set) var loadError: String?

    /// Slice start points (frames), sorted. The track's first frame always starts slice 1.
    @Published var markers: [Int] = [] { didSet { scheduleSave() } }
    @Published var pads: [Int: PadSlot] = [:] { didSet { scheduleSave() } }

    @Published var selectedSlice: Int?
    @Published var selectedPad = 0
    @Published var bank = 0

    /// Visible part of the track, as fractions 0…1.
    @Published var viewStart = 0.0
    @Published var viewEnd = 1.0

    @Published var mode: ChopMode = .transient
    @Published var sensitivity = 0.3
    @Published var beatsPerSlice = 1
    @Published var equalCount = 16

    @Published private(set) var isPlaying = false
    /// The looping region, while the looper is on.
    @Published private(set) var loopRange: Range<Int>? {
        didSet { if loopRange == nil { activeMemory = nil } }
    }
    /// Five loop memories for this track, recalled with the keys under the LOOP button.
    @Published private(set) var loopMemories: [LoopMemory?] = Array(repeating: nil, count: 5) {
        didSet { scheduleSave() }
    }
    /// Which memory is looping right now, if the loop came from one.
    @Published private(set) var activeMemory: Int?
    /// SAVE has been pressed: the next memory key stores the loop.
    @Published private(set) var savingLoop = false
    private var lastLoop: Range<Int>?
    private var changingBarsQuietly = false
    static let loopSizes: [Double] = [1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2, 1, 2, 4, 8, 16, 32]
    /// Pads pressed since Chop opened; the key overlay stays on the rest.
    @Published private(set) var revealedPads: Set<Int> = []
    @Published var loopBars = 4.0 {
        didSet {
            guard !changingBarsQuietly else { return }
            if let loop = loopRange { startLoop(at: loop.lowerBound) }
        }
    }
    /// Turntable pitch in %, within ±`pitchRange`.
    @Published var pitch = 0.0 {
        didSet { sampler.setPitch(pitch) }
    }
    /// KEY LOCK: the pitch fader changes the tempo but not the key (Rubber Band). Remembered.
    @Published var keyLock = UserDefaults.standard.bool(forKey: "keyLock") {
        didSet {
            sampler.setKeyLock(keyLock)
            UserDefaults.standard.set(keyLock, forKey: "keyLock")
        }
    }
    /// The fader's range: ±8, ±16 or ±50 %.
    @Published var pitchRange = 8 {
        didSet { pitch = min(max(pitch, -Double(pitchRange)), Double(pitchRange)) }
    }
    /// The direction lever: 1 = normal forward, 0 = stopped, −1 = full-speed reverse.
    @Published var direction = 1.0 {
        didSet { sampler.setDirection(direction) }
    }

    func cyclePitchRange() {
        let ranges = [8, 16, 50]
        pitchRange = ranges[((ranges.firstIndex(of: pitchRange) ?? 0) + 1) % ranges.count]
    }
    /// The FX knob the MIDI encoder is on: the last one clicked.
    @Published var selectedFX: FXKind?
    @Published var fx: [FXKind: FXState] = Dictionary(uniqueKeysWithValues: FXKind.allCases.map { ($0, FXState()) }) {
        didSet { sampler.apply(fx, bpm: analysis?.bpm, sampleRate: sampleRate) }
    }
    /// Where playback starts from (and where the playhead rests while paused).
    @Published var cursor = 0
    /// Bumped on every pad hit so the pad can flash.
    @Published private(set) var hit: (pad: Int, count: Int) = (-1, 0)
    @Published private(set) var isBusy = false
    @Published var status: String?

    let sampler = Sampler()
    private var saveTask: Task<Void, Never>?
    private var loaded = false

    init(track: LibraryTrack, analysis: TrackAnalyzer.Result?) {
        self.track = track
        self.analysis = analysis
    }

    var frameCount: Int { Int(buffer?.frameLength ?? 0) }
    var sampleRate: Double { buffer?.format.sampleRate ?? 44100 }

    /// The regions between markers: each starts at a marker and runs to the next (the last to the end).
    /// With no markers, the whole track is one slice.
    var slices: [Range<Int>] {
        guard frameCount > 0 else { return [] }
        let starts = markers.filter { $0 >= 0 && $0 < frameCount }
        guard !starts.isEmpty else { return [0..<frameCount] }
        let bounds = starts + [frameCount]
        return zip(bounds, bounds.dropFirst()).compactMap { $0 < $1 ? $0..<$1 : nil }
    }

    func padName(_ index: Int) -> String {
        "\(Self.bankLetters[index / 16])\(String(format: "%02d", index % 16 + 1))"
    }

    // MARK: - Loading

    func load() async {
        guard buffer == nil else { return }
        let url = track.url
        let result: (AVAudioPCMBuffer, Peaks)? = await Task.detached(priority: .userInitiated) {
            guard let buffer = DecodedCache.shared.decode(url),
                  let channels = buffer.floatChannelData else { return nil }
            let n = Int(buffer.frameLength)
            let count = n / Peaks.bucket + 1
            var levels = [Float](repeating: 0, count: count)
            for c in 0..<Int(buffer.format.channelCount) {
                for i in 0..<n {
                    let v = abs(channels[c][i])
                    let b = i / Peaks.bucket
                    if v > levels[b] { levels[b] = v }
                }
            }
            return (buffer, Peaks(levels: levels))
        }.value

        guard let (buffer, peaks) = result else {
            loadError = "Couldn't open this track"
            return
        }
        self.buffer = buffer
        self.peaks = peaks
        do {
            try sampler.load(track.url, buffer: buffer)
            sampler.setKeyLock(keyLock)
            sampler.apply(fx, bpm: analysis?.bpm, sampleRate: sampleRate)
        } catch {
            loadError = "Couldn't play this track"
        }
        restore()
        loaded = true
    }

    // MARK: - Transport

    /// The frame under the playhead: moving while playing, resting at the cursor while paused.
    var playhead: Int { sampler.position.map { min($0, frameCount) } ?? cursor }

    func togglePlay() {
        loopRange = nil
        if isPlaying {
            cursor = sampler.pause() ?? cursor
            isPlaying = false
        } else {
            if cursor >= frameCount - 1 { cursor = 0 }
            play(from: cursor)
        }
    }

    private func play(from frame: Int) {
        guard frameCount > 0 else { return }
        loopRange = nil
        cursor = frame
        sampler.play(from: frame)
        isPlaying = true
        watchForEnd()
    }

    /// Moves the playhead; if playing, playback jumps there.
    func seek(to frame: Int) {
        let frame = min(max(0, frame), max(0, frameCount - 1))
        if isPlaying { play(from: frame) } else { cursor = frame }
    }

    private var watcher: Task<Void, Never>?

    private func watchForEnd() {
        watcher?.cancel()
        watcher = Task {
            while !Task.isCancelled, isPlaying {
                try? await Task.sleep(for: .milliseconds(100))
                if sampler.reachedEdge {
                    cursor = sampler.pause() ?? cursor
                    isPlaying = false
                }
            }
        }
    }

    // MARK: - Looper & skipping

    /// One bar at the track's tempo (4 beats), or 2 seconds when the tempo is unknown.
    var barFrames: Int {
        guard let bpm = analysis?.bpm, bpm > 0 else { return Int(2 * sampleRate) }
        return Int(4 * 60 / bpm * sampleRate)
    }

    var isLooping: Bool { loopRange != nil }

    /// LOOP: locks onto `loopBars` bars from the playhead (or the selected pad, when stopped).
    func toggleLoop() {
        if let loop = loopRange {
            let at = sampler.position ?? loop.lowerBound
            loopRange = nil
            play(from: at)
            status = "Loop off"
        } else {
            let start = isPlaying ? playhead : (pads[selectedPad]?.start ?? cursor)
            startLoop(at: start)
        }
    }

    private func startLoop(at start: Int) {
        guard frameCount > 0 else { return }
        let start = min(max(0, start), frameCount - 1)
        let range = start..<min(frameCount, start + max(Int(Double(barFrames) * loopBars), Int(0.1 * sampleRate)))
        guard range.count > 0 else { return }
        beginLoop(range)
        status = "Looping \(barLabel(loopBars).lowercased()) from \(timestamp(start))"
    }

    /// Plays `range` as a loop from its first frame (also how a loop is retriggered).
    private func beginLoop(_ range: Range<Int>, memory: Int? = nil) {
        sampler.playLoop(range)
        loopRange = range
        lastLoop = range
        activeMemory = memory
        cursor = range.lowerBound
        isPlaying = true
        watchForEnd()
    }

    /// RETRIGGER: jump back to the start of the loop and play it again from the top.
    /// With no loop running it restarts the last one, or begins a new one from the playhead.
    func retriggerLoop() {
        guard frameCount > 0 else { return }
        if let range = loopRange ?? lastLoop {
            beginLoop(range, memory: loopRange == nil ? nil : activeMemory)
            status = "Retriggered from \(timestamp(range.lowerBound))"
        } else {
            startLoop(at: isPlaying ? playhead : (pads[selectedPad]?.start ?? cursor))
        }
    }

    // MARK: Loop memories

    /// The loop SAVE would store: the one that's running, else the last one, else what LOOP would start.
    private var loopToSave: Range<Int>? {
        if let range = loopRange ?? lastLoop { return range }
        guard frameCount > 0 else { return nil }
        let start = min(max(0, isPlaying ? playhead : (pads[selectedPad]?.start ?? cursor)), frameCount - 1)
        let range = start..<min(frameCount, start + max(Int(Double(barFrames) * loopBars), Int(0.1 * sampleRate)))
        return range.isEmpty ? nil : range
    }

    /// SAVE: arms the memory keys; the next one pressed stores the loop. Press SAVE again to cancel.
    func armLoopSave() {
        if savingLoop {
            savingLoop = false
            status = "Save cancelled"
        } else if loopToSave == nil {
            status = "Nothing to save yet"
        } else {
            savingLoop = true
            status = "Pick memory 1–5 to store this loop"
        }
    }

    /// A memory key: stores the loop if SAVE is armed, otherwise recalls and loops that memory.
    func pressLoopMemory(_ slot: Int) {
        guard loopMemories.indices.contains(slot) else { return }
        if savingLoop {
            guard let range = loopToSave else { savingLoop = false; return }
            let raw = Double(range.count) / Double(max(barFrames, 1))
            let bars = Self.loopSizes.min { abs($0 - raw) < abs($1 - raw) } ?? 1
            loopMemories[slot] = LoopMemory(start: range.lowerBound, length: range.count, bars: bars)
            savingLoop = false
            status = "Loop saved to memory \(slot + 1): \(barLabel(bars).lowercased()) from \(timestamp(range.lowerBound))"
            return
        }
        guard let memory = loopMemories[slot] else {
            status = "Memory \(slot + 1) is empty: press SAVE, then this key"
            return
        }
        let start = min(max(0, memory.start), max(0, frameCount - 1))
        let range = start..<min(frameCount, start + memory.length)
        guard !range.isEmpty else { return }
        if Self.loopSizes.contains(memory.bars) {
            changingBarsQuietly = true
            loopBars = memory.bars
            changingBarsQuietly = false
        }
        beginLoop(range, memory: slot)
        status = "Memory \(slot + 1): looping \(barLabel(memory.bars).lowercased()) from \(timestamp(start))"
    }

    func clearLoopMemory(_ slot: Int) {
        guard loopMemories.indices.contains(slot) else { return }
        loopMemories[slot] = nil
        if activeMemory == slot { activeMemory = nil }
        status = "Memory \(slot + 1) cleared"
    }

    /// ◀◀ / ▶▶: jump by whole bars.
    func skip(bars: Int) {
        seek(to: playhead + bars * barFrames)
    }

    /// REW / FF: plays at normal speed backwards (−1) or forwards (1) from here.
    func run(direction d: Double) {
        guard frameCount > 0 else { return }
        var at = cursor
        if d > 0, at >= frameCount - 1 { at = 0 }
        if d < 0, !isPlaying, at <= 0 { return }
        direction = d
        if !isPlaying { play(from: at) }
    }

    func stopSound() {
        if isPlaying { cursor = sampler.pause() ?? cursor }
        isPlaying = false
        loopRange = nil
    }

    /// Click on the waveform: move the playhead there and select the slice under it.
    func selectSlice(at frame: Int) {
        selectedSlice = slices.firstIndex { $0.contains(frame) }
        seek(to: frame)
    }

    // MARK: - Pad taps

    /// A filled pad jumps playback to its start and keeps playing, so the next empty pad can be tapped.
    /// An empty pad (or any pad with `resetStart`, e.g. ⌥-tap) takes its start from the playhead.
    func hitPad(_ index: Int, resetStart: Bool = false) {
        guard frameCount > 0 else { return }
        selectedPad = index
        revealedPads.insert(index)
        hit = (index, hit.count + 1)
        if let slot = pads[index], !resetStart {
            play(from: slot.start)
        } else {
            setStart(of: index, at: playhead)
        }
    }

    /// Gives a pad its start point. The pad that starts just before it now ends here,
    /// and this pad runs until the next pad's start (or the end of the track).
    func setStart(of index: Int, at frame: Int) {
        let frame = min(max(0, frame), frameCount - 1)
        var updated = pads
        updated[index] = nil
        if let previous = updated.filter({ $0.value.start < frame }).max(by: { $0.value.start < $1.value.start }) {
            updated[previous.key]?.end = frame
        }
        let next = updated.values.map(\.start).filter { $0 > frame }.min() ?? frameCount
        updated[index] = PadSlot(start: frame, end: next)
        withAnimation(Motion.jelly) { pads = updated }
        if !markers.contains(where: { abs($0 - frame) < minimumGap }) {
            markers = (markers + [frame]).sorted()
        }
        status = "\(padName(index)) starts at \(timestamp(frame))"
    }

    // MARK: - Nudging

    /// How wide the waveform is on screen, so nudges can follow the zoom.
    var viewPixels: Double = 1000

    /// Frames covered by one screen pixel at the current zoom.
    var framesPerPixel: Double {
        max(1, (viewEnd - viewStart) * Double(frameCount) / max(viewPixels, 1))
    }

    /// The nudge step, in words, e.g. "1 px = 0.4 ms".
    var nudgeStepLabel: String {
        let ms = framesPerPixel / sampleRate * 1000
        return ms < 1 ? String(format: "1 px = %.2f ms", ms) : String(format: "1 px = %.0f ms", ms)
    }

    /// Moves the selected pad's start by `pixels` at the current zoom (or exactly `samples` if given).
    /// The pad that ended on the old start follows, so the chops stay joined.
    /// With an empty pad selected, the playhead moves instead.
    func nudge(pixels: Double = 0, samples: Int? = nil) {
        guard frameCount > 0 else { return }
        let raw = samples.map(Double.init) ?? pixels * framesPerPixel
        let delta = raw < 0 ? min(-1, Int(raw.rounded())) : max(1, Int(raw.rounded()))
        guard let slot = pads[selectedPad] else {
            seek(to: playhead + delta)
            return
        }
        guard let new = movePadStart(selectedPad, to: slot.start + delta) else { return }
        seek(to: new)
    }

    /// Moves a pad's start point (dragging its flag, or nudging). The pad that ended on the old start
    /// follows, so chops stay joined; a pad can't pass its own end or the start of the pad before it.
    /// Returns the new start, or nil if nothing moved.
    @discardableResult
    func movePadStart(_ index: Int, to frame: Int) -> Int? {
        guard let slot = pads[index] else { return nil }
        let old = slot.start
        let floor = (pads.values.map(\.start).filter { $0 < old }.max() ?? -1) + 1
        let new = min(max(frame, max(0, floor)), slot.end - 1)
        guard new != old else { return nil }
        var updated = pads
        for (key, other) in updated where other.end == old { updated[key]?.end = new }
        updated[index]?.start = new
        pads = updated
        if let m = markers.firstIndex(of: old) {
            markers[m] = new
            markers.sort()
        }
        status = "\(padName(index)) starts at \(timestamp(new))"
        return new
    }

    func timestamp(_ frame: Int) -> String {
        let seconds = Double(frame) / sampleRate
        return String(format: "%d:%06.3f", Int(seconds) / 60, seconds.truncatingRemainder(dividingBy: 60))
    }

    // MARK: - Markers

    private var minimumGap: Int { Int(sampleRate * 0.01) }

    func addMarker(at frame: Int) {
        guard frame >= 0, frame < frameCount - minimumGap,
              !markers.contains(where: { abs($0 - frame) < minimumGap }) else { return }
        markers = (markers + [frame]).sorted()
        selectedSlice = slices.firstIndex { $0.lowerBound == frame }
    }

    func removeMarker(_ index: Int) {
        guard markers.indices.contains(index) else { return }
        markers.remove(at: index)
        selectedSlice = nil
    }

    func moveMarker(_ index: Int, to frame: Int) {
        guard markers.indices.contains(index) else { return }
        let lower = index > 0 ? markers[index - 1] + minimumGap : 0
        let upper = index + 1 < markers.count ? markers[index + 1] - minimumGap : frameCount - minimumGap
        guard lower < upper else { return }
        let old = markers[index]
        let new = min(max(frame, lower), upper)
        markers[index] = new
        // Pads whose start or end sat on this marker follow it.
        var updated = pads
        for (key, slot) in updated {
            if slot.start == old { updated[key]?.start = new }
            if slot.end == old { updated[key]?.end = new }
        }
        if updated != pads { pads = updated }
    }

    func clearMarkers() {
        withAnimation(Motion.bouncy) {
            markers = []
            selectedSlice = nil
        }
    }

    // MARK: - Auto chop

    func autoChop() {
        guard let buffer, !isBusy else { return }
        isBusy = true
        let mode = mode, sensitivity = sensitivity, beats = beatsPerSlice, equal = equalCount
        let bpm = analysis?.bpm
        let total = frameCount
        let rate = sampleRate
        // Only the part of the track in view is chopped; markers outside it stay put.
        let lo = Int(viewStart * Double(total)), hi = Int(viewEnd * Double(total))
        let kept = markers.filter { $0 < lo || $0 > hi }
        Task {
            let found: [Int] = await Task.detached(priority: .userInitiated) {
                switch mode {
                case .equal:
                    // The view's edges become markers too, so the region splits into exactly `equal` pieces.
                    return (0...equal).map { lo + (hi - lo) * $0 / equal }
                case .transient:
                    let mono = Array(ChopAudio.mono(buffer)[lo..<hi])
                    return Self.transients(in: mono, sampleRate: rate, sensitivity: sensitivity).map { $0 + lo }
                case .beat:
                    guard let bpm else { return [] }
                    let mono = Array(ChopAudio.mono(buffer)[lo..<hi])
                    let anchor = lo + (Self.transients(in: mono, sampleRate: rate, sensitivity: 0.6).first ?? 0)
                    let step = Double(beats) * 60 / bpm * rate
                    var points: [Int] = []
                    var p = Double(anchor)
                    while p < Double(hi) {
                        points.append(Int(p))
                        p += step
                    }
                    return points
                }
            }.value
            withAnimation(Motion.bouncy) {
                let fresh = found.filter { $0 > 0 && $0 < total }
                markers = Array((kept + fresh).sorted().prefix(Self.padCount - 1))
                selectedSlice = slices.firstIndex { $0.contains(fresh.first ?? lo) }
            }
            isBusy = false
            status = markers.isEmpty
                ? (mode == .beat && bpm == nil ? "No tempo for this track yet" : "Nothing found, try more sensitivity")
                : "\(slices.count) slices"
        }
    }

    /// Hits in the track: peaks in spectral flux that stand out from their surroundings.
    nonisolated static func transients(in mono: [Float], sampleRate: Double, sensitivity: Double) -> [Int] {
        let envelope = TrackAnalyzer.onsetEnvelope(mono)
        guard envelope.count > 8 else { return [] }
        let mean = envelope.reduce(0, +) / Float(envelope.count)
        let spread = sqrt(envelope.reduce(0) { $0 + ($1 - mean) * ($1 - mean) } / Float(envelope.count))
        let threshold = mean + spread * Float(3.2 - 2.8 * sensitivity)
        let hop = 256
        let minGap = Int(0.09 * sampleRate / Double(hop))

        var picks: [(index: Int, strength: Float)] = []
        for i in 2..<(envelope.count - 2) {
            let v = envelope[i]
            guard v > threshold, v >= envelope[i - 1], v >= envelope[i + 1], v >= envelope[i - 2], v >= envelope[i + 2] else { continue }
            if let last = picks.last, i - last.index < minGap {
                if v > last.strength { picks[picks.count - 1] = (i, v) }
                continue
            }
            picks.append((i, v))
        }
        // Keep the strongest if there are more than the pads can hold.
        if picks.count > padCount - 1 {
            picks = Array(picks.sorted { $0.strength > $1.strength }.prefix(padCount - 1))
        }
        return picks.map { $0.index * hop + hop / 2 }.sorted()
    }

    // MARK: - Pads

    /// Puts the selected slice on the selected pad, then steps both forward.
    func assignSelected() {
        guard let i = selectedSlice, slices.indices.contains(i) else {
            status = "Pick a slice on the waveform first"
            return
        }
        let range = slices[i]
        withAnimation(Motion.jelly) { pads[selectedPad] = PadSlot(start: range.lowerBound, end: range.upperBound) }
        hit = (selectedPad, hit.count + 1)
        if selectedPad + 1 < Self.padCount {
            selectedPad += 1
            bank = selectedPad / 16
        }
        if i + 1 < slices.count { selectedSlice = i + 1 }
    }

    /// Every slice in order onto pads A01, A02, … across the banks.
    func autoMap() {
        let all = slices.prefix(Self.padCount)
        guard !all.isEmpty else { return }
        withAnimation(Motion.jelly) {
            pads = Dictionary(uniqueKeysWithValues: all.enumerated().map { ($0.offset, PadSlot(start: $0.element.lowerBound, end: $0.element.upperBound)) })
            bank = 0
            selectedPad = 0
        }
        status = "\(all.count) slices mapped from A01"
    }

    func clearPad() {
        withAnimation(Motion.bouncy) { _ = pads.removeValue(forKey: selectedPad) }
    }

    func clearBank() {
        withAnimation(Motion.bouncy) {
            for i in (bank * 16)..<(bank * 16 + 16) { pads.removeValue(forKey: i) }
        }
    }

    // MARK: - View

    /// A dragged-out region of the waveform (for zooming to).
    @Published var selection: Range<Int>?

    /// Deepest zoom: about 2,000 samples across the whole waveform.
    private var minViewWidth: Double { min(1, max(0.0002, 2000 / Double(max(frameCount, 1)))) }

    /// Shows `width` of the track (0…1) with `fraction` sitting `ratio` of the way across the screen.
    /// This is what keeps the spot under the mouse still while zooming.
    func setView(width: Double, keeping fraction: Double, at ratio: Double) {
        let w = min(1, max(minViewWidth, width))
        let start = min(max(0, fraction - w * min(max(ratio, 0), 1)), 1 - w)
        viewStart = start
        viewEnd = start + w
    }

    func zoom(by factor: Double, around anchor: Double? = nil) {
        guard frameCount > 0 else { return }
        let width = viewEnd - viewStart
        let pivot = anchor ?? (viewStart + width / 2)
        setView(width: width / factor, keeping: pivot, at: width > 0 ? (pivot - viewStart) / width : 0.5)
    }

    func pan(toCenter center: Double) {
        let width = viewEnd - viewStart
        setView(width: width, keeping: center, at: 0.5)
    }

    func zoomToFit() {
        viewStart = 0
        viewEnd = 1
    }

    func zoomToSelection() {
        guard let selection, frameCount > 0 else { return zoomToFit() }
        let a = Double(selection.lowerBound) / Double(frameCount)
        let b = Double(selection.upperBound) / Double(frameCount)
        let margin = (b - a) * 0.04
        setView(width: b - a + margin * 2, keeping: a - margin, at: 0)
    }

    // MARK: - Export

    private var baseName: String {
        let title = track.info.title.isEmpty ? track.url.deletingPathExtension().lastPathComponent : track.info.title
        let cleaned = title.components(separatedBy: CharacterSet(charactersIn: "/:\\?%*|\"<>")).joined(separator: "-")
        return String(cleaned.prefix(40)).trimmingCharacters(in: .whitespaces)
    }

    /// What will be written: assigned pads, or, if none are assigned, every slice from A01.
    private var exportPlan: [(pad: Int, slot: PadSlot)] {
        if !pads.isEmpty { return pads.sorted { $0.key < $1.key }.map { ($0.key, $0.value) } }
        return slices.prefix(Self.padCount).enumerated().map { ($0.offset, PadSlot(start: $0.element.lowerBound, end: $0.element.upperBound)) }
    }

    /// Asks where to save, then writes "<title> Chops/A01 <title>.wav" etc.
    func exportSlices(to preset: URL? = nil) {
        guard let buffer, !isBusy else { return }
        let plan = exportPlan
        guard !plan.isEmpty else {
            status = "Chop the track first"
            return
        }
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.prompt = "Export Here"
        panel.message = "Where should the chops folder go?"
        if let last = UserDefaults.standard.string(forKey: "chopsDestination") {
            panel.directoryURL = URL(fileURLWithPath: last)
        }
        guard let destination = preset ?? (panel.runModal() == .OK ? panel.url : nil) else { return }
        UserDefaults.standard.set(destination.path, forKey: "chopsDestination")

        isBusy = true
        status = "Exporting \(plan.count) chops…"
        let name = baseName
        let names = plan.map { "\(padName($0.pad)) \(name).wav" }
        Task {
            let result: Result<URL, Error> = await Task.detached(priority: .userInitiated) {
                Result {
                    var folder = destination.appendingPathComponent("\(name) Chops")
                    var n = 2
                    while FileManager.default.fileExists(atPath: folder.path) {
                        folder = destination.appendingPathComponent("\(name) Chops \(n)")
                        n += 1
                    }
                    try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
                    for (item, fileName) in zip(plan, names) {
                        guard let segment = ChopAudio.segment(of: buffer, from: item.slot.start, to: item.slot.end, fade: true) else { continue }
                        try ChopAudio.writeWAV(segment, to: folder.appendingPathComponent(fileName))
                    }
                    return folder
                }
            }.value
            isBusy = false
            switch result {
            case .success(let folder):
                status = "Exported \(plan.count) chops"
                if preset == nil { NSWorkspace.shared.activateFileViewerSelecting([folder]) }
            case .failure(let error):
                status = "Export failed: \(error.localizedDescription)"
            }
        }
    }

    // MARK: - MPC project

    private static var templateURL: URL {
        let dir = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Crate", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir.appendingPathComponent("MPC Template.xpj")
    }

    var hasMPCTemplate: Bool { FileManager.default.fileExists(atPath: Self.templateURL.path) }

    /// Asks for an empty project saved on the MPC Sample and keeps a copy to build every export from.
    @discardableResult
    func chooseMPCTemplate(from preset: URL? = nil) -> Bool {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowedContentTypes = [UTType(filenameExtension: "xpj") ?? .data]
        panel.prompt = "Use as Template"
        panel.message = "Choose an empty project (.xpj) saved from your MPC Sample. DSS1 fills its pads with your chops."
        guard let url = preset ?? (panel.runModal() == .OK ? panel.url : nil) else { return false }
        do {
            let project = try XPJProject(contentsOf: url)
            guard project.padCount > 0 else { throw XPJError.missing("pads") }
            try? FileManager.default.removeItem(at: Self.templateURL)
            try FileManager.default.copyItem(at: url, to: Self.templateURL)
            status = "MPC template ready (\(project.padCount) pads)"
            return true
        } catch {
            status = error.localizedDescription
            return false
        }
    }

    /// Pads 1–8 of a bank are Serato's eight hot cues.
    static func isSeratoCue(_ index: Int) -> Bool { index % 16 < 8 }

    /// Writes a copy of the song into "<crate>/Serato" with pads 1–8 of this bank as hot cues 1–8 and the loop
    /// memories as saved loops, ready to add to Serato DJ.
    func exportSerato() {
        guard frameCount > 0, !isBusy else { return }
        let rate = sampleRate
        func ms(_ frame: Int) -> UInt32 { UInt32(max(0, (Double(frame) / rate * 1000).rounded())) }
        let cues: [SeratoExport.Cue] = (0..<8).compactMap { p in
            let index = bank * 16 + p
            guard let slot = pads[index] else { return nil }
            return SeratoExport.Cue(index: p, ms: ms(slot.start), name: padName(index))
        }
        let loops: [SeratoExport.Loop] = loopMemories.enumerated().compactMap { i, memory in
            guard let memory else { return nil }
            return SeratoExport.Loop(index: i, startMs: ms(memory.start), endMs: ms(memory.start + memory.length), name: barLabel(memory.bars))
        }
        guard !cues.isEmpty || !loops.isEmpty else {
            status = "Set pads 1–8 (the red ones) or save a loop memory first"
            return
        }
        let folder = (AppModel.shared.destination ?? track.url.deletingLastPathComponent())
            .appendingPathComponent("Serato", isDirectory: true)
        let source = track.url
        isBusy = true
        status = "Writing Serato cues…"
        Task {
            let result = await Task.detached(priority: .userInitiated) {
                Result { try SeratoExport.export(source, to: folder, cues: cues, loops: loops) }
            }.value
            isBusy = false
            switch result {
            case .success(let url):
                status = "Saved for Serato: \(cues.count) cue\(cues.count == 1 ? "" : "s"), \(loops.count) loop\(loops.count == 1 ? "" : "s") · add it to Serato from the Serato folder"
                lastSeratoExport = url
                #if TESTHOOKS
                if ProcessInfo.processInfo.environment["CRATE_SERATO"] != nil { return }
                #endif
                NSWorkspace.shared.activateFileViewerSelecting([url])
            case .failure(let error):
                status = "Serato export failed: \(error.localizedDescription)"
            }
        }
    }

    /// Where the last Serato export went (for tests).
    private(set) var lastSeratoExport: URL?

    /// Writes "<title>.xpj" and "<title>_[ProjectData]/" with each chop on its pad.
    func exportMPCProject(to preset: URL? = nil) {
        guard let buffer, !isBusy else { return }
        let plan = exportPlan
        guard !plan.isEmpty else {
            status = "Chop the track first"
            return
        }
        if !hasMPCTemplate && !chooseMPCTemplate() { return }

        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.prompt = "Export Here"
        panel.message = "Where should the MPC project go?"
        if let last = UserDefaults.standard.string(forKey: "mpcDestination") {
            panel.directoryURL = URL(fileURLWithPath: last)
        }
        guard let destination = preset ?? (panel.runModal() == .OK ? panel.url : nil) else { return }
        UserDefaults.standard.set(destination.path, forKey: "mpcDestination")

        isBusy = true
        status = "Building MPC project…"
        let name = baseName
        let names = plan.map { "\(padName($0.pad)) \(name).wav" }
        let template = Self.templateURL
        Task {
            let result: Result<URL, Error> = await Task.detached(priority: .userInitiated) {
                Result {
                    var project = try XPJProject(contentsOf: template)
                    if let last = plan.last, last.pad >= project.padCount { throw XPJError.tooFewPads(project.padCount) }
                    var stem = name
                    var n = 2
                    let fm = FileManager.default
                    while fm.fileExists(atPath: destination.appendingPathComponent("\(stem).xpj").path)
                            || fm.fileExists(atPath: destination.appendingPathComponent("\(stem)_[ProjectData]").path) {
                        stem = "\(name) \(n)"
                        n += 1
                    }
                    let dataDir = destination.appendingPathComponent("\(stem)_[ProjectData]", isDirectory: true)
                    try fm.createDirectory(at: dataDir, withIntermediateDirectories: true)
                    do {
                        for (item, fileName) in zip(plan, names) {
                            guard let segment = ChopAudio.segment(of: buffer, from: item.slot.start, to: item.slot.end, fade: true) else { continue }
                            try ChopAudio.writeWAV(segment, to: dataDir.appendingPathComponent(fileName))
                            try project.place(fileName: fileName, frames: Int(segment.frameLength), onPad: item.pad)
                        }
                        let xpj = destination.appendingPathComponent("\(stem).xpj")
                        try project.write(to: xpj)
                        return xpj
                    } catch {
                        try? fm.removeItem(at: dataDir)
                        throw error
                    }
                }
            }.value
            isBusy = false
            switch result {
            case .success(let xpj):
                status = "MPC project saved with \(plan.count) pads"
                if preset == nil { NSWorkspace.shared.activateFileViewerSelecting([xpj]) }
            case .failure(let error):
                status = "MPC export failed: \(error.localizedDescription)"
            }
        }
    }

    // MARK: - Saving

    private struct Saved: Codable {
        var markers: [Int]
        var pads: [String: PadSlot]
        var loops: [String: LoopMemory]?
    }

    private var saveURL: URL {
        let digest = SHA256.hash(data: Data(track.url.path.utf8)).prefix(12).map { String(format: "%02x", $0) }.joined()
        let dir = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Crate/Chops", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir.appendingPathComponent("\(digest).json")
    }

    private func restore() {
        guard let data = try? Data(contentsOf: saveURL),
              let saved = try? JSONDecoder().decode(Saved.self, from: data) else { return }
        markers = saved.markers.filter { $0 > 0 && $0 < frameCount }
        pads = Dictionary(uniqueKeysWithValues: saved.pads.compactMap { key, slot in Int(key).map { ($0, slot) } })
        for (key, memory) in saved.loops ?? [:] {
            if let slot = Int(key), loopMemories.indices.contains(slot), memory.start < frameCount { loopMemories[slot] = memory }
        }
        if !markers.isEmpty { status = "Picked up where you left off" }
    }

    private func scheduleSave() {
        guard loaded else { return }
        saveTask?.cancel()
        let loops = Dictionary(uniqueKeysWithValues: loopMemories.enumerated().compactMap { pair in
            pair.element.map { (String(pair.offset), $0) }
        })
        let saved = Saved(markers: markers, pads: Dictionary(uniqueKeysWithValues: pads.map { (String($0.key), $0.value) }), loops: loops)
        let url = saveURL
        saveTask = Task {
            try? await Task.sleep(for: .milliseconds(500))
            guard !Task.isCancelled, let data = try? JSONEncoder().encode(saved) else { return }
            try? data.write(to: url)
        }
    }
}
