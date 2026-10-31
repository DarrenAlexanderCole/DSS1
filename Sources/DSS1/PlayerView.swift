import SwiftUI

/// The listening room: an orange deck with a reactive visualizer on the left,
/// the whole crate (every track in the destination folder) on the right.
struct PlayerView: View {
    var body: some View {
        HStack(spacing: 0) {
            DeckSide()
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            LibraryPanel()
                .frame(width: 380)
        }
        .background(Palette.caseEdge)
    }
}

// MARK: - Deck

private struct DeckSide: View {
    @EnvironmentObject private var model: AppModel
    @EnvironmentObject private var player: AudioPlayer
    @EnvironmentObject private var library: Library

    var body: some View {
        ZStack {
            AmbientVisualizer(meter: player.meter)
            GrainOverlay(image: Grain.paper)
            GrainOverlay(image: Grain.dust)

            VStack(alignment: .leading, spacing: 0) {
                // Wordmark on the left now that four buttons share the row with it.
                HStack {
                    Wordmark(color: Palette.ink)
                    Spacer(minLength: 16)
                    addButton
                }
                .frame(height: 30)

                trackHeader
                    .padding(.top, 22)

                Spacer()

                HStack(alignment: .bottom, spacing: 0) {
                    let analysis = player.currentURL.flatMap { library.result(for: $0) }
                    TempoReadout(result: analysis)
                    KeyReadout(result: analysis)
                        .padding(.leading, 34)
                    Spacer()
                    BandLegend(meter: player.meter)
                        .padding(.bottom, 8)
                    VolumeKnob()
                        .padding(.leading, 22)
                }
                .padding(.bottom, 18)

                Scrubber()
                    .padding(.bottom, 20)

                HStack(spacing: 14) {
                    TabKey(label: "prev") { player.previous() }
                    StartStopKey(lit: player.isPlaying) { player.toggle() }
                    TabKey(label: "next") { player.next() }
                }
                .frame(maxWidth: .infinity)
            }
            .padding(.horizontal, 36)
            .padding(.top, 14)
            .padding(.bottom, 30)
        }
    }

    private var trackHeader: some View {
        ZStack(alignment: .topLeading) {
            if let info = player.current {
                VStack(alignment: .leading, spacing: 8) {
                    Text(info.title)
                        .font(.system(size: 44, weight: .black))
                        .fontWidth(.condensed)
                        .lineLimit(2)
                    Text(info.artist.isEmpty ? info.format : "\(info.artist.uppercased())  ·  \(info.format)")
                        .font(.system(size: 12, weight: .bold))
                        .tracking(2)
                        .opacity(0.65)
                }
                .id(player.index)
                .transition(.asymmetric(insertion: .move(edge: .trailing).combined(with: .opacity),
                                        removal: .move(edge: .leading).combined(with: .opacity)))
            } else {
                VStack(alignment: .leading, spacing: 8) {
                    Text("Pick a track")
                        .font(.system(size: 44, weight: .black))
                        .fontWidth(.condensed)
                    Text("FROM THE CRATE  →")
                        .font(.system(size: 12, weight: .bold))
                        .tracking(2)
                        .opacity(0.65)
                }
            }
        }
        .foregroundStyle(Palette.ink)
        .frame(maxWidth: .infinity, alignment: .leading)
        .animation(Motion.bouncy, value: player.index)
    }

    private var addButton: some View {
        HStack(spacing: 8) {
                Button { model.showRadio() } label: {
                    HStack(spacing: 6) {
                        Image(systemName: "radio")
                        Text("RADIO").tracking(2)
                    }
                    .font(.system(size: 11, weight: .black))
                    .foregroundStyle(Palette.cream)
                    .padding(.horizontal, 14)
                    .padding(.vertical, 8)
                    .background(Capsule().fill(Palette.ink))
                }
                .buttonStyle(BouncyButtonStyle())
                .help("The tape deck, with VU meters (⌘3)")
            if player.currentURL != nil {
                Button { model.showPerformance() } label: {
                    HStack(spacing: 6) {
                        Image(systemName: "hand.draw.fill")
                        Text("PERFORM").tracking(2)
                    }
                    .font(.system(size: 11, weight: .black))
                    .foregroundStyle(Palette.cream)
                    .padding(.horizontal, 14)
                    .padding(.vertical, 8)
                    .background(Capsule().fill(Palette.ink))
                }
                .buttonStyle(BouncyButtonStyle())
                .help("Play effects on an XY pad (⌘4)")
                .transition(.scale.combined(with: .opacity))
                Button { model.openChopForCurrent() } label: {
                    HStack(spacing: 6) {
                        Image(systemName: "square.grid.4x3.fill")
                        Text("CHOP").tracking(2)
                    }
                    .font(.system(size: 11, weight: .black))
                    .foregroundStyle(Palette.ink)
                    .padding(.horizontal, 14)
                    .padding(.vertical, 8)
                    .background(Capsule().fill(Palette.cream))
                }
                .buttonStyle(BouncyButtonStyle())
                .help("Chop this track onto pads (⌘K)")
                .transition(.scale.combined(with: .opacity))
            }
            zipButton
        }
    }

    private var zipButton: some View {
        Button { model.showHome() } label: {
            HStack(spacing: 6) {
                Image(systemName: "plus")
                Text("ADD AUDIO").tracking(2)
            }
            .font(.system(size: 11, weight: .black))
            .foregroundStyle(Palette.cream)
            .padding(.horizontal, 14)
            .padding(.vertical, 8)
            .background(Capsule().fill(Palette.ink))
        }
        .buttonStyle(BouncyButtonStyle())
        .help("Add audio files, folders or zips (drop screen)")
    }
}

// MARK: - Visualizer

private func average(_ bands: [Float], _ range: Range<Int>) -> Double {
    Double(bands[range].reduce(0, +)) / Double(range.count)
}

private enum Bands {
    static let low = 0..<9      // up to ~250 Hz
    static let mid = 9..<23     // ~250 Hz – 4 kHz
    static let high = 23..<32   // 4 kHz and up
}

/// Smooth, never-repeating wander in -1...1: a few sines at unrelated speeds.
private func drift(_ t: Double, _ seed: Double) -> Double {
    sin(t * 0.131 + seed) * 0.5 + sin(t * 0.0713 + seed * 1.7) * 0.3 + sin(t * 0.0377 + seed * 2.9) * 0.2
}

/// Slow 0...1 cycle with its own period, used to fade layers in and out on separate clocks.
private func wave(_ t: Double, period: Double, phase: Double) -> Double {
    0.5 + 0.5 * sin(t * 2 * .pi / period + phase)
}

/// Remembers the music between frames: fast and slow followers for each band,
/// kick detection, and the ripples those kicks leave behind.
private final class VisualizerState {
    struct Ripple {
        let born: Double
        let strength: Double
        let tilt: Double
    }

    struct Mote {
        let radius: Double
        let angle: Double
        let speed: Double
        let size: Double
        let phase: Double
    }

    var low = 0.0, mid = 0.0, high = 0.0
    var lowSlow = 0.0, midSlow = 0.0, highSlow = 0.0
    var energy = 0.0
    var ripples: [Ripple] = []
    let motes: [Mote] = (0..<80).map { _ in
        Mote(radius: .random(in: 0.16...0.66), angle: .random(in: 0...(2 * .pi)), speed: .random(in: -0.07...0.07),
             size: .random(in: 0.6...2.2), phase: .random(in: 0...(2 * .pi)))
    }

    private var last: Double?
    private var lowAverage = 0.0
    private var lastKick = 0.0

    func update(t: Double, bands: [Float]) {
        let dt = min(max(t - (last ?? t), 0), 0.1)
        last = t
        let l = average(bands, Bands.low)
        let m = average(bands, Bands.mid)
        let h = average(bands, Bands.high)

        func follow(_ value: inout Double, _ target: Double, attack: Double, release: Double) {
            value += (target - value) * min(1, dt * (target > value ? attack : release))
        }
        follow(&low, l, attack: 30, release: 6)
        follow(&mid, m, attack: 24, release: 5)
        follow(&high, h, attack: 30, release: 8)
        follow(&lowSlow, l, attack: 1.2, release: 0.5)
        follow(&midSlow, m, attack: 1.0, release: 0.4)
        follow(&highSlow, h, attack: 1.0, release: 0.4)
        follow(&energy, (l + m + h) / 3, attack: 0.8, release: 0.25)

        // A kick: the lows jump well above their recent average.
        if l > lowAverage * 1.2 + 0.05, t - lastKick > 0.3 {
            lastKick = t
            ripples.append(Ripple(born: t, strength: min(1, (l - lowAverage) * 3 + 0.35), tilt: .random(in: 0...(2 * .pi))))
        }
        follow(&lowAverage, l, attack: 2, release: 2)
        ripples.removeAll { t - $0.born > 4 }
        if ripples.count > 14 { ripples.removeFirst(ripples.count - 14) }
    }
}

/// Ambient, generative visualizer. Every layer drifts and fades on its own slow cycle,
/// the colours drift between palettes and the shapes morph, so the picture never quite repeats.
/// The music swells, pushes and ripples it.
///  - paper: the background colour itself slowly shifts scene
///  - haze: soft gradient orbs that float around and breathe
///  - lows: the gradient sun swells (its edge sometimes ripples into a scalloped knob),
///    and each kick sends a ripple outward
///  - mids: the moon orbits the sun, turning from a solid disc into a ring and back
///  - highs: three zig-zag rings spinning different ways, plus twinkling dust
private struct AmbientVisualizer: View {
    let meter: LevelMeter
    @State private var state = VisualizerState()

    var body: some View {
        TimelineView(.animation) { context in
            let t = context.date.timeIntervalSinceReferenceDate
            let bands = meter.snapshot().bands
            Canvas { ctx, size in
                state.update(t: t, bands: bands)
                VisualizerPainter.draw(in: ctx, size: size, t: t, bands: bands, state: state)
            }
        }
        .allowsHitTesting(false)
    }
}

/// An sRGB colour that can be blended, for crossfading between scenes.
private struct RGB {
    var r, g, b: Double

    init(_ hex: UInt32) {
        r = Double((hex >> 16) & 0xFF) / 255
        g = Double((hex >> 8) & 0xFF) / 255
        b = Double(hex & 0xFF) / 255
    }

    private init(r: Double, g: Double, b: Double) {
        self.r = r
        self.g = g
        self.b = b
    }

    func mix(_ other: RGB, _ f: Double) -> RGB {
        RGB(r: r + (other.r - r) * f, g: g + (other.g - g) * f, b: b + (other.b - b) * f)
    }

    func color(_ opacity: Double = 1) -> Color {
        Color(.sRGB, red: r, green: g, blue: b, opacity: opacity)
    }
}

/// One colour mood for the visualizer. Ink text stays readable on every paper.
private struct VisualScene {
    var paper: [RGB]
    var dusk: RGB
    var sun: RGB
    var sunLight: RGB
    var moon: RGB
    var overlap: RGB
    var line: RGB
    var haze: [RGB]

    /// Moose-print orange and black.
    static let ember = VisualScene(
        paper: [RGB(0xF4875E), RGB(0xE8472B), RGB(0xEE5530)], dusk: RGB(0x9E2F5E),
        sun: RGB(0xF7853A), sunLight: RGB(0xFFC08A), moon: RGB(0x161414), overlap: RGB(0xB8321E),
        line: RGB(0xFBF1DC), haze: [RGB(0xF7853A), RGB(0xE2B33B), RGB(0xFBF1DC), RGB(0xB8321E)])

    /// The riso fish print: cream paper, leaf green, lipstick red, yolk yellow.
    static let riso = VisualScene(
        paper: [RGB(0xF6F2CF), RGB(0xF1EDC0), RGB(0xE7E2AE)], dusk: RGB(0xCFC88E),
        sun: RGB(0x3F6E25), sunLight: RGB(0x7AA548), moon: RGB(0xC42A3E), overlap: RGB(0x2B2A1F),
        line: RGB(0x2B2A1F), haze: [RGB(0xE8C036), RGB(0xC42A3E), RGB(0x7AA548), RGB(0xFFFFFF)])

    /// The yellow amp knob: warm gold with a spun-silver sun.
    static let brass = VisualScene(
        paper: [RGB(0xF8DA6A), RGB(0xF0C23A), RGB(0xE3A92B)], dusk: RGB(0xC7731F),
        sun: RGB(0xE9E7E0), sunLight: RGB(0xFFFFFF), moon: RGB(0x161414), overlap: RGB(0xDC6537),
        line: RGB(0x161414), haze: [RGB(0xFFFFFF), RGB(0xDC6537), RGB(0xF7853A), RGB(0xFFF3C0)])

    static let all = [ember, riso, brass]
    /// Each launch starts somewhere different. `CRATE_SCENE` (0, 1, 2…) pins the start, for testing.
    static let offset = ProcessInfo.processInfo.environment["CRATE_SCENE"].flatMap(Double.init) ?? Double.random(in: 0..<3)
    private static let launch = Date().timeIntervalSinceReferenceDate

    func mix(_ o: VisualScene, _ f: Double) -> VisualScene {
        VisualScene(paper: zip(paper, o.paper).map { $0.mix($1, f) }, dusk: dusk.mix(o.dusk, f),
                    sun: sun.mix(o.sun, f), sunLight: sunLight.mix(o.sunLight, f),
                    moon: moon.mix(o.moon, f), overlap: overlap.mix(o.overlap, f), line: line.mix(o.line, f),
                    haze: zip(haze, o.haze).map { $0.mix($1, f) })
    }

    /// Each scene holds for a while, then slowly melts into the next.
    static func at(_ t: Double) -> VisualScene {
        let hold = 80.0
        let x = (t - launch) / hold + offset
        let i = Int(floor(x)) % all.count
        let frac = x - floor(x)
        let f = max(0, min(1, (frac - 0.6) / 0.4))
        let eased = f * f * (3 - 2 * f)
        return all[i].mix(all[(i + 1) % all.count], eased)
    }
}

private enum VisualizerPainter {
    private struct Ring {
        let radius: Double
        let teeth: Int
        let spin: Double
        let bands: Range<Int>
        let period: Double
        let phase: Double
        let width: CGFloat
    }

    private static let rings = [
        Ring(radius: 0.36, teeth: 96, spin: 0.07, bands: 9..<20, period: 17, phase: 0, width: 1.6),
        Ring(radius: 0.43, teeth: 150, spin: -0.045, bands: 14..<28, period: 29, phase: 1.9, width: 2),
        Ring(radius: 0.51, teeth: 210, spin: 0.025, bands: 20..<32, period: 41, phase: 4.2, width: 1.2),
    ]

    private static func circle(_ o: CGPoint, _ r: Double) -> Path {
        let r = CGFloat(r)
        return Path(ellipseIn: CGRect(x: o.x - r, y: o.y - r, width: r * 2, height: r * 2))
    }

    private static func point(_ o: CGPoint, _ angle: Double, _ r: Double) -> CGPoint {
        CGPoint(x: o.x + CGFloat(cos(angle) * r), y: o.y + CGFloat(sin(angle) * r))
    }

    /// A soft sphere of colour fading to nothing at its edge.
    private static func glow(_ ctx: GraphicsContext, at o: CGPoint, radius r: Double, color: RGB, opacity: Double) {
        let gradient = Gradient(colors: [color.color(opacity), color.color(opacity * 0.45), color.color(0)])
        ctx.fill(circle(o, r), with: .radialGradient(gradient, center: o, startRadius: 0, endRadius: CGFloat(r)))
    }

    static func draw(in ctx: GraphicsContext, size: CGSize, t: Double, bands: [Float], state s: VisualizerState) {
        let scene = VisualScene.at(t)
        let side = Double(min(size.width, size.height * 0.9)) * 0.62
        // Sits high, clear of the BPM row and the timeline.
        let c = CGPoint(x: size.width / 2, y: size.height * 0.39)
        // Quiet passages calm the whole picture down; it never goes fully still.
        let alive = 0.35 + 0.65 * min(1, s.energy * 1.6)

        drawPaper(ctx, size: size, t: t, scene: scene)

        // Everything above the paper is drawn as one layer that wanders between 0% and 30%,
        // so it reads as part of the background and surfaces and sinks on its own.
        var shapes = ctx
        shapes.opacity = 0.15 + 0.15 * drift(t, 42)
        shapes.drawLayer { layer in
            drawHaze(layer, size: size, side: side, t: t, s: s, scene: scene, alive: alive)
            drawRipples(layer, c: c, side: side, t: t, s: s, scene: scene)
            drawSunAndMoon(layer, c: c, side: side, t: t, s: s, scene: scene)
            drawRings(layer, c: c, side: side, t: t, bands: bands, scene: scene, alive: alive)
            drawDust(layer, c: c, side: side, t: t, s: s, scene: scene)
        }
    }

    private static func drawPaper(_ ctx: GraphicsContext, size: CGSize, t: Double, scene: VisualScene) {
        let rect = Path(CGRect(origin: .zero, size: size))
        ctx.fill(rect, with: .linearGradient(Gradient(colors: scene.paper.map { $0.color() }),
                                             startPoint: .zero, endPoint: CGPoint(x: size.width, y: size.height)))
        let duskAt = CGPoint(x: size.width * (0.15 + 0.2 * drift(t * 0.4, 1)), y: size.height * (0.9 + 0.1 * drift(t * 0.3, 2)))
        glow(ctx, at: duskAt, radius: 560, color: scene.dusk, opacity: 0.3 + 0.3 * wave(t, period: 37, phase: 0))
        let lightAt = CGPoint(x: size.width * (0.85 + 0.15 * drift(t * 0.35, 5)), y: size.height * (0.1 + 0.15 * drift(t * 0.25, 6)))
        glow(ctx, at: lightAt, radius: 480, color: scene.sunLight, opacity: 0.25 * wave(t, period: 53, phase: 2))
    }

    private static func drawHaze(_ ctx: GraphicsContext, size: CGSize, side: Double, t: Double, s: VisualizerState,
                                 scene: VisualScene, alive: Double) {
        for i in 0..<scene.haze.count {
            let seed = Double(i) * 2.17
            // Free-floating: they roam the whole deck, not just around the centre.
            let o = CGPoint(x: size.width * CGFloat(0.5 + 0.42 * drift(t * 0.45, seed)),
                            y: size.height * CGFloat(0.5 + 0.36 * drift(t * 0.38, seed + 9)))
            let r = side * (0.3 + 0.1 * drift(t, seed + 4) + s.lowSlow * 0.15)
            let opacity = (0.15 + 0.4 * wave(t, period: 19 + Double(i) * 7.3, phase: seed)) * alive
            glow(ctx, at: o, radius: r, color: scene.haze[i], opacity: opacity)
        }
    }

    private static func drawRipples(_ ctx: GraphicsContext, c: CGPoint, side: Double, t: Double, s: VisualizerState,
                                    scene: VisualScene) {
        for ripple in s.ripples {
            let age = (t - ripple.born) / 4
            let fade = (1 - age) * (1 - age)
            let o = point(c, ripple.tilt, side * 0.03 * age)
            ctx.stroke(circle(o, side * (0.27 + age * 0.38)),
                       with: .color(scene.line.color(fade * ripple.strength * 0.5)),
                       lineWidth: CGFloat(0.8 + 1.6 * (1 - age)))
        }
    }

    /// The sun's outline: round, or rippling into a scalloped knob edge. The scallops fade
    /// to nothing before the count changes, so the morph never jumps.
    private static func sunPath(center: CGPoint, radius: Double, t: Double) -> Path {
        let cycle = 90.0
        let phase = (t / cycle).truncatingRemainder(dividingBy: 1)
        let lobes = Double([7, 9, 12, 5][Int(floor(t / cycle)) % 4])
        let depth = 0.05 * pow(sin(.pi * phase), 2)
        let turn = t * 0.05
        var path = Path()
        let steps = 180
        for k in 0...steps {
            let angle = Double(k) / Double(steps) * 2 * .pi
            let r = radius * (1 + depth * sin(lobes * angle + turn))
            let p = point(center, angle, r)
            if k == 0 { path.move(to: p) } else { path.addLine(to: p) }
        }
        path.closeSubpath()
        return path
    }

    private static func drawSunAndMoon(_ ctx: GraphicsContext, c: CGPoint, side: Double, t: Double, s: VisualizerState,
                                       scene: VisualScene) {
        let sunCenter = point(c, .pi / 2 + drift(t * 0.4, 1) * 0.6, side * (0.05 + 0.025 * drift(t, 3)))
        let sunR = side * (0.24 + 0.02 * drift(t * 0.7, 5) + s.low * 0.06 + s.lowSlow * 0.04)

        glow(ctx, at: sunCenter, radius: sunR * 1.5, color: scene.sun, opacity: 0.25 + 0.4 * s.low)

        // A lit gradient sphere, its highlight wandering around the top.
        let sunPath = sunPath(center: sunCenter, radius: sunR, t: t)
        let lightAngle = -.pi * 0.7 + drift(t * 0.2, 17) * 0.5
        let light = point(sunCenter, lightAngle, sunR * 0.45)
        let sunOpacity = 0.7 + 0.3 * wave(t, period: 13, phase: 0.4)
        ctx.fill(sunPath, with: .radialGradient(
            Gradient(colors: [scene.sunLight.color(sunOpacity), scene.sun.color(sunOpacity), scene.sun.mix(scene.overlap, 0.25).color(sunOpacity)]),
            center: light, startRadius: 0, endRadius: CGFloat(sunR * 1.5)))

        // The moon: a solid disc that sometimes hollows into a ring.
        let orbit = t * 0.045 + drift(t * 0.3, 7) * 1.4 - .pi / 2
        let moonCenter = point(sunCenter, orbit, side * (0.25 + 0.09 * drift(t * 0.5, 11) + s.mid * 0.04))
        let moonR = side * (0.13 + 0.03 * drift(t * 0.6, 13) + s.mid * 0.05)
        let moonOpacity = 0.3 + 0.65 * wave(t, period: 23, phase: 2.1)
        let hollow = pow(wave(t, period: 67, phase: 4), 3)
        let moonPath = circle(moonCenter, moonR)

        ctx.fill(moonPath, with: .color(scene.moon.color(moonOpacity * (1 - hollow))))
        ctx.drawLayer { layer in
            layer.clip(to: sunPath)
            layer.fill(moonPath, with: .color(scene.overlap.color(moonOpacity * (1 - hollow))))
        }
        if hollow > 0.02 {
            ctx.stroke(circle(moonCenter, moonR * 0.86), with: .color(scene.moon.color(moonOpacity * hollow)),
                       lineWidth: CGFloat(moonR * 0.28))
        }
    }

    private static func drawRings(_ ctx: GraphicsContext, c: CGPoint, side: Double, t: Double, bands: [Float],
                                  scene: VisualScene, alive: Double) {
        for (n, ring) in rings.enumerated() {
            let opacity = (0.06 + 0.94 * pow(wave(t, period: ring.period, phase: ring.phase), 1.6)) * (0.45 + 0.55 * alive)
            guard opacity > 0.02 else { continue }
            let seed = Double(n) * 3.3
            let base = side * (ring.radius + 0.015 * drift(t * 0.5, seed))
            let rotation = t * ring.spin + drift(t * 0.2, seed) * 0.8
            let lobes = Double(3 + n)
            var path = Path()
            for k in 0...ring.teeth {
                let i = k % ring.teeth
                let u = Double(i) / Double(ring.teeth)
                let angle = u * 2 * .pi + rotation
                let mirrored = 1 - abs(u * 2 - 1)
                let band = ring.bands.lowerBound + Int(mirrored * Double(ring.bands.count - 1))
                let amp = Double(bands[band])
                let swell = side * 0.014 * sin(angle * lobes + t * (0.3 + 0.1 * Double(n)))
                let tooth = (i % 2 == 0 ? 1.0 : -1.0) * (0.8 + amp * side * 0.042)
                let p = point(c, angle, base + swell + tooth)
                if k == 0 { path.move(to: p) } else { path.addLine(to: p) }
            }
            path.closeSubpath()
            ctx.stroke(path, with: .color(scene.line.color(opacity)),
                       style: StrokeStyle(lineWidth: ring.width, lineJoin: .miter))
        }
    }

    private static func drawDust(_ ctx: GraphicsContext, c: CGPoint, side: Double, t: Double, s: VisualizerState,
                                 scene: VisualScene) {
        for (i, mote) in s.motes.enumerated() {
            let angle = mote.angle + t * mote.speed
            let r = side * (mote.radius + 0.025 * drift(t * 0.3, Double(i)))
            let twinkle = wave(t, period: 2.5 + mote.size * 2.3, phase: mote.phase)
            let opacity = min(0.9, (0.08 + 0.5 * twinkle) * (0.35 + s.high * 1.5))
            ctx.fill(circle(point(c, angle, r), mote.size * (1 + s.highSlow)),
                     with: .color(scene.line.color(opacity)))
        }
    }
}

/// The tempo, big. Beside it, small coloured tags for half and double time,
/// plus a 3:2 reading when the beat genuinely supports one.
private struct TempoReadout: View {
    var result: TrackAnalyzer.Result?

    var body: some View {
        let bpm = result?.bpm
        VStack(alignment: .leading, spacing: -4) {
            Text("BPM")
                .font(.system(size: 10, weight: .black))
                .tracking(3)
                .opacity(0.6)
            HStack(alignment: .center, spacing: 8) {
                Text(bpm.map { String(format: "%.0f", $0) } ?? "––")
                    .font(.system(size: 64, weight: .black))
                    .fontWidth(.condensed)
                    .monospacedDigit()
                    .contentTransition(.numericText())
                if let bpm {
                    VStack(alignment: .leading, spacing: 3) {
                        tag("2×", bpm * 2, fill: Palette.ink, text: Palette.label)
                        tag("½", bpm / 2, fill: Palette.ink, text: Palette.cream)
                        if let alt = result?.altBPM {
                            tag("or", alt, fill: Palette.cream, text: Palette.ink)
                                .help("The beat also reads at this tempo")
                        }
                    }
                    .transition(.scale(scale: 0.6, anchor: .leading).combined(with: .opacity))
                }
            }
        }
        .foregroundStyle(Palette.ink)
        .animation(Motion.bouncy, value: bpm)
        .help("Tempo, worked out from the track")
    }

    private func tag(_ label: String, _ value: Double, fill: Color, text: Color) -> some View {
        HStack(spacing: 3) {
            Text(label).opacity(0.7)
            Text(String(format: "%.0f", value)).monospacedDigit()
        }
        .font(.system(size: 10, weight: .black))
        .foregroundStyle(text)
        .padding(.horizontal, 6)
        .padding(.vertical, 2)
        .background(Capsule().fill(fill))
    }
}

/// Musical key and its Camelot code, coloured by its spot on the Camelot wheel.
private struct KeyReadout: View {
    var result: TrackAnalyzer.Result?

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("KEY")
                .font(.system(size: 10, weight: .black))
                .tracking(3)
                .opacity(0.6)
            Text(result?.key ?? "––")
                .font(.system(size: 34, weight: .black))
                .fontWidth(.condensed)
                .contentTransition(.interpolate)
            if let camelot = result?.camelot {
                CamelotChip(code: camelot, large: true)
                    .transition(.scale.combined(with: .opacity))
            }
        }
        .foregroundStyle(Palette.ink)
        .padding(.bottom, 8)
        .animation(Motion.bouncy, value: result?.key)
        .help("Key, with its Camelot code for harmonic mixing")
    }
}

/// "8A" on its colour from the Camelot wheel: neighbouring codes mix well together.
struct CamelotChip: View {
    var code: String
    var large = false

    private var color: Color {
        let number = Double(code.dropLast()) ?? 1
        let minor = code.hasSuffix("A")
        // 1 = yellow-green … around the wheel; minor keys a touch lighter.
        let hue = ((number - 1) / 12 + 0.22).truncatingRemainder(dividingBy: 1)
        return Color(hue: hue, saturation: minor ? 0.45 : 0.6, brightness: minor ? 0.98 : 0.88)
    }

    var body: some View {
        Text(code)
            .font(.system(size: large ? 12 : 9, weight: .black).monospacedDigit())
            .foregroundStyle(Palette.ink)
            .padding(.horizontal, large ? 8 : 5)
            .padding(.vertical, large ? 3 : 1.5)
            .background(Capsule().fill(color))
    }
}

/// A black knurled knob with a spun-metal cap, numbered 0–10 like a guitar amp's.
/// Drag up or down to turn it.
private struct VolumeKnob: View {
    @EnvironmentObject private var player: AudioPlayer
    @State private var dragStart: Double?

    private let size: CGFloat = 66

    var body: some View {
        let value = player.volume
        let angle = Angle.degrees(-135 + 270 * value)

        ZStack {
            ForEach(0...10, id: \.self) { n in
                Text("\(n)")
                    .font(.system(size: 8, weight: .heavy).monospacedDigit())
                    .foregroundStyle(Palette.ink.opacity(Double(n) <= value * 10 + 0.01 ? 0.9 : 0.35))
                    .offset(y: -size / 2 - 11)
                    .rotationEffect(.degrees(-135 + 27 * Double(n)))
            }

            ZStack {
                Circle().fill(Palette.ink)
                ForEach(0..<56, id: \.self) { i in
                    Rectangle()
                        .fill(Color.white.opacity(0.16))
                        .frame(width: 1, height: 5)
                        .offset(y: -size / 2 + 3)
                        .rotationEffect(.degrees(Double(i) * 360 / 56))
                }
                Capsule()
                    .fill(Palette.cream)
                    .frame(width: 2.5, height: 9)
                    .offset(y: -size / 2 + 6)
            }
            .frame(width: size, height: size)
            .rotationEffect(angle)
            .shadow(color: .black.opacity(0.35), radius: 4, y: 3)

            SpunMetal()
                .frame(width: size * 0.7, height: size * 0.7)
                .overlay(
                    Text("VOLUME")
                        .font(.system(size: 7, weight: .bold))
                        .tracking(0.5)
                        .foregroundStyle(Palette.ink.opacity(0.75))
                )
        }
        .frame(width: size + 34, height: size + 34)
        .scaleEffect(dragStart == nil ? 1 : 1.06)
        .animation(Motion.jelly, value: dragStart == nil)
        .contentShape(Circle())
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { g in
                    if dragStart == nil { dragStart = player.volume }
                    let delta = (-g.translation.height + g.translation.width * 0.5) / 180
                    player.volume = min(max((dragStart ?? 0) + delta, 0), 1)
                }
                .onEnded { _ in dragStart = nil }
        )
        .help("Volume: drag up or down (⌘↑ / ⌘↓)")
    }
}

/// A turned aluminium cap: conic sheen plus fine concentric rings.
private struct SpunMetal: View {
    var body: some View {
        ZStack {
            Circle().fill(AngularGradient(colors: [
                Color(hex: 0xF4F4F2), Color(hex: 0xB9B9B5), Color(hex: 0xEDEDEA), Color(hex: 0xA9A9A4),
                Color(hex: 0xF4F4F2), Color(hex: 0xC4C4C0), Color(hex: 0xF4F4F2),
            ], center: .center))
            ForEach(1..<9, id: \.self) { i in
                Circle()
                    .stroke(Color.black.opacity(0.05), lineWidth: 0.6)
                    .padding(CGFloat(i) * 2.6)
            }
            Circle().strokeBorder(Color.black.opacity(0.25), lineWidth: 0.8)
        }
    }
}

/// Live meters for the three shapes, each with its own swatch.
private struct BandLegend: View {
    let meter: LevelMeter

    var body: some View {
        TimelineView(.animation) { _ in
            let bands = meter.snapshot().bands
            VStack(alignment: .trailing, spacing: 7) {
                row("LOW", average(bands, Bands.low)) { Circle().fill(Palette.glow) }
                row("MID", average(bands, Bands.mid)) { Circle().fill(Palette.ink) }
                row("HIGH", average(bands, Bands.high)) { Circle().strokeBorder(Palette.cream, lineWidth: 1.5) }
            }
        }
    }

    private func row<S: View>(_ name: String, _ value: Double, @ViewBuilder swatch: () -> S) -> some View {
        HStack(spacing: 8) {
            Text(name)
                .font(.system(size: 9, weight: .black))
                .tracking(2)
                .foregroundStyle(Palette.ink.opacity(0.7))
            ZStack(alignment: .leading) {
                Capsule().fill(Palette.ink.opacity(0.15))
                Capsule().fill(Palette.ink).frame(width: max(3, 90 * value))
            }
            .frame(width: 90, height: 4)
            swatch().frame(width: 10, height: 10)
        }
    }
}

// MARK: - Transport

private struct Scrubber: View {
    @EnvironmentObject private var player: AudioPlayer
    @State private var dragFraction: Double?

    var body: some View {
        let live = player.duration > 0 ? player.currentTime / player.duration : 0
        let fraction = dragFraction ?? live

        VStack(spacing: 6) {
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    Capsule().fill(Palette.ink.opacity(0.2)).frame(height: 3)
                    Capsule().fill(Palette.ink).frame(width: max(3, geo.size.width * fraction), height: 3)
                    Circle().fill(Palette.ink)
                        .frame(width: 12, height: 12)
                        .scaleEffect(dragFraction == nil ? 1 : 1.4)
                        .offset(x: geo.size.width * fraction - 6)
                        .animation(Motion.jelly, value: dragFraction == nil)
                }
                .frame(maxHeight: .infinity)
                .contentShape(Rectangle())
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { dragFraction = min(max($0.location.x / geo.size.width, 0), 1) }
                        .onEnded {
                            player.seek(to: min(max($0.location.x / geo.size.width, 0), 1))
                            dragFraction = nil
                        }
                )
            }
            .frame(height: 16)

            HStack {
                Text(clock(fraction * player.duration))
                Spacer()
                Text(clock(player.duration))
            }
            .font(.system(size: 11, weight: .bold).monospacedDigit())
            .foregroundStyle(Palette.ink.opacity(0.6))
        }
    }
}

func clock(_ seconds: Double) -> String {
    let s = Int(seconds.isFinite ? max(seconds, 0) : 0)
    return String(format: "%d:%02d", s / 60, s % 60)
}

/// The big brushed key.
private struct StartStopKey: View {
    var lit: Bool
    var action: () -> Void

    var body: some View {
        Button(action: action) {
            ZStack(alignment: .topTrailing) {
                Text("start · stop")
                    .font(.system(size: 18, weight: .regular))
                    .foregroundStyle(Palette.ink.opacity(0.85))
                    .frame(width: 176, height: 64)
                    .background(BrushedSteel())
                    .clipShape(RoundedRectangle(cornerRadius: 5, style: .continuous))
                    .overlay(RoundedRectangle(cornerRadius: 5, style: .continuous).strokeBorder(Color.black.opacity(0.35), lineWidth: 1))
                Circle()
                    .fill(lit ? Palette.red : Palette.ink.opacity(0.25))
                    .frame(width: 6, height: 6)
                    .shadow(color: lit ? Palette.red : .clear, radius: 4)
                    .padding(9)
            }
            .background(RoundedRectangle(cornerRadius: 6).fill(Palette.ink).offset(y: 4))
        }
        .buttonStyle(KeyPress())
        .animation(Motion.snappy, value: lit)
        .help("Play / pause (space)")
    }
}

/// Small silver tabs, like a deck's 33 / 45 selectors.
private struct TabKey: View {
    var label: String
    var action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 10) {
                Text(label)
                    .font(.system(size: 13, weight: .regular))
                    .foregroundStyle(Palette.ink.opacity(0.85))
                Rectangle().fill(Palette.ink).frame(width: 16, height: 4)
            }
            .frame(width: 92, height: 38)
            .background(BrushedSteel())
            .clipShape(RoundedRectangle(cornerRadius: 3, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 3, style: .continuous).strokeBorder(Color.black.opacity(0.35), lineWidth: 1))
            .background(RoundedRectangle(cornerRadius: 4).fill(Palette.ink).offset(y: 3))
        }
        .buttonStyle(KeyPress())
    }
}

private struct KeyPress: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .offset(y: configuration.isPressed ? 3 : 0)
            .scaleEffect(configuration.isPressed ? 0.97 : 1)
            .animation(.spring(response: 0.2, dampingFraction: 0.45), value: configuration.isPressed)
    }
}

// MARK: - Library

/// The flight case: every track in the destination, newest drops first.
private struct LibraryPanel: View {
    @EnvironmentObject private var model: AppModel
    @EnvironmentObject private var player: AudioPlayer
    @EnvironmentObject private var library: Library

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            VStack(alignment: .leading, spacing: 5) {
                Text("THE CRATE")
                    .font(.system(size: 13, weight: .black))
                    .fontWidth(.expanded)
                    .tracking(3)
                Text("\(library.tracks.count) TRACKS  ·  \(model.destination?.lastPathComponent.uppercased() ?? "NO FOLDER")")
                    .font(.system(size: 10, weight: .bold))
                    .tracking(1.5)
                    .opacity(0.5)
                    .contentTransition(.numericText())
            }
            .foregroundStyle(Palette.cream)
            .padding(.horizontal, 24)
            .padding(.top, 40)
            .padding(.bottom, 12)

            SortBar()
                .padding(.horizontal, 20)
                .padding(.bottom, 10)

            HStack(spacing: 10) {
                LevelMatchSwitch(on: $player.levelMatch, gainDB: player.levelGainDB)
                Spacer(minLength: 0)
                if let p = library.analysisProgress {
                    AnalysisReadout(done: p.done, total: p.total, color: Palette.cream)
                        .frame(width: 200)
                        .transition(.opacity)
                }
            }
            .padding(.horizontal, 22)
            .padding(.bottom, 12)
            .animation(Motion.snappy, value: library.analysisProgress == nil)

            Rectangle().fill(Palette.caseEdge).frame(height: 3)

            if library.tracks.isEmpty {
                VStack(spacing: 8) {
                    Text("Empty crate")
                        .font(.system(size: 20, weight: .black))
                        .fontWidth(.condensed)
                    Text("Drop audio files, folders or zips and it fills up here.")
                        .font(.system(size: 12))
                        .opacity(0.6)
                }
                .foregroundStyle(Palette.cream)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                ScrollViewReader { proxy in
                    ScrollView {
                        LazyVStack(spacing: 2) {
                            ForEach(library.ordered) { track in
                                TrackRow(track: track,
                                         isCurrent: track.url == player.currentURL,
                                         isPlaying: player.isPlaying,
                                         bpm: library.bpm(for: track.url),
                                         camelot: library.result(for: track.url)?.camelot,
                                         duration: library.duration(for: track.url),
                                         album: library.sort == .album ? library.album(for: track.url) : nil,
                                         unreadable: library.isUnreadable(track.url),
                                         played: library.isPlayed(track.url)) {
                                    model.play(track)
                                }
                                .contextMenu {
                                    Button("Play") { model.play(track) }
                                    Button("Chop This Track") { model.openChop(track) }
                                    Divider()
                                    Button("Show in Finder") { NSWorkspace.shared.activateFileViewerSelecting([track.url]) }
                                }
                                .id(track.id)
                                .transition(.move(edge: .top).combined(with: .opacity))
                            }
                        }
                        .padding(10)
                    }
                    .onChange(of: library.sort) { _, _ in resorted(proxy) }
                    .onChange(of: library.ascending) { _, _ in resorted(proxy) }
                    .onChange(of: player.currentURL) { _, url in
                        guard let url else { return }
                        withAnimation(Motion.bouncy) { proxy.scrollTo(url.path, anchor: .center) }
                    }
                    .onAppear {
                        if let url = player.currentURL { proxy.scrollTo(url.path, anchor: .center) }
                    }
                }
            }
        }
        .background(CaseSurface())
    }

    /// The next/previous keys follow the new order; the list jumps back to what's playing.
    private func resorted(_ proxy: ScrollViewProxy) {
        player.reorder(library.ordered.map(\.url))
        let target = player.currentURL?.path ?? library.ordered.first?.id
        if let target { withAnimation(Motion.bouncy) { proxy.scrollTo(target, anchor: .center) } }
    }
}

/// LEVEL MATCH: a little lamp switch; shows the gain it's applying to the current track.
private struct LevelMatchSwitch: View {
    @Binding var on: Bool
    var gainDB: Float

    var body: some View {
        Button { withAnimation(Motion.snappy) { on.toggle() } } label: {
            HStack(spacing: 6) {
                RoundedRectangle(cornerRadius: 1.5)
                    .fill(on ? Palette.label : Palette.cream.opacity(0.2))
                    .frame(width: 10, height: 4)
                    .shadow(color: on ? Palette.label : .clear, radius: 3)
                Text("LEVEL MATCH")
                    .font(.system(size: 9, weight: .black))
                    .tracking(1)
                if on {
                    Text(String(format: "%+.1f dB", gainDB))
                        .font(.system(size: 9, weight: .bold).monospacedDigit())
                        .opacity(0.6)
                        .contentTransition(.numericText())
                }
            }
            .foregroundStyle(Palette.cream.opacity(on ? 0.95 : 0.55))
            .padding(.horizontal, 9)
            .padding(.vertical, 5)
            .background(Capsule().fill(Color.white.opacity(0.06)))
            .fixedSize()
        }
        .buttonStyle(BouncyButtonStyle(pressedScale: 0.92))
        .help("Play every track at about the same loudness. Your files aren't changed.")
    }
}

/// SORT: a row of stamped tabs; tapping the active one flips its direction.
private struct SortBar: View {
    @EnvironmentObject private var library: Library

    var body: some View {
        HStack(spacing: 3) {
            ForEach(CrateSort.allCases) { field in
                let active = library.sort == field
                Button {
                    withAnimation(Motion.bouncy) {
                        if active { if field != .newest { library.ascending.toggle() } } else {
                            library.sort = field
                            // Newest, longest and fastest read best top-down; words go A→Z.
                            library.ascending = !(field == .length || field == .bpm)
                        }
                    }
                } label: {
                    HStack(spacing: 3) {
                        Text(field.label.uppercased())
                        if active && field != .newest {
                            Image(systemName: library.ascending ? "arrow.up" : "arrow.down")
                                .font(.system(size: 7, weight: .black))
                        }
                    }
                    .font(.system(size: 8.5, weight: .black))
                    .tracking(0.4)
                    .lineLimit(1)
                    .fixedSize()
                    .padding(.horizontal, 5.5)
                    .padding(.vertical, 5)
                    .foregroundStyle(active ? Palette.ink : Palette.cream.opacity(0.6))
                    .background(
                        Capsule().fill(active ? Palette.label : Color.white.opacity(0.06))
                    )
                    .contentShape(Capsule())
                }
                .buttonStyle(BouncyButtonStyle(pressedScale: 0.92))
                .help(field == .newest ? "Newest drops first" : "Sort by \(field.label.lowercased()) · click again to flip")
            }
            Spacer(minLength: 0)
        }
    }
}

private struct CaseSurface: View {
    var body: some View {
        ZStack(alignment: .leading) {
            Palette.caseGrey
            GrainOverlay(image: Grain.dust)
            GrainOverlay(image: Grain.paper)
            // Black aluminium edge with rivets.
            Rectangle().fill(Palette.caseEdge).frame(width: 10)
                .overlay {
                    VStack {
                        rivet
                        Spacer()
                        rivet
                        Spacer()
                        rivet
                    }
                    .padding(.vertical, 60)
                }
        }
    }

    private var rivet: some View {
        Circle()
            .fill(LinearGradient(colors: [Color(hex: 0x5A5C60), Color(hex: 0x1E1F21)], startPoint: .top, endPoint: .bottom))
            .frame(width: 5, height: 5)
    }
}

private struct TrackRow: View {
    let track: LibraryTrack
    let isCurrent: Bool
    let isPlaying: Bool
    let bpm: Double?
    let camelot: String?
    let duration: Double?
    var album: String? = nil
    var unreadable = false
    let played: Bool
    let action: () -> Void
    @State private var hovering = false

    var body: some View {
        Button(action: action) {
            HStack(spacing: 12) {
                MiniRecord(spinning: isCurrent && isPlaying)
                    .frame(width: 30, height: 30)
                VStack(alignment: .leading, spacing: 2) {
                    Text(track.info.title)
                        .font(.system(size: 13, weight: .semibold))
                        .lineLimit(1)
                    Text(album.map { a in track.info.artist.isEmpty ? a : "\(track.info.artist) · \(a)" }
                         ?? (track.info.artist.isEmpty ? track.info.format : track.info.artist))
                        .font(.system(size: 11))
                        .opacity(0.55)
                        .lineLimit(1)
                }
                Spacer(minLength: 6)
                if unreadable {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .font(.system(size: 11))
                        .foregroundStyle(Palette.label)
                        .help("This file couldn't be read for analysis; it may be damaged")
                } else if !played && !isCurrent {
                    Circle().fill(Palette.red).frame(width: 6, height: 6)
                        .help("Not played yet")
                }
                VStack(alignment: .trailing, spacing: 2) {
                    HStack(spacing: 5) {
                        if let camelot {
                            CamelotChip(code: camelot)
                        }
                        Text(bpm.map { String(format: "%.0f", $0) } ?? "··")
                            .font(.system(size: 13, weight: .black).monospacedDigit())
                            .fontWidth(.condensed)
                    }
                    Text(duration.map(clock) ?? "")
                        .font(.system(size: 10, weight: .medium).monospacedDigit())
                        .opacity(0.5)
                }
                .frame(width: 64, alignment: .trailing)
            }
            .foregroundStyle(isCurrent ? Palette.ink : Palette.cream.opacity(played ? 0.6 : 1))
            .padding(.horizontal, 10)
            .padding(.vertical, 7)
            .background(
                RoundedRectangle(cornerRadius: 10, style: .continuous)
                    .fill(isCurrent ? Palette.red : Color.white.opacity(hovering ? 0.07 : 0))
            )
            .contentShape(Rectangle())
        }
        .buttonStyle(BouncyButtonStyle(pressedScale: 0.97))
        .onHover { hovering = $0 }
        .animation(Motion.snappy, value: hovering)
        .animation(Motion.bouncy, value: isCurrent)
    }
}

/// A tiny record with a yellow label; it spins while its track plays.
private struct MiniRecord: View {
    var spinning: Bool

    var body: some View {
        TimelineView(.animation(minimumInterval: nil, paused: !spinning)) { context in
            ZStack {
                Circle().fill(Palette.ink)
                Circle().stroke(Color.white.opacity(0.12), lineWidth: 0.5).padding(3)
                Circle().stroke(Color.white.opacity(0.08), lineWidth: 0.5).padding(6)
                Circle().fill(Palette.label).padding(9)
                Rectangle().fill(Palette.ink.opacity(0.5)).frame(width: 1, height: 4).offset(y: -3.5)
                Circle().fill(Palette.ink).frame(width: 2.5, height: 2.5)
            }
            .rotationEffect(.degrees(spinning ? context.date.timeIntervalSinceReferenceDate * 200 : 0))
        }
    }
}
