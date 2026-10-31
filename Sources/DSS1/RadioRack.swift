import AppKit
import SwiftUI

// The units racked under the radio's cassette deck: a 20-band graphic EQ with a spectrum
// analyzer, and a live recording deck that samples whatever the system is playing.

private extension View {
    func place(_ r: CGRect) -> some View {
        frame(width: r.width, height: r.height).position(x: r.midX, y: r.midY)
    }

    /// Silver lettering printed on black metal.
    func silkscreen(_ opacity: Double = 0.75) -> some View {
        foregroundStyle(Color(hex: 0xD9D9D4).opacity(opacity))
            .shadow(color: .black.opacity(0.8), radius: 0, x: 0, y: -0.5)
    }
}

/// Black anodised faceplate: the same scratched brushed metal as the deck, darkened.
struct BlackPlate: View {
    var corner: CGFloat = 9

    var body: some View {
        let shape = RoundedRectangle(cornerRadius: corner, style: .continuous)
        ZStack {
            shape.fill(Color.black.opacity(0.6)).blur(radius: 12).offset(y: 10)
            Image(decorative: Materials.aluminum, scale: 2).resizable().clipShape(shape)
            shape.fill(Color(hex: 0x131315).opacity(0.86))
            GrainOverlay(image: Grain.dust).opacity(0.3).clipShape(shape)
            shape.fill(LinearGradient(stops: [
                .init(color: .white.opacity(0.12), location: 0),
                .init(color: .clear, location: 0.25),
                .init(color: .black.opacity(0.3), location: 1),
            ], startPoint: .top, endPoint: .bottom))
            shape.strokeBorder(LinearGradient(colors: [.white.opacity(0.35), .white.opacity(0.06), .black.opacity(0.7)],
                                              startPoint: .top, endPoint: .bottom), lineWidth: 1.5)
        }
    }
}

private struct Corners: View {
    let size: CGSize

    var body: some View {
        ForEach([CGPoint(x: 18, y: 18), CGPoint(x: size.width - 18, y: 18),
                 CGPoint(x: 18, y: size.height - 18), CGPoint(x: size.width - 18, y: size.height - 18)], id: \.x) { p in
            Screw(angle: Double(p.x + p.y), size: 10).position(p)
        }
    }
}

// MARK: - Graphic EQ

struct EQUnit: View {
    static let size = CGSize(width: 1000, height: 250)
    private static let leftBank = CGRect(x: 40, y: 44, width: 300, height: 166)
    private static let display = CGRect(x: 360, y: 44, width: 280, height: 150)
    private static let rightBank = CGRect(x: 660, y: 44, width: 300, height: 166)

    @ObservedObject private var eq = MasterEQ.shared

    var body: some View {
        ZStack(alignment: .topLeading) {
            BlackPlate()
            Corners(size: Self.size)

            HStack(alignment: .firstTextBaseline, spacing: 10) {
                Text("DSS1").font(.system(size: 14, weight: .bold)).tracking(3)
                Text("STEREO GRAPHIC EQUALIZER  ·  GE-20").font(.system(size: 9, weight: .semibold)).tracking(1.5).opacity(0.75)
            }
            .silkscreen()
            .position(x: 210, y: 24)
            Text("±12 dB  ·  20 BAND").font(.system(size: 9, weight: .semibold)).tracking(1.5)
                .silkscreen(0.6)
                .position(x: Self.size.width - 120, y: 24)

            FaderBank(eq: eq, bands: 0..<10).place(Self.leftBank)
            SpectrumWindow(eq: eq).place(Self.display)
            FaderBank(eq: eq, bands: 10..<20).place(Self.rightBank)

            Text("20 BAND SPECTRUM ANALYZER").font(.system(size: 8, weight: .bold)).tracking(2)
                .foregroundStyle(Color(hex: 0xFFB02E).opacity(0.8))
                .position(x: Self.display.midX, y: Self.display.maxY + 13)

            HStack(spacing: 12) {
                RackSwitch(title: "EQ", lit: eq.enabled, lamp: Color(hex: 0xFFB02E)) {
                    withAnimation(Motion.snappy) { eq.enabled.toggle() }
                }
                .help("Switch the EQ in or out")
                RackSwitch(title: "FLAT", lit: false, lamp: nil) {
                    withAnimation(Motion.bouncy) { eq.flatten() }
                }
                .help("Set every band back to 0 dB")
            }
            .position(x: Self.display.midX, y: Self.size.height - 22)
        }
        .frame(width: Self.size.width, height: Self.size.height)
    }
}

/// Ten faders in a black well, with dB lines across like the real thing.
private struct FaderBank: View {
    @ObservedObject var eq: MasterEQ
    let bands: Range<Int>

    var body: some View {
        GeometryReader { geo in
            let colW = geo.size.width / CGFloat(bands.count)
            let top: CGFloat = 14, bottom = geo.size.height - 26
            ZStack(alignment: .topLeading) {
                RoundedRectangle(cornerRadius: 4)
                    .fill(Color.black.opacity(0.5))
                    .overlay(RoundedRectangle(cornerRadius: 4).strokeBorder(
                        LinearGradient(colors: [.black.opacity(0.8), .white.opacity(0.12)], startPoint: .top, endPoint: .bottom), lineWidth: 1))
                Canvas { ctx, size in
                    for db in [12, 6, 0, -6, -12] {
                        let y = top + CGFloat(12 - db) / 24 * (bottom - top)
                        ctx.fill(Path(CGRect(x: 6, y: y, width: size.width - 12, height: 0.6)),
                                 with: .color(Color.white.opacity(db == 0 ? 0.2 : 0.08)))
                    }
                }
                ForEach(bands, id: \.self) { band in
                    EQFader(eq: eq, band: band, top: top, bottom: bottom)
                        .frame(width: colW, height: geo.size.height)
                        .offset(x: CGFloat(band - bands.lowerBound) * colW)
                }
            }
        }
    }
}

/// One band: a slot, a steel cap with a red tip, the frequency printed underneath.
/// Drag to set; double-click for 0 dB.
private struct EQFader: View {
    @ObservedObject var eq: MasterEQ
    let band: Int
    let top: CGFloat
    let bottom: CGFloat
    @State private var dragStart: Float?

    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width, h = geo.size.height
            let gain = eq.gains[band]
            let y = top + CGFloat((EQBands.range - gain) / (2 * EQBands.range)) * (bottom - top)
            ZStack(alignment: .topLeading) {
                RoundedRectangle(cornerRadius: 1.5)
                    .fill(Color.black)
                    .overlay(RoundedRectangle(cornerRadius: 1.5).strokeBorder(Color.white.opacity(0.08), lineWidth: 0.5))
                    .frame(width: 4, height: bottom - top + 10)
                    .position(x: w / 2, y: (top + bottom) / 2)
                FaderCap()
                    .frame(width: min(20, w - 8), height: 14)
                    .position(x: w / 2, y: y)
                Text(EQBands.labels[band])
                    .font(.system(size: 7, weight: .bold))
                    .silkscreen(0.6)
                    .position(x: w / 2, y: h - 11)
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        if dragStart == nil { dragStart = gain }
                        var v = (dragStart ?? 0) - Float(g.translation.height / max(bottom - top, 1)) * 2 * EQBands.range
                        if abs(v) < 0.6 { v = 0 }
                        eq.setGain(band, v)
                    }
                    .onEnded { _ in dragStart = nil }
            )
            .simultaneousGesture(TapGesture(count: 2).onEnded { eq.setGain(band, 0) })
            .help("\(EQBands.labels[band]) Hz: \(String(format: "%+.1f", gain)) dB. Drag, or double-click for 0.")
        }
    }
}

private struct FaderCap: View {
    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 2)
                .fill(LinearGradient(colors: [Color(hex: 0xEDEDEA), Color(hex: 0xA8A8A3), Color(hex: 0x6A6A66)], startPoint: .top, endPoint: .bottom))
            VStack(spacing: 2) {
                Rectangle().fill(Color.black.opacity(0.3)).frame(height: 0.8)
                Rectangle().fill(Color(hex: 0xC8321F)).frame(height: 1.6)
                Rectangle().fill(Color.black.opacity(0.3)).frame(height: 0.8)
            }
            .padding(.horizontal, 2)
            RoundedRectangle(cornerRadius: 2).strokeBorder(Color.black.opacity(0.5), lineWidth: 0.6)
        }
        .shadow(color: .black.opacity(0.6), radius: 1.5, y: 1.5)
    }
}

/// The amber analyzer: live levels in each band as lit segments with peak holds,
/// with the EQ's own curve traced over it so you can see what the faders are doing.
private struct SpectrumWindow: View {
    @ObservedObject var eq: MasterEQ
    @EnvironmentObject private var model: AppModel
    @EnvironmentObject private var player: AudioPlayer
    @EnvironmentObject private var library: Library
    @State private var hold = PeakHold()

    private static let amber = Color(hex: 0xFFB02E)
    private static let segments = 14

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 4)
                .fill(LinearGradient(colors: [Color(hex: 0x1C110A), Color(hex: 0x0A0604)], startPoint: .top, endPoint: .bottom))
            TimelineView(.animation) { _ in
                let meter = NowPlaying.current(model: model, player: player, library: library).meter
                let levels = hold.update(meter?.spectrum() ?? Array(repeating: 0, count: EQBands.frequencies.count))
                Canvas { ctx, size in draw(ctx, size, levels: levels) }
            }
            // Smoked glass.
            LinearGradient(stops: [
                .init(color: .white.opacity(0.1), location: 0),
                .init(color: .clear, location: 0.4),
                .init(color: .white.opacity(0.04), location: 0.85),
            ], startPoint: .topLeading, endPoint: .bottomTrailing)
                .allowsHitTesting(false)
            RoundedRectangle(cornerRadius: 4)
                .strokeBorder(LinearGradient(colors: [.black, .white.opacity(0.25)], startPoint: .top, endPoint: .bottom), lineWidth: 2)
        }
        .clipShape(RoundedRectangle(cornerRadius: 4))
    }

    private func draw(_ ctx: GraphicsContext, _ size: CGSize, levels: (now: [Float], peak: [Float])) {
        let left: CGFloat = 26, right: CGFloat = 10, top: CGFloat = 12, bottom: CGFloat = 20
        let w = size.width - left - right, h = size.height - top - bottom
        let count = levels.now.count
        let colW = w / CGFloat(count)
        let segH = h / CGFloat(Self.segments)

        // dB scale for the curve.
        for (db, label) in [(12, "+12"), (0, "0"), (-12, "−12")] {
            let y = top + CGFloat(12 - db) / 24 * h
            ctx.draw(Text(label).font(.system(size: 6.5, weight: .bold).monospacedDigit()).foregroundColor(Self.amber.opacity(0.55)),
                     at: CGPoint(x: left - 12, y: y))
        }

        // Segments, with a soft glow behind the lit ones.
        var lit = Path(), dim = Path(), peaks = Path(), hot = Path()
        for i in 0..<count {
            let x = left + CGFloat(i) * colW + colW * 0.16
            let on = Int((Double(levels.now[i]) * Double(Self.segments)).rounded())
            let peak = Int((Double(levels.peak[i]) * Double(Self.segments)).rounded())
            for s in 0..<Self.segments {
                let rect = CGRect(x: x, y: top + h - CGFloat(s + 1) * segH + segH * 0.18, width: colW * 0.68, height: segH * 0.64)
                if s < on {
                    if s >= Self.segments - 2 { hot.addRect(rect) } else { lit.addRect(rect) }
                } else if s == peak - 1 && peak > on {
                    peaks.addRect(rect)
                } else {
                    dim.addRect(rect)
                }
            }
        }
        ctx.fill(dim, with: .color(Self.amber.opacity(0.07)))
        ctx.drawLayer { glow in
            glow.addFilter(.blur(radius: 3))
            glow.fill(lit, with: .color(Self.amber.opacity(0.7)))
            glow.fill(hot, with: .color(Color(hex: 0xFF6A2E).opacity(0.7)))
        }
        ctx.fill(lit, with: .color(Self.amber))
        ctx.fill(hot, with: .color(Color(hex: 0xFF7A3A)))
        ctx.fill(peaks, with: .color(Self.amber.opacity(0.85)))

        // The EQ curve, through every fader's setting.
        let gains = eq.gains
        let points = gains.enumerated().map { i, g in
            CGPoint(x: left + (CGFloat(i) + 0.5) * colW, y: top + CGFloat((EQBands.range - g) / (2 * EQBands.range)) * h)
        }
        var zero = Path()
        zero.move(to: CGPoint(x: left, y: top + h / 2))
        zero.addLine(to: CGPoint(x: left + w, y: top + h / 2))
        ctx.stroke(zero, with: .color(Self.amber.opacity(0.2)), style: StrokeStyle(lineWidth: 0.6, dash: [2, 3]))
        var curve = Path()
        curve.move(to: points[0])
        for i in 1..<points.count {
            let mid = CGPoint(x: (points[i - 1].x + points[i].x) / 2, y: (points[i - 1].y + points[i].y) / 2)
            curve.addQuadCurve(to: mid, control: points[i - 1])
        }
        curve.addLine(to: points[points.count - 1])
        let curveColor = Color(hex: 0xFFE7C2).opacity(eq.enabled ? 0.9 : 0.3)
        ctx.stroke(curve, with: .color(curveColor), style: StrokeStyle(lineWidth: 1.4, lineCap: .round, dash: eq.enabled ? [] : [3, 3]))
        for p in points {
            ctx.fill(Path(ellipseIn: CGRect(x: p.x - 1.6, y: p.y - 1.6, width: 3.2, height: 3.2)), with: .color(curveColor))
        }

        // A few frequency marks along the bottom.
        for i in [0, 4, 8, 10, 12, 16, 19] {
            ctx.draw(Text(EQBands.labels[i]).font(.system(size: 6.5, weight: .bold)).foregroundColor(Self.amber.opacity(0.55)),
                     at: CGPoint(x: left + (CGFloat(i) + 0.5) * colW, y: size.height - 9))
        }
    }
}

/// Holds each band's peak for a moment, then lets it fall.
@MainActor
private final class PeakHold {
    private var peaks = [Float](repeating: 0, count: EQBands.frequencies.count)
    private var heldAt = [Date](repeating: .distantPast, count: EQBands.frequencies.count)

    func update(_ now: [Float]) -> (now: [Float], peak: [Float]) {
        let t = Date()
        for i in 0..<min(now.count, peaks.count) {
            if now[i] >= peaks[i] {
                peaks[i] = now[i]
                heldAt[i] = t
            } else if t.timeIntervalSince(heldAt[i]) > 0.8 {
                peaks[i] = max(now[i], peaks[i] - 0.012)
            }
        }
        return (now, peaks)
    }
}

/// A small rectangular push switch with an optional lamp, like the EQ/FLAT buttons on a rack unit.
private struct RackSwitch: View {
    let title: String
    let lit: Bool
    let lamp: Color?
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 6) {
                if let lamp {
                    RoundedRectangle(cornerRadius: 1)
                        .fill(lit ? lamp : Color(hex: 0x3A2A10))
                        .frame(width: 9, height: 4)
                        .shadow(color: lit ? lamp : .clear, radius: 3)
                }
                Text(title)
                    .font(.system(size: 8.5, weight: .heavy))
                    .tracking(1.5)
                    .silkscreen(0.85)
            }
            .padding(.horizontal, 10)
            .frame(height: 20)
            .background(RoundedRectangle(cornerRadius: 3).fill(LinearGradient(colors: [Color(hex: 0x3A3A3C), Color(hex: 0x1B1B1D)], startPoint: .top, endPoint: .bottom)))
            .overlay(RoundedRectangle(cornerRadius: 3).strokeBorder(LinearGradient(colors: [.white.opacity(0.25), .black], startPoint: .top, endPoint: .bottom), lineWidth: 1))
            .shadow(color: .black.opacity(0.6), radius: 1, y: 1.5)
        }
        .buttonStyle(BouncyButtonStyle(pressedScale: 0.92))
    }
}

// MARK: - Live recording deck

struct RecorderDeckView: View {
    static let size = CGSize(width: 1000, height: 124)

    @ObservedObject private var deck = LiveDeck.shared
    @State private var meterHold = MeterHold()

    var body: some View {
        ZStack(alignment: .topLeading) {
            BlackPlate()
            RoundedRectangle(cornerRadius: 6, style: .continuous)
                .strokeBorder(Color(hex: 0xC9C9C4).opacity(0.5), lineWidth: 1)
                .padding(9)
            Corners(size: Self.size)

            // Title and counter.
            VStack(alignment: .leading, spacing: 3) {
                Text("LIVE SAMPLER")
                    .font(.system(size: 17, weight: .black))
                    .fontWidth(.condensed)
                    .tracking(2)
                    .silkscreen(0.9)
                Text("RECORDS EVERYTHING YOU HEAR")
                    .font(.system(size: 7, weight: .bold))
                    .tracking(1.6)
                    .silkscreen(0.5)
            }
            .position(x: 118, y: 36)
            CounterWindow(deck: deck).place(CGRect(x: 36, y: 56, width: 168, height: 42))

            // Keys.
            HStack(spacing: 14) {
                Keycap(label: "RECORD", symbol: "circle.fill", red: true, lit: deck.state == .recording) { deck.record() }
                    .help("Start recording whatever is playing, effects and EQ included")
                Keycap(label: "STOP", symbol: "stop.fill", red: false, lit: false) { deck.stop() }
                Keycap(label: "PLAY", symbol: "play.fill", red: false, lit: deck.state == .playing) { deck.play() }
                    .help("Play back the take")
                Keycap(label: "SAVE", symbol: "square.and.arrow.down", red: false, lit: deck.takeSaved) { deck.save() }
                    .help("Save the take to \(deck.destination.lastPathComponent)")
            }
            .position(x: 372, y: 62)

            PadLever(pad: $deck.pad).place(CGRect(x: 540, y: 16, width: 82, height: 94))

            // Meters, destination and status.
            TimelineView(.animation(minimumInterval: 1.0 / 30)) { _ in
                let raw = LiveRecorder.shared.levels()
                let held = meterHold.update(raw)
                VStack(alignment: .leading, spacing: 5) {
                    LEDMeter(label: "L", level: held.left, peak: held.peakLeft)
                    LEDMeter(label: "R", level: held.right, peak: held.peakRight)
                    HStack(spacing: 0) {
                        Spacer().frame(width: 14)
                        ForEach(["−48", "−30", "−18", "−12", "−6", "−3", "0"], id: \.self) { mark in
                            Text(mark).font(.system(size: 6, weight: .bold).monospacedDigit()).silkscreen(0.45)
                            if mark != "0" { Spacer(minLength: 0) }
                        }
                    }
                }
            }
            .place(CGRect(x: 640, y: 26, width: 320, height: 46))

            HStack(spacing: 8) {
                Text("SAVES TO").font(.system(size: 7, weight: .bold)).tracking(1.5).silkscreen(0.5)
                Button { deck.chooseDestination() } label: {
                    HStack(spacing: 4) {
                        Text(deck.destination.lastPathComponent.uppercased())
                            .font(.system(size: 9, weight: .black))
                            .tracking(1)
                            .lineLimit(1)
                        Image(systemName: "chevron.right").font(.system(size: 7, weight: .black))
                    }
                    .silkscreen(0.9)
                }
                .buttonStyle(BouncyButtonStyle())
                .help("Change where takes are saved")
                if deck.lastSaved != nil {
                    Button { deck.revealLastSaved() } label: {
                        Text("SHOW").font(.system(size: 7.5, weight: .black)).tracking(1.5).silkscreen(0.7)
                    }
                    .buttonStyle(BouncyButtonStyle())
                    .help("Show the last saved take in Finder")
                }
                Spacer(minLength: 0)
            }
            .place(CGRect(x: 640, y: 78, width: 320, height: 14))

            Text(deck.status)
                .font(.system(size: 9, weight: .medium))
                .silkscreen(0.6)
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(width: 320, alignment: .leading)
                .position(x: 800, y: 99)
        }
        .frame(width: Self.size.width, height: Self.size.height)
    }
}

/// The input pad: a steel bat-handle lever that drops into four detents, 0, −10, −30 and −60 dB.
/// Drag it, or click a mark.
private struct PadLever: View {
    @Binding var pad: Int
    @State private var dragStart: Int?

    private let steps = LiveDeck.padSteps
    private let slotTop: CGFloat = 20, slotBottom: CGFloat = 80

    var body: some View {
        let index = steps.firstIndex(of: pad) ?? 0
        ZStack(alignment: .topLeading) {
            Text("INPUT PAD")
                .font(.system(size: 7, weight: .heavy))
                .tracking(1.5)
                .silkscreen(0.7)
                .frame(width: 82)
                .position(x: 41, y: 6)

            // Slot plate.
            RoundedRectangle(cornerRadius: 3)
                .fill(Color.black)
                .overlay(RoundedRectangle(cornerRadius: 3).strokeBorder(LinearGradient(colors: [.black, .white.opacity(0.22)], startPoint: .top, endPoint: .bottom), lineWidth: 1))
                .frame(width: 8, height: slotBottom - slotTop + 10)
                .position(x: 22, y: (slotTop + slotBottom) / 2)

            // Detent marks.
            ForEach(Array(steps.enumerated()), id: \.offset) { i, step in
                let y = yFor(i)
                Button { withAnimation(Motion.snappy) { pad = step } } label: {
                    HStack(spacing: 5) {
                        Rectangle().fill(Color.white.opacity(i == index ? 0.8 : 0.3)).frame(width: 6, height: 1)
                        Text(step == 0 ? "0 dB" : "−\(step)")
                            .font(.system(size: 8, weight: .black).monospacedDigit())
                            .foregroundStyle(i == index ? (step == 0 ? Color(hex: 0xD9D9D4) : Color(hex: 0xFFC233)) : Color(hex: 0xD9D9D4).opacity(0.4))
                    }
                    .frame(width: 44, height: 14, alignment: .leading)
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .position(x: 56, y: y)
            }

            // Bat handle.
            ZStack {
                Capsule()
                    .fill(LinearGradient(colors: [Color(hex: 0xF4F4F1), Color(hex: 0xB3B3AE), Color(hex: 0x6E6E6A)], startPoint: .leading, endPoint: .trailing))
                    .frame(width: 10, height: 22)
                    .offset(y: -5)
                Circle()
                    .fill(RadialGradient(colors: [Color(hex: 0xE6E6E2), Color(hex: 0x6A6A66)], center: .init(x: 0.35, y: 0.3), startRadius: 0.5, endRadius: 8))
                    .frame(width: 14, height: 14)
                    .offset(y: 6)
            }
            .shadow(color: .black.opacity(0.7), radius: 2, x: 1, y: 2)
            .position(x: 22, y: yFor(index))
            .animation(.spring(response: 0.22, dampingFraction: 0.55), value: index)
        }
        .frame(width: 82, height: 94)
        .contentShape(Rectangle())
        .gesture(
            DragGesture(minimumDistance: 2)
                .onChanged { g in
                    if dragStart == nil { dragStart = index }
                    let spacing = (slotBottom - slotTop) / CGFloat(steps.count - 1)
                    let i = min(max((dragStart ?? 0) + Int((g.translation.height / spacing).rounded()), 0), steps.count - 1)
                    if steps[i] != pad { pad = steps[i] }
                }
                .onEnded { _ in dragStart = nil }
        )
        .help("Input pad: turns the recording level down by 10, 30 or 60 dB so the meters aren't pinned")
    }

    private func yFor(_ i: Int) -> CGFloat {
        slotTop + CGFloat(i) * (slotBottom - slotTop) / CGFloat(steps.count - 1)
    }
}

/// Red counter window: blinking REC while recording, then the take's length.
private struct CounterWindow: View {
    @ObservedObject var deck: LiveDeck

    var body: some View {
        TimelineView(.animation(minimumInterval: 0.1)) { context in
            let recording = deck.state == .recording
            let blink = Int(context.date.timeIntervalSinceReferenceDate * 2) % 2 == 0
            let red = Color(hex: 0xFF3B2A)
            HStack(spacing: 10) {
                HStack(spacing: 4) {
                    Circle().fill(red.opacity(recording && blink ? 1 : 0.18)).frame(width: 7, height: 7)
                        .shadow(color: recording && blink ? red : .clear, radius: 4)
                    Text("REC").font(.system(size: 9, weight: .black)).tracking(1)
                        .foregroundStyle(red.opacity(recording ? 0.95 : 0.25))
                }
                Text(stamp(deck.counterSeconds))
                    .font(.system(size: 20, weight: .medium, design: .monospaced))
                    .foregroundStyle(red.opacity(deck.state == .idle ? 0.3 : 0.95))
                    .shadow(color: red.opacity(deck.state == .idle ? 0 : 0.7), radius: 4)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(RoundedRectangle(cornerRadius: 4).fill(Color(hex: 0x120505)))
            .overlay(RoundedRectangle(cornerRadius: 4).strokeBorder(LinearGradient(colors: [.black, .white.opacity(0.2)], startPoint: .top, endPoint: .bottom), lineWidth: 1.5))
        }
    }

    private func stamp(_ s: Double) -> String {
        String(format: "%02d:%04.1f", Int(s) / 60, s.truncatingRemainder(dividingBy: 60))
    }
}

/// A chunky keycap, like a mechanical keyboard key: dished top face over a darker skirt. Sinks when pressed.
private struct Keycap: View {
    let label: String
    let symbol: String
    let red: Bool
    let lit: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            VStack(spacing: 7) {
                ZStack {
                    RoundedRectangle(cornerRadius: 8)
                        .fill(red ? Color(hex: 0x8E1320) : Color(hex: 0xA9A9A2))
                        .frame(width: 64, height: 48)
                        .offset(y: 3)
                    RoundedRectangle(cornerRadius: 8)
                        .fill(LinearGradient(colors: red ? [Color(hex: 0xE5364A), Color(hex: 0xC21F33)] : [Color(hex: 0xF7F7F3), Color(hex: 0xD9D9D3)],
                                             startPoint: .top, endPoint: .bottom))
                        .frame(width: 64, height: 46)
                    RoundedRectangle(cornerRadius: 6)
                        .fill(LinearGradient(colors: red ? [Color(hex: 0xF0485B), Color(hex: 0xD62A3E)] : [.white, Color(hex: 0xE8E8E3)],
                                             startPoint: .topLeading, endPoint: .bottomTrailing))
                        .frame(width: 50, height: 32)
                        .offset(y: -3)
                    Image(systemName: symbol)
                        .font(.system(size: 11, weight: .black))
                        .foregroundStyle(red ? Color.white.opacity(0.9) : Color(hex: 0x2A2A2A).opacity(0.7))
                        .offset(y: -3)
                    if lit {
                        Circle()
                            .fill(red ? Color(hex: 0xFFE3E3) : Color(hex: 0x3FD15A))
                            .frame(width: 6, height: 6)
                            .shadow(color: red ? .white : Color(hex: 0x3FD15A), radius: 4)
                            .offset(x: 22, y: -15)
                    }
                }
                .shadow(color: .black.opacity(0.6), radius: 3, y: 3)
                Text(label)
                    .font(.system(size: 8.5, weight: .heavy))
                    .tracking(1.8)
                    .silkscreen(0.8)
            }
        }
        .buttonStyle(KeycapPress())
    }
}

private struct KeycapPress: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .offset(y: configuration.isPressed ? 3 : 0)
            .brightness(configuration.isPressed ? -0.06 : 0)
            .animation(.spring(response: 0.16, dampingFraction: 0.5), value: configuration.isPressed)
    }
}

/// Horizontal LED ladder from −48 dB to 0 dB: green, then yellow, then red.
private struct LEDMeter: View {
    let label: String
    let level: Float
    let peak: Float

    private static let segments = 30

    var body: some View {
        HStack(spacing: 6) {
            Text(label).font(.system(size: 8, weight: .black)).silkscreen(0.7).frame(width: 8)
            Canvas { ctx, size in
                let gap: CGFloat = 2
                let segW = (size.width - gap * CGFloat(Self.segments - 1)) / CGFloat(Self.segments)
                let lit = Self.index(level), held = Self.index(peak)
                for s in 0..<Self.segments {
                    let x = CGFloat(s) * (segW + gap)
                    let color: Color = s >= Self.segments - 3 ? Color(hex: 0xFF3B2A) : (s >= Self.segments - 9 ? Color(hex: 0xFFC233) : Color(hex: 0x3FD15A))
                    let on = s < lit || s == held - 1
                    let rect = Path(roundedRect: CGRect(x: x, y: 0, width: segW, height: size.height), cornerRadius: 1)
                    ctx.fill(rect, with: .color(color.opacity(on ? 1 : 0.12)))
                }
            }
            .frame(height: 9)
        }
    }

    /// Lit segments for a peak level: 30 segments across −48…0 dB.
    static func index(_ level: Float) -> Int {
        guard level > 0 else { return 0 }
        let db = 20 * log10(Double(level))
        return min(segments, max(0, Int(((db + 48) / 48 * Double(segments)).rounded())))
    }
}

/// Smooths the meters and holds peaks for a moment.
@MainActor
private final class MeterHold {
    private var l: Float = 0, r: Float = 0
    private var pl: Float = 0, pr: Float = 0
    private var heldL = Date.distantPast, heldR = Date.distantPast

    func update(_ raw: (left: Float, right: Float)) -> (left: Float, right: Float, peakLeft: Float, peakRight: Float) {
        let t = Date()
        l = max(raw.left, l * 0.86)
        r = max(raw.right, r * 0.86)
        if raw.left >= pl { pl = raw.left; heldL = t } else if t.timeIntervalSince(heldL) > 1.2 { pl *= 0.94 }
        if raw.right >= pr { pr = raw.right; heldR = t } else if t.timeIntervalSince(heldR) > 1.2 { pr *= 0.94 }
        return (l, r, pl, pr)
    }
}

// MARK: - Take monitor

/// The fourth unit: the live take's waveform drawn as it records, a playhead you can drag
/// to scrub through it afterwards, and a pair of lights showing the playback level.
struct TakeMonitorView: View {
    static let size = CGSize(width: 1000, height: 112)
    private static let screen = CGRect(x: 214, y: 18, width: 520, height: 76)

    @ObservedObject private var deck = LiveDeck.shared
    @State private var meterHold = MeterHold()

    var body: some View {
        ZStack(alignment: .topLeading) {
            BlackPlate()
            Corners(size: Self.size)

            VStack(alignment: .leading, spacing: 3) {
                Text("TAKE MONITOR")
                    .font(.system(size: 15, weight: .black))
                    .fontWidth(.condensed)
                    .tracking(2)
                    .silkscreen(0.9)
                Text(caption)
                    .font(.system(size: 7, weight: .bold))
                    .tracking(1.6)
                    .silkscreen(0.5)
            }
            .position(x: 118, y: 34)

            TimelineView(.animation(minimumInterval: 1.0 / 30)) { _ in
                HStack(spacing: 6) {
                    Text(stamp(deck.playheadSeconds))
                        .foregroundStyle(Color(hex: 0xFFB02E))
                    Text("/ \(stamp(deck.state == .recording ? LiveRecorder.shared.seconds : deck.takeSeconds))")
                        .foregroundStyle(Color(hex: 0xFFB02E).opacity(0.45))
                }
                .font(.system(size: 12, weight: .medium, design: .monospaced))
                .frame(width: 168, height: 26)
                .background(RoundedRectangle(cornerRadius: 3).fill(Color(hex: 0x120B04)))
                .overlay(RoundedRectangle(cornerRadius: 3).strokeBorder(LinearGradient(colors: [.black, .white.opacity(0.2)], startPoint: .top, endPoint: .bottom), lineWidth: 1))
            }
            .position(x: 120, y: 76)

            TakeScreen(deck: deck).place(Self.screen)

            TimelineView(.animation(minimumInterval: 1.0 / 30)) { _ in
                let held = meterHold.update(deck.playbackLevels())
                VStack(alignment: .leading, spacing: 5) {
                    Text("PLAYBACK")
                        .font(.system(size: 7, weight: .heavy))
                        .tracking(1.5)
                        .silkscreen(0.6)
                    LEDMeter(label: "L", level: held.left, peak: held.peakLeft)
                    LEDMeter(label: "R", level: held.right, peak: held.peakRight)
                    HStack(spacing: 0) {
                        Spacer().frame(width: 14)
                        ForEach(["−48", "−30", "−18", "−6", "0"], id: \.self) { mark in
                            Text(mark).font(.system(size: 6, weight: .bold).monospacedDigit()).silkscreen(0.45)
                            if mark != "0" { Spacer(minLength: 0) }
                        }
                    }
                }
            }
            .place(CGRect(x: 756, y: 22, width: 210, height: 64))
        }
        .frame(width: Self.size.width, height: Self.size.height)
    }

    private var caption: String {
        switch deck.state {
        case .recording: return "DRAWING THE TAKE"
        case .ready: return "DRAG THE PLAYHEAD TO SCRUB"
        case .playing: return "PLAYING THE TAKE"
        case .idle: return "WAITING FOR A TAKE"
        }
    }

    private func stamp(_ s: Double) -> String {
        String(format: "%02d:%04.1f", Int(s) / 60, s.truncatingRemainder(dividingBy: 60))
    }
}

/// The waveform window. While recording, the take grows across the screen (red) with a write head at its end;
/// once it's done, the whole take is shown and a white playhead can be dragged to scrub.
private struct TakeScreen: View {
    @ObservedObject var deck: LiveDeck
    @State private var dragging = false

    private static let ink = Color(hex: 0xEDE6D6)
    private static let red = Color(hex: 0xFF3B2A)

    var body: some View {
        GeometryReader { geo in
            ZStack {
                RoundedRectangle(cornerRadius: 4)
                    .fill(LinearGradient(colors: [Color(hex: 0x0E0E10), Color(hex: 0x050506)], startPoint: .top, endPoint: .bottom))
                TimelineView(.animation(minimumInterval: 1.0 / 30)) { _ in
                    // Read the live values here, so every frame hands the canvas fresh data.
                    let recording = deck.state == .recording
                    let wave = recording ? LiveRecorder.shared.waveform() : deck.takeWave
                    let played = deck.playbackFraction
                    Canvas { ctx, size in draw(ctx, size, wave: wave, recording: recording, played: played) }
                }
                LinearGradient(stops: [
                    .init(color: .white.opacity(0.08), location: 0),
                    .init(color: .clear, location: 0.45),
                ], startPoint: .topLeading, endPoint: .bottomTrailing)
                    .allowsHitTesting(false)
                RoundedRectangle(cornerRadius: 4)
                    .strokeBorder(LinearGradient(colors: [.black, .white.opacity(0.22)], startPoint: .top, endPoint: .bottom), lineWidth: 2)
            }
            .clipShape(RoundedRectangle(cornerRadius: 4))
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        guard deck.hasTake else { return }
                        dragging = true
                        deck.scrub(to: Double((g.location.x - 8) / max(geo.size.width - 16, 1)))
                    }
                    .onEnded { _ in dragging = false }
            )
            .onContinuousHover { phase in
                if case .active = phase, deck.hasTake { NSCursor.resizeLeftRight.set() } else { NSCursor.arrow.set() }
            }
            .help(deck.hasTake ? "Drag to move the playhead through the take" : "The take's waveform appears here as it records")
        }
    }

    private func draw(_ ctx: GraphicsContext, _ size: CGSize, wave: [Float], recording: Bool, played: Double) {
        let inset: CGFloat = 8
        let w = size.width - inset * 2, h = size.height
        let mid = h / 2

        // Grid: centre line and a tick every second.
        ctx.fill(Path(CGRect(x: inset, y: mid, width: w, height: 0.6)), with: .color(Self.ink.opacity(0.12)))
        let rate = 44100.0 / Double(LiveRecorder.waveStep)  // close enough for tick spacing
        // While recording, the window holds at least 10 s so the take visibly grows into it.
        let span = recording ? max(Double(wave.count), 10 * rate) : Double(max(wave.count, 1))
        let perSecond = Double(w) / span * rate
        if perSecond > 6 {
            var x = 0.0, n = 0
            while x <= Double(w) {
                let major = n % 5 == 0
                ctx.fill(Path(CGRect(x: inset + CGFloat(x), y: h - (major ? 7 : 4), width: 0.6, height: major ? 7 : 4)),
                         with: .color(Self.ink.opacity(major ? 0.3 : 0.15)))
                x += perSecond
                n += 1
            }
        }

        guard !wave.isEmpty else {
            ctx.draw(Text(recording ? "LISTENING…" : "NO TAKE YET").font(.system(size: 9, weight: .heavy)).tracking(2)
                        .foregroundColor(Self.ink.opacity(0.3)), at: CGPoint(x: size.width / 2, y: mid))
            return
        }

        // Waveform: one bar per pixel column, loudest sample in that column.
        let columns = Int(w)
        var bars = Path(), playedBars = Path()
        let filled = recording ? min(columns, Int(Double(wave.count) / span * Double(columns))) : columns
        for c in 0..<max(filled, 1) {
            let lo = Int(Double(c) / Double(columns) * span)
            let hi = min(wave.count, max(lo + 1, Int(Double(c + 1) / Double(columns) * span)))
            guard lo < wave.count else { break }
            let peak = wave[lo..<hi].max() ?? 0
            let barH = max(1, CGFloat(min(peak, 1)) * (h * 0.42))
            let rect = CGRect(x: inset + CGFloat(c), y: mid - barH, width: 0.8, height: barH * 2)
            if !recording && Double(c) / Double(columns) <= played { playedBars.addRect(rect) } else { bars.addRect(rect) }
        }
        ctx.fill(bars, with: .color(recording ? Self.red.opacity(0.85) : Self.ink.opacity(0.75)))
        ctx.fill(playedBars, with: .color(Color(hex: 0xFFB02E)))

        if recording {
            // Write head at the end of what's been recorded.
            let x = inset + CGFloat(filled)
            ctx.fill(Path(CGRect(x: x, y: 2, width: 1.5, height: h - 4)), with: .color(Self.red))
            ctx.drawLayer { glow in
                glow.addFilter(.blur(radius: 3))
                glow.fill(Path(CGRect(x: x - 1, y: 2, width: 3, height: h - 4)), with: .color(Self.red.opacity(0.8)))
            }
        } else {
            // Playhead with a grab tab.
            let x = inset + CGFloat(played) * w
            ctx.fill(Path(CGRect(x: x - 0.75, y: 0, width: 1.5, height: h)), with: .color(.white))
            var tab = Path()
            tab.move(to: CGPoint(x: x - 5, y: 0))
            tab.addLine(to: CGPoint(x: x + 5, y: 0))
            tab.addLine(to: CGPoint(x: x, y: 7))
            tab.closeSubpath()
            ctx.fill(tab, with: .color(.white))
        }
    }
}
