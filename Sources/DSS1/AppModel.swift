import AppKit
import SwiftUI
import UniformTypeIdentifiers

@MainActor
final class AppModel: ObservableObject {
    static let shared = AppModel()
    nonisolated static let audioExtensions: Set<String> = ["mp3", "wav", "flac", "aif", "aiff", "m4a", "aac", "caf"]

    @Published var destination: URL? {
        didSet {
            UserDefaults.standard.set(destination?.path, forKey: "destinationPath")
            library.setFolder(destination)
        }
    }
    @Published private(set) var phase: Phase = .idle
    @Published private(set) var note: String?
    /// Files the last import couldn't read (damaged, or not really audio).
    @Published private(set) var damaged: [String] = []
    /// Import settings (DSS1 → Settings…).
    @Published var convertToWAV = UserDefaults.standard.object(forKey: "convertToWAV") as? Bool ?? true {
        didSet { UserDefaults.standard.set(convertToWAV, forKey: "convertToWAV") }
    }
    @Published var screen: Screen = .home
    @Published var isDropTargeted = false
    @Published private(set) var chop: ChopSession?
    /// Where the radio page's back / eject goes.
    @Published private(set) var radioReturn: Screen = .player

    let player = AudioPlayer()
    let library = Library()
    /// The performance page's deck (Kaoss-style XY effects).
    let performance = PerformanceDeck()
    /// Where the performance page's back button goes.
    private(set) var performReturn: Screen = .player

    private var queue: [URL] = []
    private var runID = 0
    private var batchTracks: [URL] = []
    private var batchImported = 0
    private var batchSkipped = 0
    private var batchDamaged: [String] = []

    private init() {
        performance.onFinished = { [weak self] in self?.nextTrack(continuing: true) }
        player.analysisFor = { [weak library] url in library?.result(for: url) }
        MIDIHub.shared.onEvent = { [weak self] event, learned in self?.handleMIDI(event, learned: learned) }
        destination = UserDefaults.standard.string(forKey: "destinationPath").map { URL(fileURLWithPath: $0) }
        library.setFolder(destination)
        Task { await restoreLastSession() }
    }

    // MARK: - Folders

    func pickDestination() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.prompt = "Use This Folder"
        panel.message = "Where should the audio land?"
        if panel.runModal() == .OK, let url = panel.url {
            withAnimation(Motion.bouncy) { destination = url }
        }
    }

    func pickSources() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = true
        panel.allowsMultipleSelection = true
        panel.allowedContentTypes = [.zip, .folder, .audio]
        panel.prompt = "Add to Crate"
        panel.message = "Pick audio files, folders or zips"
        if panel.runModal() == .OK { importItems(panel.urls) }
    }

    func openDestination() {
        guard let destination else { pickDestination(); return }
        NSWorkspace.shared.open(destination)
    }

    // MARK: - Screens & playback

    func showPlayer() {
        withAnimation(Motion.reveal) { screen = .player }
    }

    /// The radio page: a cassette deck showing whatever's playing.
    func showRadio() {
        guard screen != .radio else { return }
        radioReturn = screen == .home ? .player : screen
        withAnimation(Motion.reveal) { screen = .radio }
    }

    /// The radio's eject key: straight back to the Crate, wherever you came from.
    func ejectToCrate() {
        withAnimation(Motion.reveal) { screen = .player }
    }

    func closeRadio() {
        withAnimation(Motion.reveal) { screen = radioReturn }
    }

    /// The performance page: takes over the current track where the library player was,
    /// and hands it back (same spot, same play state) on the way out.
    func showPerformance() {
        guard screen != .perform else { return }
        performReturn = (screen == .home || screen == .radio) ? .player : screen
        chop?.stopSound()
        let wasPlaying = player.isPlaying
        if wasPlaying { player.toggle() }
        if let url = player.currentURL {
            let time = player.currentTime
            performance.levelGain = levelGain(for: url)
            Task { await performance.load(url, bpm: library.bpm(for: url), from: time, play: wasPlaying) }
        }
        withAnimation(Motion.reveal) { screen = .perform }
    }

    func closePerformance() {
        let wasPlaying = performance.isPlaying
        let time = performance.seconds, total = performance.duration
        performance.stop()
        if performance.url == player.currentURL, total > 0 {
            player.seek(to: time / total)
            if wasPlaying && !player.isPlaying { player.toggle() }
        }
        withAnimation(Motion.reveal) { screen = performReturn == .perform ? .player : performReturn }
    }

    /// ⌘→ and the next key: on the performance page the deck moves on (and the crate follows).
    func nextTrack(continuing: Bool = false) {
        guard screen == .perform else { player.next(); return }
        stepPerformance(by: 1, play: continuing || performance.isPlaying)
    }

    func previousTrack() {
        guard screen == .perform else { player.previous(); return }
        if performance.seconds > 3 { performance.seek(to: 0); return }
        stepPerformance(by: -1, play: performance.isPlaying)
    }

    private func stepPerformance(by delta: Int, play: Bool) {
        let i = player.index + delta
        guard player.tracks.indices.contains(i) else { return }
        player.play(queue: player.tracks, index: i, autoplay: false)
        let url = player.tracks[i]
        performance.levelGain = levelGain(for: url)
        Task { await performance.load(url, bpm: library.bpm(for: url), from: 0, play: play) }
    }

    /// The library player's level-match gain for a track, as a multiplier.
    private func levelGain(for url: URL) -> Float {
        guard player.levelMatch, let r = library.result(for: url), let db = LevelMatch.gainDB(r) else { return 1 }
        return LevelMatch.linear(db)
    }

    func showHome() {
        withAnimation(Motion.reveal) { screen = .home }
    }

    /// Opens a track in the chop station, pausing the library player.
    func openChop(_ track: LibraryTrack) {
        if player.isPlaying { player.toggle() }
        performance.stop()
        chop?.sampler.shutDown()
        let session = ChopSession(track: track, analysis: library.result(for: track.url))
        session.sampler.volume = Float(player.volume * player.volume)
        chop = session
        withAnimation(Motion.reveal) { screen = .chop }
    }

    func openChopForCurrent() {
        guard let url = player.currentURL, let track = library.tracks.first(where: { $0.url == url }) else { return }
        openChop(track)
    }

    func closeChop() {
        chop?.stopSound()
        withAnimation(Motion.reveal) { screen = .player }
    }

    /// Space bar: play / pause the chop station while chopping, otherwise the library player.
    func togglePlayback() {
        if screen == .chop, let chop { chop.togglePlay() }
        else if screen == .perform { performance.togglePlay() }
        else { player.toggle() }
    }

    /// Plays a track from the library, with the rest of the library queued after it.
    func play(_ track: LibraryTrack) {
        let urls = library.ordered.map(\.url)
        guard let i = urls.firstIndex(of: track.url) else { return }
        player.play(queue: urls, index: i)
    }

    /// Reopen cued on the track you were last listening to.
    private func restoreLastSession() async {
        await library.rescan()
        let urls = library.ordered.map(\.url)
        guard player.currentURL == nil, !urls.isEmpty else { return }
        let lastPath = UserDefaults.standard.string(forKey: "lastTrack")
        if let i = urls.firstIndex(where: { $0.path == lastPath }) {
            player.play(queue: urls, index: i, from: UserDefaults.standard.double(forKey: "lastTime"), autoplay: false)
        } else {
            player.play(queue: urls, index: 0, autoplay: false)
        }
    }

    // MARK: - MIDI

    /// The MPK joystick (or learned axes) on the performance pad: where it is, and whether it's off centre.
    private var joystick = CGPoint(x: 0.5, y: 0)

    /// A learned mapping wins; otherwise the Akai preset for whichever page is showing.
    /// Transport for whatever the current page is driving: the chop station on Chop (and on the Radio when it's
    /// showing the chop), the performance deck on Perform, otherwise the library player.
    func transport(_ t: Transport) {
        if screen == .perform {
            switch t {
            case .play: if !performance.isPlaying { performance.togglePlay() }
            case .pause: if performance.isPlaying { performance.togglePlay() }
            case .playPause: performance.togglePlay()
            case .stop:
                if performance.isPlaying { performance.togglePlay() }
                performance.seek(to: 0)
            case .next: nextTrack()
            case .previous: previousTrack()
            case .rewind, .forward, .loop, .retrigger: break
            }
            return
        }
        if let chop, screen == .chop || (screen == .radio && (chop.isPlaying || radioReturn == .chop)) {
            switch t {
            case .play: chop.run(direction: 1)
            case .pause: if chop.isPlaying { chop.togglePlay() }
            case .playPause: chop.togglePlay()
            case .stop: chop.stopSound(); chop.cursor = 0
            case .rewind: chop.run(direction: -1)
            case .forward: chop.run(direction: 1.5)
            case .loop: chop.toggleLoop()
            case .retrigger: chop.retriggerLoop()
            case .next: nextTrack()
            case .previous: previousTrack()
            }
            return
        }
        switch t {
        case .play: player.play1x()
        case .pause: if player.isPlaying { player.toggle() }
        case .playPause: player.toggle()
        case .stop:
            if player.isPlaying { player.toggle() }
            player.seek(to: 0)
        case .rewind: player.rewind()
        case .forward: player.forward()
        case .next: nextTrack()
        case .previous: previousTrack()
        case .loop, .retrigger: break
        }
    }

    /// Acts on a MIDI message; returns what it drove (for the MIDI monitor), or nil when nothing.
    @discardableResult
    func handleMIDI(_ event: MIDIEvent, learned: MIDITarget?) -> String? {
        guard let target = learned ?? presetTarget(for: event) else { return nil }
        let on: Bool
        let amount: Double
        switch event {
        case .noteOn(_, let v): on = true; amount = Double(v) / 127
        case .noteOff: on = false; amount = 0
        case .cc(_, let v): on = v > 63; amount = Double(v) / 127
        case .bend(let v): on = true; amount = Double(v) / 16383
        case .transport, .sysex: on = true; amount = 1
        case .program(let v), .aftertouch(let v), .polyAftertouch(_, let v): on = v > 63; amount = Double(v) / 127
        case .nrpn(_, let v, let st): on = (v ?? 0) > 63 || st != nil; amount = Double(v ?? 0) / 127
        }
        let isPress: Bool = {
            switch event {
            case .cc: return on
            case .noteOn, .transport, .sysex, .program: return true
            default: return false
            }
        }()

        switch target {
        case .pad(let i):
            guard isPress, screen == .chop, let chop else { return nil }
            chop.bank = min(7, i / 16)
            chop.hitPad(i)
        case .fxDepth(let kind):
            if screen == .perform {
                performance.fx[kind] = FXState(on: true, depth: amount)
            } else if let chop {
                chop.fx[kind] = FXState(on: true, depth: amount)
            }
        case .fxToggle(let kind):
            guard isPress else { return nil }
            if screen == .perform {
                performance.fx[kind]?.on.toggle()
            } else if let chop {
                chop.fx[kind]?.on.toggle()
            }
        case .latch:
            if isPress { performance.latch.toggle() }
        case .padX, .padY:
            guard screen == .perform else { return nil }
            if target == .padX { joystick.x = amount } else { joystick.y = amount }
            let away = abs(joystick.x - 0.5) > 0.03 || joystick.y > 0.03
            if away {
                if performance.touching { performance.padMove(joystick) } else { performance.padDown(joystick) }
            } else if performance.touching {
                performance.padUp()
            }
        case .playPause:
            if isPress { togglePlayback() }
        case .transport(let t):
            if isPress { transport(t) }
        case .encoder:
            guard let kind = selectedFXKind else { return nil }
            let current = fxState(kind).depth
            let absolute = MIDIHub.shared.encoderAbsolute
            /// A relative byte: 1…63 turn up, 65…127 turn down (two's complement).
            func relative(_ v: Int) -> Double { Double(v <= 63 ? v : v - 128) / 100 }
            let depth: Double
            switch event {
            case .cc(96, _): depth = current + 0.01        // data increment
            case .cc(97, _): depth = current - 0.01        // data decrement
            case .cc(_, let v): depth = absolute ? amount : current + relative(v)
            case .nrpn(_, _, let st?): depth = current + Double(st) / 100
            case .nrpn(_, let v?, nil): depth = absolute ? Double(v) / 127 : current + relative(v)
            case .bend, .program, .aftertouch, .polyAftertouch: depth = amount
            default: return nil
            }
            setFXDepth(kind, min(max(depth, 0), 1))
            return "\(kind.label) depth \(Int(min(max(depth, 0), 1) * 100))"
        case .encoderPress:
            guard isPress, let kind = selectedFXKind else { return nil }
            if screen == .perform { performance.fx[kind]?.on.toggle() } else { chop?.fx[kind]?.on.toggle() }
        case .pitch:
            guard screen == .chop || screen == .radio, let chop else { return nil }
            let range = Double(chop.pitchRange)
            var p = (amount - 0.5) * 2 * range
            if abs(p) < range * 0.03 { p = 0 }
            chop.pitch = min(max(p, -range), range)
        }
        return target.label
    }

    /// The FX knob the encoder is on, on the page showing the effects.
    private var selectedFXKind: FXKind? { screen == .perform ? performance.selectedFX : chop?.selectedFX }

    private func fxState(_ kind: FXKind) -> FXState {
        (screen == .perform ? performance.fx[kind] : chop?.fx[kind]) ?? FXState()
    }

    private func setFXDepth(_ kind: FXKind, _ depth: Double) {
        if screen == .perform { performance.fx[kind]?.depth = depth } else { chop?.fx[kind]?.depth = depth }
    }

    /// Akai MPD / MPK / MPC defaults:
    /// - **Chop:** notes 36–99 are pads A01–D16 (MPC layout, bank buttons included).
    /// - **Perform:** pads 1–8 (notes 36–43) arm the eight effects and pad 9 (note 44) is LATCH.
    /// - **Both:** knob CCs set FX depth, and the joystick (pitch bend / CC 1) plays the XY pad.
    private func presetTarget(for event: MIDIEvent) -> MIDITarget? {
        switch event {
        case .noteOn(let n, _), .noteOff(let n):
            if screen == .perform {
                if (36...43).contains(n) { return .fxToggle(FXKind.allCases[n - 36]) }
                if n == 44 { return .latch }
                return nil
            }
            return (36...99).contains(n) ? .pad(n - 36) : nil
        case .cc(let n, _):
            if n == 1 { return .padY }
            return MIDIHub.presetFX(forCC: n).map { .fxDepth($0) }
        case .bend:
            return .padX
        case .transport(let t):
            return .transport(t)
        case .program, .aftertouch, .polyAftertouch, .nrpn, .sysex:
            return nil
        }
    }

    // MARK: - Importing

    func importItems(_ urls: [URL]) {
        guard !urls.isEmpty else { return }
        if destination == nil {
            pickDestination()
            guard destination != nil else { return }
        }
        if screen == .player { showHome() }
        if queue.isEmpty && !phase.isBusy {
            damaged = []
            library.pauseWatching()
        }
        queue.append(contentsOf: urls)
        runNext()
    }

    private func runNext() {
        guard !phase.isBusy, !queue.isEmpty, let dest = destination else { return }
        let source = queue.removeFirst()
        runID += 1
        let id = runID
        let engine = ImportEngine(destination: dest, extensions: Self.audioExtensions, convertToWAV: convertToWAV)

        note = nil
        withAnimation(Motion.bouncy) { phase = .extracting(name: source.lastPathComponent, done: 0, total: 0) }

        Task.detached(priority: .userInitiated) {
            let result = Result {
                try engine.run(source: source) { step in
                    Task { @MainActor in AppModel.shared.apply(step, runID: id) }
                }
            }
            await AppModel.shared.finish(source: source, result: result, runID: id)
        }
    }

    private func apply(_ step: ImportPhase, runID id: Int) {
        guard id == runID, phase.isBusy else { return }
        withAnimation(Motion.snappy) {
            switch step {
            case let .extracting(name, done, total): phase = .extracting(name: name, done: done, total: total)
            case let .copying(done, total, name): phase = .copying(done: done, total: total, name: name)
            }
        }
    }

    private func finish(source: URL, result: Result<ImportOutcome, Error>, runID id: Int) {
        switch result {
        case .success(let outcome):
            batchTracks += outcome.tracks
            batchImported += outcome.imported
            batchSkipped += outcome.skipped
            batchDamaged += outcome.damaged
        case .failure(let error):
            note = error.localizedDescription
        }

        withAnimation(Motion.bouncy) { phase = .idle }
        guard queue.isEmpty else { runNext(); return }

        // Whole batch done: report, then open the player on the first new track.
        let first = batchTracks.min { $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending }
        let imported = batchImported
        let skipped = batchSkipped
        damaged = batchDamaged
        batchTracks = []
        batchImported = 0
        batchSkipped = 0
        batchDamaged = []

        guard let first else {
            Task { await library.resumeWatching() }
            if note == nil {
                note = damaged.isEmpty ? "No audio in \(source.lastPathComponent)"
                    : "\(damaged.count) file\(damaged.count == 1 ? "" : "s") couldn't be read"
            }
            return
        }
        withAnimation(Motion.jelly) { phase = .done(imported) }
        var parts: [String] = []
        if skipped > 0 { parts.append("\(skipped) already there") }
        if !damaged.isEmpty { parts.append("\(damaged.count) couldn't be read") }
        note = parts.isEmpty ? nil : parts.joined(separator: " · ")
        let hasDamaged = !damaged.isEmpty

        Task {
            await library.resumeWatching()
            let urls = library.ordered.map(\.url)
            let i = urls.firstIndex { $0.standardizedFileURL.path == first.standardizedFileURL.path } ?? 0
            player.play(queue: urls, index: i)
            try? await Task.sleep(for: .seconds(1.1))
            // Stay on the report when something couldn't be read, so you can see which.
            guard id == runID, case .done = phase, !hasDamaged else { return }
            showPlayer()
            try? await Task.sleep(for: .seconds(1))
            if id == runID, case .done = phase {
                phase = .idle
                note = nil
            }
        }
    }
}
