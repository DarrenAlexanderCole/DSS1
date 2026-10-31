import AVFoundation
import SwiftUI

/// The performance page's audio: the library's current track on its own tape head, through its own copy
/// of the effects pedal. Effects switched on are *armed*: they only sound while the XY pad is held
/// (or latched), and the pad's position drives them.
@MainActor
final class PerformanceDeck: ObservableObject {
    let sampler = Sampler()

    @Published private(set) var url: URL?
    @Published private(set) var isLoading = false
    @Published private(set) var isPlaying = false
    @Published private(set) var loadError: String?
    /// The FX knob the MIDI encoder is on: the last one clicked.
    @Published var selectedFX: FXKind?
    @Published var fx: [FXKind: FXState] {
        didSet {
            save()
            apply()
        }
    }
    /// Where the pad was last touched (0…1 each way, y up); nil when the pad is released.
    @Published private(set) var point: CGPoint?
    @Published private(set) var touching = false
    /// LATCH: letting go of the pad leaves the effects running where you left them.
    @Published var latch = UserDefaults.standard.bool(forKey: "performanceLatch") {
        didSet {
            UserDefaults.standard.set(latch, forKey: "performanceLatch")
            startEnvelope()
        }
    }
    /// How long the effects take to come in once the pad is touched, and to fade after it's let go (seconds).
    @Published var attack: Double = UserDefaults.standard.object(forKey: "performanceAttack") as? Double ?? 0.05 {
        didSet { UserDefaults.standard.set(attack, forKey: "performanceAttack") }
    }
    @Published var release: Double = UserDefaults.standard.object(forKey: "performanceRelease") as? Double ?? 0.3 {
        didSet { UserDefaults.standard.set(release, forKey: "performanceRelease") }
    }
    /// The effects' envelope, 0…1: follows the pad with `attack` and `release`.
    private(set) var wet = 0.0
    private var envelope: Task<Void, Never>?

    /// Level-match gain for the loaded track (same as the library player's).
    @Published var levelGain: Float = 1

    /// Called when the track runs out, so the crate can move on.
    var onFinished: (() -> Void)?

    private(set) var bpm: Double?
    private var frameCount = 0
    private var sampleRate = 44100.0
    private var cursor = 0
    private var watcher: Task<Void, Never>?

    init() {
        var saved: [FXKind: FXState] = [:]
        let stored = UserDefaults.standard.dictionary(forKey: "performanceFX") as? [String: [Double]] ?? [:]
        for kind in FXKind.allCases {
            if let v = stored[kind.rawValue], v.count == 2 {
                saved[kind] = FXState(on: v[0] > 0.5, depth: v[1])
            } else {
                // Out of the box: the filter is armed, everything at full reach.
                saved[kind] = FXState(on: kind == .lpf, depth: 1)
            }
        }
        fx = saved
    }

    /// The pad is doing something: held, or latched after a touch.
    var engaged: Bool { point != nil && (touching || latch) }
    var armed: [FXKind] { FXKind.allCases.filter { fx[$0]?.on == true } }

    var seconds: Double { Double(sampler.position ?? cursor) / sampleRate }
    var duration: Double { Double(frameCount) / sampleRate }

    // MARK: - Track

    func load(_ url: URL, bpm: Double?, from time: Double, play: Bool) async {
        stop()
        self.url = url
        self.bpm = bpm
        isLoading = true
        loadError = nil
        let buffer: AVAudioPCMBuffer? = await Task.detached(priority: .userInitiated) {
            DecodedCache.shared.decode(url)
        }.value
        // Another track may have been asked for while this one was decoding.
        guard self.url == url else { return }
        isLoading = false
        guard let buffer else {
            loadError = "Couldn't open this track"
            return
        }
        frameCount = Int(buffer.frameLength)
        sampleRate = buffer.format.sampleRate
        cursor = min(Int(time * sampleRate), max(0, frameCount - 1))
        do {
            try sampler.load(url, buffer: buffer)
        } catch {
            loadError = "Couldn't play this track"
            return
        }
        apply()
        if play { start() }
    }

    func togglePlay() {
        if isPlaying {
            stop()
        } else {
            if cursor >= frameCount - 1 { cursor = 0 }
            start()
        }
    }

    func stop() {
        watcher?.cancel()
        if isPlaying { cursor = sampler.pause() ?? cursor }
        isPlaying = false
    }

    func seek(to fraction: Double) {
        let frame = Int(Double(frameCount) * min(max(fraction, 0), 0.999))
        cursor = frame
        if isPlaying { start() }
    }

    private func start() {
        guard frameCount > 0 else { return }
        sampler.play(from: cursor)
        isPlaying = true
        watcher?.cancel()
        watcher = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(for: .milliseconds(100))
                guard let self, self.isPlaying else { return }
                if self.sampler.reachedEdge {
                    self.cursor = self.sampler.pause() ?? self.cursor
                    self.isPlaying = false
                    self.onFinished?()
                    return
                }
            }
        }
    }

    // MARK: - Pad

    func padDown(_ p: CGPoint) {
        touching = true
        point = p
        startEnvelope()
    }

    func padMove(_ p: CGPoint) {
        point = p
        apply()
    }

    func padUp() {
        touching = false
        startEnvelope()
    }

    /// Glides `wet` toward 1 (pad engaged) or 0 (released), then lets the pad point go once faded out.
    private func startEnvelope() {
        envelope?.cancel()
        envelope = Task { [weak self] in
            var last = Date()
            while !Task.isCancelled {
                guard let self else { return }
                let target = self.engaged ? 1.0 : 0.0
                let now = Date()
                let dt = now.timeIntervalSince(last)
                last = now
                let seconds = target > self.wet ? self.attack : self.release
                self.wet = seconds < 0.005 ? target : (target > self.wet ? min(target, self.wet + dt / seconds) : max(target, self.wet - dt / seconds))
                self.apply()
                if self.wet == target {
                    if target == 0, !self.touching { self.point = nil }
                    return
                }
                try? await Task.sleep(for: .milliseconds(16))
            }
        }
    }

    /// Armed effects sound only while the pad is engaged, and follow it.
    private func apply() {
        let live = wet > 0.001 && point != nil
        var effective = fx
        for kind in FXKind.allCases { effective[kind]?.on = live && fx[kind]?.on == true }
        sampler.apply(effective, bpm: bpm, sampleRate: sampleRate, xy: live ? point : nil, wet: wet)
    }

    private func save() {
        var out: [String: [Double]] = [:]
        for (kind, s) in fx { out[kind.rawValue] = [s.on ? 1 : 0, s.depth] }
        UserDefaults.standard.set(out, forKey: "performanceFX")
    }
}

extension FXKind {
    /// Short names for the pad's LED display.
    var short: String {
        switch self {
        case .delay: return "DLY"
        case .echo: return "ECH"
        case .lofi: return "LoFi"
        case .lpf: return "LPF"
        case .hpf: return "HPF"
        case .reverb: return "REV"
        case .spaceEcho: return "TAPE"
        case .stutter: return "STUT"
        }
    }

    /// What the pad's two axes do to this effect.
    var padAxes: (x: String, y: String) {
        switch self {
        case .delay: return ("TIME", "FEEDBACK")
        case .echo: return ("TIME", "FEEDBACK")
        case .lofi: return ("RATE", "BITS")
        case .lpf: return ("CUTOFF", "RESONANCE")
        case .hpf: return ("CUTOFF", "REACH")
        case .reverb: return ("SIZE", "MIX")
        case .spaceEcho: return ("TAPE SPEED", "FEEDBACK")
        case .stutter: return ("SIZE", "—")
        }
    }
}

// MARK: - Page

/// The performance page, after a Kaoss Pad: arm effects on the left, play them on the pad.
struct PerformanceView: View {
    @EnvironmentObject private var model: AppModel
    @EnvironmentObject private var player: AudioPlayer
    @ObservedObject var deck: PerformanceDeck

    var body: some View {
        VStack(spacing: 0) {
            topBar
                .padding(.horizontal, 26)
                .padding(.top, 40)
            Spacer(minLength: 14)
            PerformanceUnit(deck: deck)
                .padding(.horizontal, 26)
            Spacer(minLength: 22)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(ChopBackground())
        .onChange(of: player.volume, initial: true) { _, v in deck.sampler.volume = Float(v * v) * deck.levelGain }
        .onChange(of: deck.levelGain) { _, g in deck.sampler.volume = Float(player.volume * player.volume) * g }
    }

    private var topBar: some View {
        HStack(spacing: 14) {
            BackButton(label: model.performReturn.backLabel, help: "Back; the track carries on from here") { model.closePerformance() }
            Spacer()
            MIDIChip()
        }
    }
}

/// The hardware: a light-grey chassis with the effects module, the pad, and the latch.
private struct PerformanceUnit: View {
    @ObservedObject var deck: PerformanceDeck

    private static let shell = Color(hex: 0xE4E4E0)
    private static let shellLine = Color(hex: 0xC4C4BE)

    var body: some View {
        GeometryReader { geo in
            let padSide = max(260, min(geo.size.height - 170, geo.size.width - 560, 560))
            VStack(spacing: 14) {
                header
                HStack(alignment: .top, spacing: 22) {
                    FXModule(deck: deck)
                        .frame(width: 230)
                    XYPad(deck: deck)
                        .frame(width: padSide, height: padSide)
                    sideControls
                        .frame(width: 190)
                }
                TransportStrip(deck: deck)
            }
            .padding(.horizontal, 24)
            .padding(.vertical, 18)
            .background(
                RoundedRectangle(cornerRadius: 22, style: .continuous)
                    .fill(LinearGradient(colors: [Color(hex: 0xEDEDEA), Self.shell, Color(hex: 0xD6D6D1)], startPoint: .top, endPoint: .bottom))
                    .overlay(GrainOverlay(image: Grain.dust).clipShape(RoundedRectangle(cornerRadius: 22, style: .continuous)).opacity(0.5))
                    .shadow(color: .black.opacity(0.55), radius: 22, y: 12)
            )
            .overlay(RoundedRectangle(cornerRadius: 22, style: .continuous).strokeBorder(Color.white.opacity(0.7), lineWidth: 1))
            .fixedSize()
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }

    private var header: some View {
        HStack(alignment: .center, spacing: 18) {
            HStack(alignment: .firstTextBaseline, spacing: 10) {
                Text("DSS1")
                    .font(.system(size: 26, weight: .black))
                    .tracking(1)
                Text("PERFORMANCE PAD")
                    .font(.system(size: 13, weight: .semibold))
                    .tracking(3)
                    .opacity(0.7)
            }
            .foregroundStyle(Palette.ink)
            Spacer()
            LEDDisplay(deck: deck)
        }
    }

    private var sideControls: some View {
        VStack(alignment: .leading, spacing: 16) {
            LatchButton(on: $deck.latch)
            HStack(spacing: 10) {
                PadKnob(label: "attack", value: $deck.attack, range: 0...2, reset: 0.05)
                PadKnob(label: "release", value: $deck.release, range: 0...3, reset: 0.3)
            }
            .padding(.leading, 4)
            VStack(alignment: .leading, spacing: 8) {
                Text("PAD AXES")
                    .font(.system(size: 9, weight: .black))
                    .tracking(1.5)
                    .opacity(0.55)
                if deck.armed.isEmpty {
                    Text("Arm an effect on the left, then touch the pad.")
                        .font(.system(size: 11))
                        .opacity(0.6)
                        .fixedSize(horizontal: false, vertical: true)
                } else {
                    ForEach(deck.armed) { kind in
                        HStack(spacing: 6) {
                            RoundedRectangle(cornerRadius: 1.5).fill(kind.color).frame(width: 7, height: 7)
                            Text(kind.short)
                                .font(.system(size: 10, weight: .black))
                                .frame(width: 34, alignment: .leading)
                            Text("X \(kind.padAxes.x) · Y \(kind.padAxes.y)")
                                .font(.system(size: 9, weight: .semibold))
                                .opacity(0.6)
                                .lineLimit(1)
                                .minimumScaleFactor(0.7)
                        }
                    }
                }
            }
            .foregroundStyle(Palette.ink)
            Spacer(minLength: 0)
        }
    }
}

// MARK: - Effects module

/// A copy of the chop station's effect knobs. Tap to arm for the pad; drag for how far the pad can push it.
private struct FXModule: View {
    @ObservedObject var deck: PerformanceDeck

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("FX ASSIGN")
                    .font(.system(size: 11, weight: .black))
                    .tracking(1.5)
                Spacer()
                Button {
                    withAnimation(Motion.bouncy) {
                        for kind in FXKind.allCases { deck.fx[kind]?.on = false }
                    }
                } label: {
                    Text("CLEAR")
                        .font(.system(size: 8.5, weight: .black))
                        .tracking(1)
                        .padding(.horizontal, 8)
                        .padding(.vertical, 4)
                        .background(Capsule().fill(Color.black.opacity(0.07)))
                }
                .buttonStyle(BouncyButtonStyle(pressedScale: 0.9))
                .help("Disarm every effect")
            }
            .foregroundStyle(Palette.ink)
            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 6), count: 2), spacing: 10) {
                ForEach(FXKind.allCases) { kind in
                    FXKnob(kind: kind, state: binding(kind), offCaption: "OFF", selected: deck.selectedFX == kind) { deck.selectedFX = kind }
                }
            }
            Text("tap to arm · drag for reach")
                .font(.system(size: 8, weight: .medium))
                .foregroundStyle(Palette.ink.opacity(0.55))
                .frame(maxWidth: .infinity)
        }
        .padding(14)
        .background(
            RoundedRectangle(cornerRadius: 14, style: .continuous)
                .fill(Color(hex: 0xECE8DF))
                .shadow(color: .black.opacity(0.2), radius: 4, y: 2)
        )
        .overlay(RoundedRectangle(cornerRadius: 14, style: .continuous).strokeBorder(Color(hex: 0xCFC9BC), lineWidth: 1))
    }

    private func binding(_ kind: FXKind) -> Binding<FXState> {
        Binding(get: { deck.fx[kind] ?? FXState() }, set: { deck.fx[kind] = $0 })
    }
}

// MARK: - XY pad

/// The touch pad: a grid of LEDs that light up under the cursor and leave a fading trail.
/// Click and drag to play; X runs left → right, Y bottom → top.
private struct XYPad: View {
    @ObservedObject var deck: PerformanceDeck
    @State private var trail: [(point: CGPoint, time: Date)] = []

    private static let cells = 12
    private static let glow = Color(hex: 0x3FE0D0)
    private static let deep = Color(hex: 0x2B7BFF)

    var body: some View {
        GeometryReader { geo in
            TimelineView(.animation) { context in
                let now = context.date
                let point = deck.point
                let engaged = deck.engaged
                let trail = self.trail
                Canvas { ctx, size in
                    draw(&ctx, size: size, now: now, point: point, engaged: engaged, trail: trail)
                }
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        let p = Self.normalized(g.location, in: geo.size)
                        if deck.touching { deck.padMove(p) } else { deck.padDown(p) }
                        trail.append((p, Date()))
                        if trail.count > 40 { trail.removeFirst(trail.count - 40) }
                    }
                    .onEnded { _ in deck.padUp() }
            )
        }
        .padding(14)
        .background(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .fill(Color(hex: 0x0B1418))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .strokeBorder(LinearGradient(colors: [.black.opacity(0.8), .white.opacity(0.35)], startPoint: .top, endPoint: .bottom), lineWidth: 2)
        )
        .padding(8)
        .background(
            RoundedRectangle(cornerRadius: 14, style: .continuous)
                .fill(LinearGradient(colors: [Color(hex: 0xBDBDB7), Color(hex: 0xF4F4F1)], startPoint: .top, endPoint: .bottom))
        )
        .help("Click and drag to play the armed effects. LATCH keeps them going when you let go. Right-click to MIDI Learn an axis.")
        .contextMenu {
            MIDILearnItem(target: .padX)
            MIDILearnItem(target: .padY)
        }
    }

    /// A point on the LEDs as 0…1 each way, y up.
    private static func normalized(_ location: CGPoint, in size: CGSize) -> CGPoint {
        let x = location.x / max(size.width, 1)
        let y = 1 - location.y / max(size.height, 1)
        return CGPoint(x: min(max(x, 0), 1), y: min(max(y, 0), 1))
    }

    private func draw(_ ctx: inout GraphicsContext, size: CGSize, now: Date, point: CGPoint?, engaged: Bool,
                      trail: [(point: CGPoint, time: Date)]) {
        let n = Self.cells
        let gap: CGFloat = 5
        let cell = (size.width - gap * CGFloat(n - 1)) / CGFloat(n)
        let t = now.timeIntervalSinceReferenceDate

        for row in 0..<n {
            for col in 0..<n {
                let cx = (CGFloat(col) + 0.5) / CGFloat(n)
                let cy = 1 - (CGFloat(row) + 0.5) / CGFloat(n)
                // Idle: a slow, dim ripple so the pad looks alive.
                var level = 0.10 + 0.06 * sin(t * 1.3 + Double(col) * 0.55 + Double(row) * 0.35)
                // The finger and its fading trail.
                for (p, time) in trail {
                    let age = now.timeIntervalSince(time)
                    guard age < 0.9 else { continue }
                    let d2 = Double((p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy))
                    level = max(level, (1 - age / 0.9) * 0.75 * exp(-d2 / 0.006))
                }
                if engaged, let p = point {
                    let d2 = Double((p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy))
                    let pulse = 0.9 + 0.1 * sin(t * 8)
                    level = max(level, pulse * exp(-d2 / 0.012))
                    // Light the row and column through the point, like crosshairs.
                    if abs(p.x - cx) < 0.5 / CGFloat(n) || abs(p.y - cy) < 0.5 / CGFloat(n) { level = max(level, 0.32) }
                }
                let rect = CGRect(x: CGFloat(col) * (cell + gap), y: CGFloat(row) * (cell + gap), width: cell, height: cell)
                let shape = Path(roundedRect: rect, cornerRadius: cell * 0.22)
                let mix = min(1, level)
                // Deep blue at rest, through teal, to near-white at full.
                let color = Self.deep.mix(Self.glow, by: min(1, mix * 1.4)).mix(.white, by: max(0, mix - 0.75) * 1.6)
                ctx.fill(shape, with: .color(color.opacity(0.18 + 0.82 * mix)))
                if mix > 0.4 {
                    var glow = ctx
                    glow.addFilter(.blur(radius: cell * 0.35))
                    glow.fill(shape, with: .color(Self.glow.opacity((mix - 0.4) * 0.9)))
                }
            }
        }
    }
}

private extension Color {
    func mix(_ other: Color, by amount: Double) -> Color {
        let a = NSColor(self).usingColorSpace(.sRGB) ?? .black
        let b = NSColor(other).usingColorSpace(.sRGB) ?? .black
        let k = CGFloat(min(max(amount, 0), 1))
        return Color(red: a.redComponent + (b.redComponent - a.redComponent) * k,
                     green: a.greenComponent + (b.greenComponent - a.greenComponent) * k,
                     blue: a.blueComponent + (b.blueComponent - a.blueComponent) * k)
    }
}

// MARK: - Display & latch

/// A red LED readout, like the KP's: the armed effects at rest, X/Y while you play.
private struct LEDDisplay: View {
    @ObservedObject var deck: PerformanceDeck

    var body: some View {
        HStack(spacing: 14) {
            Text(mainText)
                .font(.system(size: 22, weight: .heavy, design: .monospaced))
                .foregroundStyle(Color(hex: 0xFF3B30))
                .shadow(color: Color(hex: 0xFF3B30).opacity(0.8), radius: 4)
                .lineLimit(1)
                .frame(minWidth: 150, alignment: .leading)
            VStack(alignment: .trailing, spacing: 2) {
                Text(deck.bpm.map { String(format: "%.0f BPM", $0) } ?? "--- BPM")
                Text(deck.engaged ? "PLAY" : (deck.latch ? "LATCH" : "READY"))
            }
            .font(.system(size: 9, weight: .bold, design: .monospaced))
            .foregroundStyle(Color(hex: 0xFF3B30).opacity(0.75))
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 8)
        .background(RoundedRectangle(cornerRadius: 6).fill(Color(hex: 0x140606)))
        .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(Color.black.opacity(0.6), lineWidth: 2))
    }

    private var mainText: String {
        if let p = deck.point, deck.engaged {
            return String(format: "X%03d Y%03d", Int(p.x * 127), Int(p.y * 127))
        }
        let names = deck.armed.map(\.short)
        if names.isEmpty { return "no FX" }
        return names.count > 2 ? "\(names[0])+\(names.count - 1)" : names.joined(separator: "+")
    }
}

/// A small dark knob for seconds: drag up/down, double-click to reset.
private struct PadKnob: View {
    let label: String
    @Binding var value: Double
    let range: ClosedRange<Double>
    let reset: Double
    @State private var dragStart: Double?

    private var fraction: Double { (value - range.lowerBound) / (range.upperBound - range.lowerBound) }

    var body: some View {
        VStack(spacing: 4) {
            ZStack {
                Circle().fill(Color(hex: 0x1F1F1F))
                Circle().strokeBorder(Color.white.opacity(0.14), lineWidth: 1)
                Capsule().fill(Palette.cream).frame(width: 2.5, height: 9).offset(y: -9)
                    .rotationEffect(.degrees(-135 + 270 * fraction))
            }
            .frame(width: 36, height: 36)
            .padding(3)
            .background(Circle().fill(Color(hex: 0xB9B9B3)))
            .shadow(color: .black.opacity(0.3), radius: 2, y: 2)
            .contentShape(Circle())
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { g in
                    if dragStart == nil { dragStart = fraction }
                    let f = min(max((dragStart ?? 0) - g.translation.height / 160, 0), 1)
                    value = range.lowerBound + (range.upperBound - range.lowerBound) * f
                }
                .onEnded { _ in dragStart = nil })
            .simultaneousGesture(TapGesture(count: 2).onEnded { withAnimation(Motion.snappy) { value = reset } })
            .help("\(label.capitalized): how long the effects take to \(label == "attack" ? "come in" : "fade out"). Drag up/down; double-click to reset.")
            Text(dragStart == nil ? label.uppercased() : String(format: "%.2fs", value))
                .font(.system(size: 8, weight: .black).monospacedDigit())
                .tracking(1)
                .foregroundStyle(Palette.ink.opacity(0.7))
                .fixedSize()
        }
        .frame(width: 60)
    }
}

/// A big square key with an LED, like the KP's HOLD.
private struct LatchButton: View {
    @Binding var on: Bool

    var body: some View {
        Button { withAnimation(Motion.snappy) { on.toggle() } } label: {
            VStack(spacing: 6) {
                RoundedRectangle(cornerRadius: 1.5)
                    .fill(on ? Color(hex: 0xFF3B30) : Palette.ink.opacity(0.2))
                    .frame(width: 22, height: 5)
                    .shadow(color: on ? Color(hex: 0xFF3B30) : .clear, radius: 4)
                Text("LATCH")
                    .font(.system(size: 13, weight: .black))
                    .tracking(2)
                    .foregroundStyle(Palette.cream)
            }
            .frame(width: 120, height: 64)
            .background(RoundedRectangle(cornerRadius: 8).fill(Color(hex: 0x1F1F1F)))
            .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(Color.white.opacity(0.12), lineWidth: 1))
            .padding(5)
            .background(RoundedRectangle(cornerRadius: 11).fill(Color(hex: 0xB9B9B3)))
            .shadow(color: .black.opacity(0.3), radius: 2, y: 2)
        }
        .buttonStyle(BouncyButtonStyle(pressedScale: 0.94))
        .help("Latch: when you let go of the pad, the effects keep going where you left them. Turn off to release.")
        .contextMenu { MIDILearnItem(target: .latch) }
    }
}

// MARK: - Transport

private struct TransportStrip: View {
    @EnvironmentObject private var model: AppModel
    @EnvironmentObject private var player: AudioPlayer
    @ObservedObject var deck: PerformanceDeck

    var body: some View {
        HStack(spacing: 16) {
            HStack(spacing: 8) {
                key("backward.end.fill") { model.previousTrack() }
                    .help("Previous track (⌘←)")
                Button { deck.togglePlay() } label: {
                    Image(systemName: deck.isPlaying ? "pause.fill" : "play.fill")
                        .font(.system(size: 15, weight: .black))
                        .foregroundStyle(Palette.cream)
                        .frame(width: 58, height: 36)
                        .background(RoundedRectangle(cornerRadius: 8).fill(deck.isPlaying ? Palette.red : Color(hex: 0x1F1F1F)))
                }
                .buttonStyle(BouncyButtonStyle(pressedScale: 0.92))
                .help("Play / pause (space)")
                key("forward.end.fill") { model.nextTrack() }
                    .help("Next track (⌘→)")
            }

            VStack(alignment: .leading, spacing: 3) {
                let info = deck.url.map(TrackInfo.init(url:))
                Text(deck.loadError ?? (deck.isLoading ? "Loading…" : info?.title ?? "Nothing cued: pick a track in the crate"))
                    .font(.system(size: 14, weight: .black))
                    .lineLimit(1)
                Text(info?.artist ?? "")
                    .font(.system(size: 10, weight: .semibold))
                    .opacity(0.6)
                    .lineLimit(1)
            }
            .foregroundStyle(Palette.ink)
            .frame(width: 260, alignment: .leading)

            TimelineView(.periodic(from: .now, by: 0.1)) { _ in
                let now = deck.seconds, total = deck.duration
                HStack(spacing: 10) {
                    Text(clock(now)).frame(width: 40, alignment: .trailing)
                    GeometryReader { geo in
                        ZStack(alignment: .leading) {
                            Capsule().fill(Color.black.opacity(0.12))
                            Capsule().fill(Palette.ink)
                                .frame(width: total > 0 ? geo.size.width * now / total : 0)
                        }
                        .frame(height: 5)
                        .frame(maxHeight: .infinity)
                        .contentShape(Rectangle())
                        .gesture(DragGesture(minimumDistance: 0).onEnded { g in
                            deck.seek(to: g.location.x / max(geo.size.width, 1))
                        })
                    }
                    Text(clock(total)).frame(width: 40, alignment: .leading)
                }
                .font(.system(size: 10, weight: .bold).monospacedDigit())
                .foregroundStyle(Palette.ink.opacity(0.7))
            }
            .frame(height: 20)
        }
        .padding(.top, 2)
    }

    private func key(_ symbol: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(size: 12, weight: .black))
                .foregroundStyle(Palette.ink)
                .frame(width: 42, height: 36)
                .background(RoundedRectangle(cornerRadius: 8).fill(Color.white))
                .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(Palette.ink.opacity(0.15), lineWidth: 1))
                .shadow(color: .black.opacity(0.15), radius: 0, y: 2)
        }
        .buttonStyle(BouncyButtonStyle(pressedScale: 0.9))
    }

    private func clock(_ s: Double) -> String {
        guard s.isFinite else { return "0:00" }
        let t = Int(s)
        return String(format: "%d:%02d", t / 60, t % 60)
    }
}
