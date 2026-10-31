import AppKit
import SwiftUI

/// The chop station: waveform with slice markers on top, MPC-style banks and pads below.
struct ChopView: View {
    @EnvironmentObject private var model: AppModel
    @ObservedObject var session: ChopSession
    @FocusState private var focused: Bool
    @State private var showExport = false

    /// MPC software's computer-keyboard pad layout (pad 1 = Z … pad 16 = 4).
    private static let padKeys: [Character: Int] = [
        "z": 0, "x": 1, "c": 2, "v": 3,
        "a": 4, "s": 5, "d": 6, "f": 7,
        "q": 8, "w": 9, "e": 10, "r": 11,
        "1": 12, "2": 13, "3": 14, "4": 15,
    ]

    var body: some View {
        VStack(spacing: 14) {
            topBar
            WaveformPanel(session: session)
                .frame(maxHeight: .infinity)
            ChopTools(session: session)
            HStack(alignment: .top, spacing: 22) {
                BankKeys(session: session)
                PadGrid(session: session)
                PadInspector(session: session)
                    .frame(width: 250)
                ChopRack(session: session)
                    .frame(maxWidth: .infinity)
            }
        }
        .padding(.horizontal, 26)
        .padding(.top, 40)
        .padding(.bottom, 22)
        .background(ChopBackground())
        .overlay {
            if session.buffer == nil {
                VStack(spacing: 10) {
                    ProgressView().controlSize(.large).tint(Palette.cream)
                    Text(session.loadError ?? "LOADING \(session.track.info.title.uppercased())")
                        .font(.system(size: 11, weight: .black))
                        .tracking(2)
                        .foregroundStyle(Palette.cream.opacity(0.7))
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(Palette.caseEdge.opacity(0.85))
                .transition(.opacity)
            }
        }
        .animation(Motion.snappy, value: session.buffer == nil)
        .focusable()
        .focusEffectDisabled()
        .focused($focused)
        .onKeyPress(phases: .down) { press in
            switch press.characters {
            case "0":
                session.zoomToFit()
                return .handled
            case "=", "+":
                session.zoom(by: 2)
                return .handled
            case "-":
                session.zoom(by: 0.5)
                return .handled
            default:
                break
            }
            if press.key == .leftArrow || press.key == .rightArrow {
                let direction: Double = press.key == .leftArrow ? -1 : 1
                if press.modifiers.contains(.option) {
                    session.nudge(samples: Int(direction))
                } else {
                    session.nudge(pixels: direction * (press.modifiers.contains(.shift) ? 10 : 1))
                }
                return .handled
            }
            if press.key == .return {
                session.assignSelected()
                return .handled
            }
            if let c = press.characters.lowercased().first, let pad = Self.padKeys[c] {
                session.hitPad(session.bank * 16 + pad, resetStart: press.modifiers.contains(.option))
                return .handled
            }
            return .ignored
        }
        // Keyed to the session, so reopening Chop quickly (before the old page has gone) still loads.
        .task(id: ObjectIdentifier(session)) { await session.load() }
        .onAppear { focused = true }
    }

    private var topBar: some View {
        HStack(spacing: 14) {
            BackButton(label: "Crate") { model.closeChop() }
            .padding(.leading, 60)

            VStack(alignment: .leading, spacing: 2) {
                Text(session.track.info.title)
                    .font(.system(size: 24, weight: .black))
                    .fontWidth(.condensed)
                    .lineLimit(1)
                Text(session.track.info.artist.uppercased())
                    .font(.system(size: 10, weight: .bold))
                    .tracking(2)
                    .opacity(0.55)
                    .lineLimit(1)
            }
            .foregroundStyle(Palette.cream)

            if let bpm = session.analysis?.bpm {
                chip(String(format: "%.0f BPM", bpm))
            }
            if let camelot = session.analysis?.camelot {
                CamelotChip(code: camelot, large: true)
            }

            Spacer()

            MIDIChip()

            Button { model.showRadio() } label: {
                HStack(spacing: 6) {
                    Image(systemName: "radio")
                    Text("RADIO").tracking(2)
                }
                .font(.system(size: 11, weight: .black))
                .foregroundStyle(Palette.cream)
                .padding(.horizontal, 14)
                .padding(.vertical, 10)
                .background(Capsule().fill(Color.white.opacity(0.08)))
            }
            .buttonStyle(BouncyButtonStyle())
            .help("The tape deck, with VU meters (⌘3)")

            Button { showExport.toggle() } label: {
                HStack(spacing: 6) {
                    Image(systemName: "square.and.arrow.up")
                    Text("EXPORT").tracking(2)
                }
                .font(.system(size: 11, weight: .black))
                .foregroundStyle(Palette.ink)
                .padding(.horizontal, 16)
                .padding(.vertical, 10)
                .background(Capsule().fill(Palette.red))
            }
            .buttonStyle(BouncyButtonStyle())
            .disabled(session.isBusy)
            .help("Export the pads as WAVs, or as an MPC Sample project")
            .popover(isPresented: $showExport, arrowEdge: .bottom) {
                ExportChoices(session: session) { showExport = false }
            }
        }
    }

    private func chip(_ text: String) -> some View {
        Text(text)
            .font(.system(size: 11, weight: .black).monospacedDigit())
            .foregroundStyle(Palette.cream)
            .padding(.horizontal, 8)
            .padding(.vertical, 3)
            .background(Capsule().strokeBorder(Palette.cream.opacity(0.3), lineWidth: 1))
    }
}

struct ChopBackground: View {
    var body: some View {
        ZStack {
            Palette.caseGrey
            GrainOverlay(image: Grain.dust)
            GrainOverlay(image: Grain.paper)
        }
    }
}

// MARK: - Waveform

/// Where the mouse is over the waveform. A class so the scroll-wheel monitor always reads the latest.
@MainActor
private final class HoverTracker {
    var x: Double?
    var width: Double = 1
}

private struct WaveformPanel: View {
    @ObservedObject var session: ChopSession
    @State private var grabbed: Grab?
    @State private var dragMoved = false
    @State private var zoomBase: (start: Double, end: Double)?
    @State private var hoverX: Double?
    @State private var tracker = HoverTracker()
    @State private var scrollMonitor: Any?

    var body: some View {
        VStack(spacing: 8) {
            VStack(spacing: 0) {
                Ruler(session: session, hoverX: hoverX)
                    .frame(height: 24)
                Rectangle().fill(Color.white.opacity(0.08)).frame(height: 1)
                GeometryReader { geo in
                    TimelineView(.animation(minimumInterval: nil, paused: !session.isPlaying)) { context in
                        Canvas { ctx, size in
                            draw(ctx, size: size, now: context.date)
                        }
                    }
                    .contentShape(Rectangle())
                    .onAppear { session.viewPixels = geo.size.width }
                    .onChange(of: geo.size.width) { _, w in session.viewPixels = w }
                    .onContinuousHover { phase in
                        switch phase {
                        case .active(let p):
                            hoverX = p.x
                            tracker.x = p.x
                            tracker.width = geo.size.width
                            (grab(at: p, size: geo.size) != nil ? NSCursor.resizeLeftRight : NSCursor.arrow).set()
                        case .ended:
                            hoverX = nil
                            tracker.x = nil
                            NSCursor.arrow.set()
                        }
                    }
                    .gesture(clickAndDrag(size: geo.size))
                    .simultaneousGesture(
                        SpatialTapGesture(count: 2).onEnded { tap in
                            doubleClick(at: tap.location.x, width: geo.size.width)
                        }
                    )
                    .gesture(
                        MagnifyGesture()
                            .onChanged { value in
                                if zoomBase == nil { zoomBase = (session.viewStart, session.viewEnd) }
                                guard let base = zoomBase else { return }
                                let ratio = value.startLocation.x / geo.size.width
                                session.setView(width: (base.end - base.start) / value.magnification,
                                                keeping: base.start + (base.end - base.start) * ratio, at: ratio)
                            }
                            .onEnded { _ in zoomBase = nil }
                    )
                }
            }
            .background(RoundedRectangle(cornerRadius: 14, style: .continuous).fill(Palette.ink))
            .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))

            Overview(session: session)
                .frame(height: 26)

            TransportRow(session: session)
        }
        .onAppear(perform: installScrollMonitor)
        .onDisappear {
            if let scrollMonitor { NSEvent.removeMonitor(scrollMonitor) }
            scrollMonitor = nil
        }
    }

    // MARK: Scroll wheel / trackpad

    /// Over the waveform, vertical scrolling zooms around the mouse and sideways
    /// (or Shift-) scrolling moves along the track, like a DAW arrangement.
    private func installScrollMonitor() {
        guard scrollMonitor == nil else { return }
        let tracker = tracker
        let session = session
        scrollMonitor = NSEvent.addLocalMonitorForEvents(matching: .scrollWheel) { event in
            guard let x = tracker.x else { return event }
            var dx = Double(event.scrollingDeltaX)
            var dy = Double(event.scrollingDeltaY)
            if event.modifierFlags.contains(.shift), dx == 0 {
                dx = dy
                dy = 0
            }
            let precise = event.hasPreciseScrollingDeltas
            let width = session.viewEnd - session.viewStart
            let ratio = x / max(tracker.width, 1)
            let anchor = session.viewStart + width * ratio
            if abs(dy) >= abs(dx) {
                session.setView(width: width * exp(-dy * (precise ? 0.01 : 0.12)), keeping: anchor, at: ratio)
            } else {
                let shift = -dx / max(tracker.width, 1) * width * (precise ? 1 : 8)
                session.setView(width: width, keeping: anchor + shift, at: ratio)
            }
            return nil
        }
    }

    // MARK: Geometry

    private func frame(atX x: Double, width: Double) -> Int {
        let fraction = session.viewStart + (session.viewEnd - session.viewStart) * min(max(x / width, 0), 1)
        return Int(fraction * Double(session.frameCount))
    }

    private func x(for frame: Int, width: Double) -> Double {
        guard session.frameCount > 0 else { return 0 }
        let fraction = Double(frame) / Double(session.frameCount)
        return (fraction - session.viewStart) / (session.viewEnd - session.viewStart) * width
    }

    private func marker(nearX x: Double, width: Double) -> Int? {
        session.markers.indices.min { abs(self.x(for: session.markers[$0], width: width) - x) < abs(self.x(for: session.markers[$1], width: width) - x) }
            .flatMap { abs(self.x(for: session.markers[$0], width: width) - x) < 7 ? $0 : nil }
    }

    private enum Grab: Equatable {
        case marker(Int)
        case pad(Int)
    }

    /// What's under the mouse: a pad's red flag along the bottom, a marker's yellow flag along the top,
    /// or a marker line anywhere. The nearest wins when they crowd together.
    private func grab(at p: CGPoint, size: CGSize) -> Grab? {
        let w = size.width, h = size.height
        if p.y > h - 22 {
            let hits = session.pads.compactMap { index, slot -> (Int, Double)? in
                let px = x(for: slot.start, width: w)
                return p.x >= px - 4 && p.x <= px + 30 ? (index, abs(px + 4 - p.x)) : nil
            }
            if let best = hits.min(by: { $0.1 < $1.1 }) { return .pad(best.0) }
        }
        if p.y < 18 {
            let hits = session.markers.enumerated().compactMap { i, m -> (Int, Double)? in
                let mx = x(for: m, width: w)
                return p.x >= mx - 4 && p.x <= mx + 22 ? (i, abs(mx + 4 - p.x)) : nil
            }
            if let best = hits.min(by: { $0.1 < $1.1 }) { return .marker(best.0) }
        }
        return marker(nearX: p.x, width: w).map { .marker($0) }
    }

    // MARK: Gestures

    /// Click to move the playhead (or pick a pad by its flag); drag a flag or marker line to move it;
    /// drag empty space to select a region.
    private func clickAndDrag(size: CGSize) -> some Gesture {
        let width = size.width
        return DragGesture(minimumDistance: 0)
            .onChanged { g in
                if grabbed == nil && !dragMoved {
                    grabbed = grab(at: g.startLocation, size: size)
                }
                if abs(g.translation.width) > 3 { dragMoved = true }
                guard dragMoved else { return }
                switch grabbed {
                case .marker(let m):
                    session.moveMarker(m, to: frame(atX: g.location.x, width: width))
                case .pad(let index):
                    session.selectedPad = index
                    session.movePadStart(index, to: frame(atX: g.location.x, width: width))
                case nil:
                    let a = frame(atX: g.startLocation.x, width: width), b = frame(atX: g.location.x, width: width)
                    session.selection = min(a, b)..<max(a, b)
                }
            }
            .onEnded { g in
                if !dragMoved {
                    if case .pad(let index) = grabbed, let slot = session.pads[index] {
                        session.selectedPad = index
                        session.bank = index / 16
                        session.seek(to: slot.start)
                    } else {
                        session.selection = nil
                        session.selectSlice(at: frame(atX: g.location.x, width: width))
                    }
                } else if case .pad(let index) = grabbed, let slot = session.pads[index] {
                    session.seek(to: slot.start)
                } else if grabbed == nil, let selection = session.selection {
                    session.seek(to: selection.lowerBound)
                }
                grabbed = nil
                dragMoved = false
            }
    }

    /// Double-click empty space to add a marker, or a marker to remove it.
    private func doubleClick(at x: Double, width: Double) {
        if let m = marker(nearX: x, width: width) {
            withAnimation(Motion.snappy) { session.removeMarker(m) }
        } else {
            withAnimation(Motion.snappy) { session.addMarker(at: frame(atX: x, width: width)) }
        }
    }

    // MARK: Drawing

    private func draw(_ ctx: GraphicsContext, size: CGSize, now: Date) {
        guard let peaks = session.peaks, session.frameCount > 0 else { return }
        let w = Double(size.width), h = Double(size.height)
        let mid = h / 2
        let slices = session.slices

        // Selected slice and the selected pad's region.
        if let i = session.selectedSlice, slices.indices.contains(i) {
            let s = slices[i]
            let x0 = x(for: s.lowerBound, width: w), x1 = x(for: s.upperBound, width: w)
            ctx.fill(Path(CGRect(x: x0, y: 0, width: x1 - x0, height: h)), with: .color(Palette.cream.opacity(0.1)))
        }
        if let slot = session.pads[session.selectedPad] {
            let x0 = x(for: slot.start, width: w), x1 = x(for: slot.end, width: w)
            ctx.fill(Path(CGRect(x: x0, y: h - 5, width: x1 - x0, height: 5)), with: .color(Palette.red))
        }

        // Dragged-out selection.
        if let sel = session.selection {
            let x0 = x(for: sel.lowerBound, width: w), x1 = x(for: sel.upperBound, width: w)
            ctx.fill(Path(CGRect(x: x0, y: 0, width: x1 - x0, height: h)), with: .color(Palette.cream.opacity(0.16)))
            ctx.fill(Path(CGRect(x: x0, y: 0, width: 1, height: h)), with: .color(Palette.cream.opacity(0.7)))
            ctx.fill(Path(CGRect(x: x1 - 1, y: 0, width: 1, height: h)), with: .color(Palette.cream.opacity(0.7)))
        }

        // Looping region: a green band across the top and a soft wash.
        if let loop = session.loopRange {
            let x0 = x(for: loop.lowerBound, width: w), x1 = x(for: loop.upperBound, width: w)
            let green = Color(hex: 0x3FD15A)
            ctx.fill(Path(CGRect(x: x0, y: 0, width: x1 - x0, height: h)), with: .color(green.opacity(0.1)))
            ctx.fill(Path(CGRect(x: x0, y: 0, width: x1 - x0, height: 4)), with: .color(green))
            ctx.fill(Path(CGRect(x: x0, y: 0, width: 1.5, height: h)), with: .color(green.opacity(0.8)))
            ctx.fill(Path(CGRect(x: x1 - 1.5, y: 0, width: 1.5, height: h)), with: .color(green.opacity(0.8)))
        }

        // Hover line.
        if let hx = hoverX {
            ctx.fill(Path(CGRect(x: hx, y: 0, width: 1, height: h)), with: .color(Palette.cream.opacity(0.25)))
        }

        // Waveform, one bar every 2 pixels.
        let selected = session.selectedSlice.flatMap { slices.indices.contains($0) ? slices[$0] : nil }
        var px = 0.0
        while px < w {
            let f0 = frame(atX: px, width: w), f1 = frame(atX: px + 2, width: w)
            let level = Double(min(1, peaks.level(from: f0, to: max(f1, f0 + 1))))
            let barH = max(1, level * (h * 0.44))
            let inSelection = selected?.contains(f0) ?? false
            ctx.fill(Path(CGRect(x: px, y: mid - barH, width: 1.4, height: barH * 2)),
                     with: .color(inSelection ? Palette.red : Palette.cream.opacity(0.78)))
            px += 2
        }

        // Markers with numbered flags, 1 for the first marker (drag a flag or line to move it).
        var lastTab = -100.0
        for (i, m) in session.markers.enumerated() {
            let mx = x(for: m, width: w)
            guard mx >= -20, mx <= w + 20 else { continue }
            ctx.fill(Path(CGRect(x: mx - 0.75, y: 0, width: 1.5, height: h)), with: .color(Palette.label.opacity(0.9)))
            guard mx - lastTab >= 24 else { continue }
            lastTab = mx
            let label = Text("\(i + 1)").font(.system(size: 9, weight: .black)).foregroundColor(Palette.ink)
            ctx.fill(Path(roundedRect: CGRect(x: mx, y: 0, width: 20, height: 14), cornerRadius: 3), with: .color(Palette.label))
            ctx.draw(label, at: CGPoint(x: mx + 10, y: 7))
        }

        // Pad tags at each pad's start point.
        var lastPadTag = -100.0
        for (index, slot) in session.pads.sorted(by: { $0.value.start < $1.value.start }) {
            let px = x(for: slot.start, width: w)
            guard px >= -30, px <= w + 30, px - lastPadTag >= 30 else { continue }
            lastPadTag = px
            let selectedPad = index == session.selectedPad
            ctx.fill(Path(roundedRect: CGRect(x: px, y: h - 18, width: 28, height: 14), cornerRadius: 3),
                     with: .color(selectedPad ? Palette.cream : Palette.red))
            ctx.draw(Text(session.padName(index)).font(.system(size: 8, weight: .black)).foregroundColor(Palette.ink),
                     at: CGPoint(x: px + 14, y: h - 11))
        }

        // Playhead: moving while playing, resting where it was left while paused.
        let hx = x(for: session.playhead, width: w)
        if hx >= -2, hx <= w + 2 {
            ctx.fill(Path(CGRect(x: hx - 1, y: 0, width: 2, height: h)), with: .color(session.isPlaying ? Palette.red : Palette.cream))
            var cap = Path()
            cap.move(to: CGPoint(x: hx - 6, y: 0))
            cap.addLine(to: CGPoint(x: hx + 6, y: 0))
            cap.addLine(to: CGPoint(x: hx, y: 8))
            cap.closeSubpath()
            ctx.fill(cap, with: .color(session.isPlaying ? Palette.red : Palette.cream))
        }
    }
}

/// Time ruler. Like a DAW's: drag down to zoom in, up to zoom out, sideways to scroll;
/// the spot you grabbed stays under the mouse. Double-click to see the whole track.
private struct Ruler: View {
    @ObservedObject var session: ChopSession
    var hoverX: Double?
    @State private var dragBase: (width: Double, anchor: Double)?

    /// A magnifying glass, like a DAW's zoom cursor.
    private static let magnifier: NSCursor = {
        let config = NSImage.SymbolConfiguration(pointSize: 15, weight: .bold)
            .applying(NSImage.SymbolConfiguration(paletteColors: [.white]))
        let glyph = NSImage(systemSymbolName: "magnifyingglass", accessibilityDescription: "Zoom")?
            .withSymbolConfiguration(config) ?? NSImage()
        let image = NSImage(size: NSSize(width: 20, height: 20), flipped: false) { rect in
            NSGraphicsContext.current?.saveGraphicsState()
            let shadow = NSShadow()
            shadow.shadowColor = .black
            shadow.shadowBlurRadius = 2
            shadow.set()
            glyph.draw(in: rect.insetBy(dx: 2, dy: 2))
            NSGraphicsContext.current?.restoreGraphicsState()
            return true
        }
        return NSCursor(image: image, hotSpot: NSPoint(x: 8, y: 8))
    }()

    var body: some View {
        GeometryReader { geo in
            Canvas { ctx, size in draw(ctx, size: size) }
                .contentShape(Rectangle())
                .onContinuousHover { phase in
                    if case .active = phase { Self.magnifier.set() } else { NSCursor.arrow.set() }
                }
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { g in
                            let w = max(geo.size.width, 1)
                            if dragBase == nil {
                                let width = session.viewEnd - session.viewStart
                                dragBase = (width, session.viewStart + width * g.startLocation.x / w)
                            }
                            guard let base = dragBase else { return }
                            session.setView(width: base.width * exp(-g.translation.height / 110),
                                            keeping: base.anchor, at: g.location.x / w)
                            Self.magnifier.set()
                        }
                        .onEnded { _ in dragBase = nil }
                )
                .simultaneousGesture(TapGesture(count: 2).onEnded { session.zoomToFit() })
        }
        .help("Drag down to zoom in, up to zoom out, sideways to scroll. Double-click for the whole track.")
    }

    private func draw(_ ctx: GraphicsContext, size: CGSize) {
        guard session.frameCount > 0 else { return }
        let w = Double(size.width), h = Double(size.height)
        let total = Double(session.frameCount) / session.sampleRate
        let t0 = session.viewStart * total, t1 = session.viewEnd * total
        let span = max(t1 - t0, 0.0001)
        let pxPerSecond = w / span

        // Biggest-sensible label spacing that keeps labels ~80 px apart.
        let steps = [0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60]
        let step = steps.first { $0 * pxPerSecond >= 80 } ?? 60
        let minor = step / 5

        var t = (t0 / minor).rounded(.down) * minor
        while t <= t1 {
            let x = (t - t0) * pxPerSecond
            let isMajor = abs((t / step).rounded() * step - t) < minor / 2
            ctx.fill(Path(CGRect(x: x, y: h - (isMajor ? 9 : 4), width: 1, height: isMajor ? 9 : 4)),
                     with: .color(Palette.cream.opacity(isMajor ? 0.6 : 0.25)))
            if isMajor {
                ctx.draw(Text(label(t, step: step)).font(.system(size: 9, weight: .bold).monospacedDigit())
                            .foregroundColor(Palette.cream.opacity(0.6)),
                         at: CGPoint(x: x + 3, y: 8), anchor: .leading)
            }
            t += minor
        }

        if let hx = hoverX {
            let time = t0 + hx / pxPerSecond
            let text = Text(label(time, step: 0.01)).font(.system(size: 9, weight: .black).monospacedDigit()).foregroundColor(Palette.ink)
            let boxW = 46.0
            let bx = min(max(hx - boxW / 2, 0), w - boxW)
            ctx.fill(Path(roundedRect: CGRect(x: bx, y: 2, width: boxW, height: 14), cornerRadius: 3), with: .color(Palette.cream))
            ctx.draw(text, at: CGPoint(x: bx + boxW / 2, y: 9))
        }
    }

    private func label(_ seconds: Double, step: Double) -> String {
        let m = Int(seconds) / 60
        let s = seconds - Double(m * 60)
        if step >= 1 { return String(format: "%d:%02d", m, Int(s)) }
        let tenths = step * 10
        if step >= 0.1, abs(tenths - tenths.rounded()) < 0.001 { return String(format: "%d:%04.1f", m, s) }
        return String(format: "%d:%05.2f", m, s)
    }
}

/// Play / pause and the clock, with the brushed start · stop key from the deck.
private struct TransportRow: View {
    @ObservedObject var session: ChopSession

    /// Steel ◀◀ / ▶▶ keys: one bar per press, four with Shift.
    private func skipKey(_ symbol: String, bars: Int) -> some View {
        Button {
            session.skip(bars: NSEvent.modifierFlags.contains(.shift) ? bars * 4 : bars)
        } label: {
            Image(systemName: symbol)
                .font(.system(size: 11, weight: .black))
                .foregroundStyle(Palette.ink.opacity(0.8))
                .frame(width: 44, height: 38)
                .background(BrushedSteel())
                .clipShape(RoundedRectangle(cornerRadius: 4, style: .continuous))
                .overlay(RoundedRectangle(cornerRadius: 4, style: .continuous).strokeBorder(Color.black.opacity(0.35), lineWidth: 1))
                .background(RoundedRectangle(cornerRadius: 5).fill(Color.black).offset(y: 3))
        }
        .buttonStyle(BouncyButtonStyle(pressedScale: 0.92))
        .help(bars < 0 ? "Back one bar (⇧ four)" : "Forward one bar (⇧ four)")
        .contextMenu { MIDILearnItem(target: .transport(bars < 0 ? .rewind : .forward)) }
    }

    private func zoomButton(_ title: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 11, weight: .black))
                .foregroundStyle(Palette.cream)
                .frame(minWidth: 26)
                .padding(.horizontal, 8)
                .padding(.vertical, 6)
                .background(Capsule().fill(Color.white.opacity(0.08)))
        }
        .buttonStyle(BouncyButtonStyle())
    }

    var body: some View {
        HStack(spacing: 14) {
            skipKey("backward.fill", bars: -1)
            Button { session.togglePlay() } label: {
                ZStack(alignment: .topTrailing) {
                    Text("start · stop")
                        .font(.system(size: 14))
                        .foregroundStyle(Palette.ink.opacity(0.85))
                        .frame(width: 128, height: 38)
                        .background(BrushedSteel())
                        .clipShape(RoundedRectangle(cornerRadius: 4, style: .continuous))
                        .overlay(RoundedRectangle(cornerRadius: 4, style: .continuous).strokeBorder(Color.black.opacity(0.35), lineWidth: 1))
                    Circle()
                        .fill(session.isPlaying ? Palette.red : Palette.ink.opacity(0.25))
                        .frame(width: 5, height: 5)
                        .shadow(color: session.isPlaying ? Palette.red : .clear, radius: 3)
                        .padding(6)
                }
                .background(RoundedRectangle(cornerRadius: 5).fill(Color.black).offset(y: 3))
            }
            .buttonStyle(BouncyButtonStyle(pressedScale: 0.95))
            .help("Play / pause (space)")
            .contextMenu { MIDILearnItem(target: .transport(.playPause)) }
            skipKey("forward.fill", bars: 1)

            TimelineView(.periodic(from: .now, by: 0.05)) { _ in
                HStack(spacing: 6) {
                    Text(session.timestamp(session.playhead))
                        .foregroundStyle(Palette.cream)
                    Text("/ \(session.timestamp(session.frameCount))")
                        .foregroundStyle(Palette.cream.opacity(0.45))
                }
                .font(.system(size: 15, weight: .bold).monospacedDigit())
            }

            DirectionLever(value: $session.direction)
                .padding(.leading, 10)

            Spacer()

            if session.selection != nil {
                zoomButton("ZOOM TO SELECTION") { session.zoomToSelection() }
                    .help("Zoom to the dragged-out region")
                    .transition(.scale.combined(with: .opacity))
            }
            zoomButton("FIT") { session.zoomToFit() }
                .help("Show the whole track (0)")
            zoomButton("−") { session.zoom(by: 0.5) }
                .help("Zoom out (−), or drag up on the ruler")
            zoomButton("+") { session.zoom(by: 2) }
                .help("Zoom in (=), drag down on the ruler, scroll, or pinch")
        }
    }
}

/// A steel lever for speed and direction: right is normal forward, the middle is stopped,
/// left is full-speed reverse, and everything in between glides. Double-click to return to forward.
private struct DirectionLever: View {
    @Binding var value: Double
    @State private var dragStart: Double?

    private let width: CGFloat = 220
    private let handle: CGFloat = 34

    var body: some View {
        VStack(spacing: 3) {
            GeometryReader { _ in
                let travel = width - handle
                let x = CGFloat((min(max(value, -1), 1) + 1) / 2) * travel
                ZStack(alignment: .leading) {
                    // Plate and slot.
                    RoundedRectangle(cornerRadius: 4, style: .continuous)
                        .fill(Color(hex: 0x1E1F21))
                    RoundedRectangle(cornerRadius: 2)
                        .fill(Color.black)
                        .frame(height: 4)
                        .padding(.horizontal, handle / 2)
                    // Detent marks: reverse, stop, forward.
                    ForEach([-1.0, 0.0, 1.0], id: \.self) { d in
                        Rectangle()
                            .fill(d == 0 ? Palette.red : Palette.cream.opacity(0.5))
                            .frame(width: 2, height: 10)
                            .offset(x: CGFloat((d + 1) / 2) * travel + handle / 2 - 1)
                    }
                    // Steel handle with grip lines.
                    HStack(spacing: 3) {
                        ForEach(0..<3, id: \.self) { _ in
                            Rectangle().fill(Color.black.opacity(0.35)).frame(width: 1)
                        }
                    }
                    .padding(.vertical, 5)
                    .frame(width: handle, height: 26)
                    .background(BrushedSteel())
                    .clipShape(RoundedRectangle(cornerRadius: 3))
                    .overlay(RoundedRectangle(cornerRadius: 3).strokeBorder(Color.black.opacity(0.4), lineWidth: 1))
                    .shadow(color: .black.opacity(0.4), radius: 2, y: 2)
                    .offset(x: x)
                }
                .contentShape(Rectangle())
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { g in
                            if dragStart == nil { dragStart = value }
                            var v = (dragStart ?? 1) + Double(g.translation.width / travel) * 2
                            v = min(max(v, -1), 1)
                            // Gentle detents at reverse, stop and forward.
                            for d in [-1.0, 0.0, 1.0] where abs(v - d) < 0.04 { v = d }
                            value = v
                        }
                        .onEnded { _ in dragStart = nil }
                )
                .simultaneousGesture(TapGesture(count: 2).onEnded { value = 1 })
            }
            .frame(width: width, height: 30)

            HStack {
                Text("◀ REV").opacity(value < 0 ? 1 : 0.45)
                Spacer()
                Text(value == 0 ? "STOP" : String(format: "%@ ×%.2f", value < 0 ? "REV" : "FWD", abs(value)))
                    .monospacedDigit()
                Spacer()
                Text("FWD ▶").opacity(value > 0 ? 1 : 0.45)
            }
            .font(.system(size: 8, weight: .black))
            .tracking(1)
            .foregroundStyle(Palette.cream.opacity(0.7))
            .frame(width: width)
        }
        .help("Drag right for forward, left for reverse, middle to stop. Double-click for normal forward.")
    }
}

/// The whole track in miniature; drag to move the zoomed view.
private struct Overview: View {
    @ObservedObject var session: ChopSession

    var body: some View {
        GeometryReader { geo in
            Canvas { ctx, size in
                guard let peaks = session.peaks, session.frameCount > 0 else { return }
                let w = Double(size.width), h = Double(size.height)
                var px = 0.0
                while px < w {
                    let f0 = Int(px / w * Double(session.frameCount)), f1 = Int((px + 2) / w * Double(session.frameCount))
                    let level = Double(min(1, peaks.level(from: f0, to: f1)))
                    let barH = max(0.5, level * h * 0.45)
                    ctx.fill(Path(CGRect(x: px, y: h / 2 - barH, width: 1.2, height: barH * 2)), with: .color(Palette.cream.opacity(0.35)))
                    px += 2
                }
                for m in session.markers {
                    let mx = Double(m) / Double(session.frameCount) * w
                    ctx.fill(Path(CGRect(x: mx, y: 0, width: 1, height: h)), with: .color(Palette.label.opacity(0.5)))
                }
                let x0 = session.viewStart * w, x1 = session.viewEnd * w
                ctx.stroke(Path(roundedRect: CGRect(x: x0, y: 1, width: max(4, x1 - x0), height: h - 2), cornerRadius: 4),
                           with: .color(Palette.cream), lineWidth: 1.5)
            }
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0).onChanged { g in
                session.pan(toCenter: g.location.x / geo.size.width)
            })
        }
        .background(RoundedRectangle(cornerRadius: 8).fill(Color.black.opacity(0.25)))
        .help("Drag to move around the track")
    }
}

// MARK: - Tools

private struct ChopTools: View {
    @ObservedObject var session: ChopSession
    @Namespace private var pill

    var body: some View {
        HStack(spacing: 14) {
            Text("AUTO CHOP")
                .font(.system(size: 10, weight: .black))
                .tracking(2)
                .foregroundStyle(Palette.cream.opacity(0.5))

            HStack(spacing: 2) {
                ForEach(ChopMode.allCases) { mode in
                    let on = session.mode == mode
                    Text(mode.label)
                        .font(.system(size: 10, weight: .black))
                        .tracking(1)
                        .foregroundStyle(on ? Palette.ink : Palette.cream.opacity(0.65))
                        .padding(.horizontal, 12)
                        .padding(.vertical, 7)
                        .background {
                            if on { Capsule().fill(Palette.cream).matchedGeometryEffect(id: "mode", in: pill) }
                        }
                        .contentShape(Capsule())
                        .onTapGesture { withAnimation(Motion.jelly) { session.mode = mode } }
                }
            }
            .padding(3)
            .background(Capsule().fill(Color.white.opacity(0.07)))

            Group {
                switch session.mode {
                case .transient:
                    HStack(spacing: 8) {
                        Text("FEWER").font(.system(size: 9, weight: .bold)).fixedSize()
                        Slider(value: $session.sensitivity, in: 0...1).frame(width: 120).tint(Palette.red)
                        Text("MORE").font(.system(size: 9, weight: .bold)).fixedSize()
                    }
                case .beat:
                    options([1, 2, 4, 8], selection: $session.beatsPerSlice) { "\($0) BEAT\($0 == 1 ? "" : "S")" }
                        .opacity(session.analysis?.bpm == nil ? 0.4 : 1)
                case .equal:
                    options([8, 16, 32, 64], selection: $session.equalCount) { "\($0)" }
                }
            }
            .foregroundStyle(Palette.cream.opacity(0.7))
            .transition(.opacity)

            Button { session.autoChop() } label: {
                Text(session.isBusy ? "CHOPPING…" : "CHOP")
                    .font(.system(size: 11, weight: .black))
                    .tracking(2)
                    .foregroundStyle(Palette.ink)
                    .padding(.horizontal, 16)
                    .padding(.vertical, 8)
                    .background(Capsule().fill(Palette.label))
            }
            .buttonStyle(BouncyButtonStyle())
            .disabled(session.isBusy || session.buffer == nil)

            Spacer()

            Text("\(session.slices.count) SLICES")
                .font(.system(size: 11, weight: .black).monospacedDigit())
                .tracking(1)
                .foregroundStyle(Palette.cream)
                .contentTransition(.numericText())

            smallButton("CLEAR") { session.clearMarkers() }
                .help("Remove every marker")

        }
        .animation(Motion.snappy, value: session.mode)
    }

    private func options(_ values: [Int], selection: Binding<Int>, label: @escaping (Int) -> String) -> some View {
        HStack(spacing: 4) {
            ForEach(values, id: \.self) { v in
                Text(label(v))
                    .font(.system(size: 9, weight: .black))
                    .foregroundStyle(selection.wrappedValue == v ? Palette.ink : Palette.cream.opacity(0.7))
                    .padding(.horizontal, 8)
                    .padding(.vertical, 5)
                    .background(Capsule().fill(selection.wrappedValue == v ? Palette.cream : Color.white.opacity(0.06)))
                    .onTapGesture { withAnimation(Motion.jelly) { selection.wrappedValue = v } }
            }
        }
    }

    private func smallButton(_ title: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 11, weight: .black))
                .foregroundStyle(Palette.cream)
                .frame(minWidth: 26)
                .padding(.horizontal, 8)
                .padding(.vertical, 6)
                .background(Capsule().fill(Color.white.opacity(0.08)))
        }
        .buttonStyle(BouncyButtonStyle())
    }
}

/// The two ways out: a plain folder of WAVs, or a project the MPC Sample opens with every chop on its pad.
private struct ExportChoices: View {
    @ObservedObject var session: ChopSession
    let dismiss: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            choice("FOLDER OF WAVS", "A01 … H16, one file per pad") { session.exportSlices() }
            choice("MPC SAMPLE PROJECT", session.hasMPCTemplate ? ".xpj with each chop on its pad" : "Needs an empty project saved from your MPC") {
                session.exportMPCProject()
            }
            choice("SERATO DJ", "Pads 1–8 → hot cues, loop memories → saved loops") { session.exportSerato() }
                .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(Palette.red.opacity(0.8), lineWidth: 1))
            Button(session.hasMPCTemplate ? "Change MPC template…" : "Choose MPC template…") {
                dismiss()
                session.chooseMPCTemplate()
            }
            .buttonStyle(.link)
            .font(.system(size: 11))
        }
        .padding(14)
        .frame(width: 280)
    }

    private func choice(_ title: String, _ detail: String, action: @escaping () -> Void) -> some View {
        Button {
            dismiss()
            action()
        } label: {
            VStack(alignment: .leading, spacing: 3) {
                Text(title).font(.system(size: 12, weight: .black)).tracking(1)
                Text(detail).font(.system(size: 11)).opacity(0.6)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(10)
            .background(RoundedRectangle(cornerRadius: 8).fill(Color.primary.opacity(0.06)))
        }
        .buttonStyle(BouncyButtonStyle(pressedScale: 0.96))
    }
}

// MARK: - Banks & pads

private struct BankKeys: View {
    @ObservedObject var session: ChopSession

    private func bankKey(_ b: Int) -> some View {
        let on = session.bank == b
        let used = (b * 16..<b * 16 + 16).contains { session.pads[$0] != nil }
        return Button { withAnimation(Motion.jelly) { session.bank = b } } label: {
            ZStack(alignment: .topTrailing) {
                Text(String(ChopSession.bankLetters[b]))
                    .font(.system(size: 15, weight: .black))
                    .foregroundStyle(on ? Palette.ink : Palette.cream)
                    .frame(width: 42, height: 36)
                    .background(RoundedRectangle(cornerRadius: 7).fill(on ? Palette.label : Color.white.opacity(0.07)))
                if used {
                    Circle().fill(Palette.red).frame(width: 6, height: 6).padding(5)
                }
            }
        }
        .buttonStyle(BouncyButtonStyle(pressedScale: 0.85))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("BANK")
                .font(.system(size: 9, weight: .black))
                .tracking(2)
                .foregroundStyle(Palette.cream.opacity(0.5))
            VStack(alignment: .leading, spacing: 6) {
                ForEach(0..<4, id: \.self) { row in
                    HStack(spacing: 6) {
                        bankKey(row * 2)
                        bankKey(row * 2 + 1)
                    }
                }
            }
        }
    }
}

/// Computer-keyboard letter per pad within a bank (pad 1 = Z … pad 16 = 4).
private let padKeyLetters: [Int: String] = [
    0: "Z", 1: "X", 2: "C", 3: "V", 4: "A", 5: "S", 6: "D", 7: "F",
    8: "Q", 9: "W", 10: "E", 11: "R", 12: "1", 13: "2", 14: "3", 15: "4",
]

private struct PadGrid: View {
    @ObservedObject var session: ChopSession

    /// MPC layout: pads 13–16 on top, 1–4 on the bottom.
    private let rows = [[12, 13, 14, 15], [8, 9, 10, 11], [4, 5, 6, 7], [0, 1, 2, 3]]

    var body: some View {
        VStack(spacing: 9) {
            ForEach(rows, id: \.self) { row in
                HStack(spacing: 9) {
                    ForEach(row, id: \.self) { p in
                        let index = session.bank * 16 + p
                        PadButton(session: session, index: index)
                    }
                }
            }
        }
        // Pads 1–8 export to Serato as hot cues: a thin red frame around the bottom two rows.
        .overlay(alignment: .bottom) {
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(Palette.red, lineWidth: 1)
                .frame(height: 60 * 2 + 9 + 10)
                .padding(.horizontal, -5)
                .padding(.bottom, -5)
                .overlay(alignment: .topLeading) {
                    Text("SERATO CUES")
                        .font(.system(size: 7, weight: .black))
                        .tracking(1)
                        .foregroundStyle(Palette.red)
                        .padding(.horizontal, 4)
                        .background(Palette.caseEdge)
                        .offset(x: 10, y: -5)
                }
                .allowsHitTesting(false)
        }
        .padding(12)
        .background(RoundedRectangle(cornerRadius: 16, style: .continuous).fill(Palette.caseEdge))
    }
}

private struct PadButton: View {
    @ObservedObject var session: ChopSession
    let index: Int
    @State private var flash = 0.0
    @State private var pressed = false

    private var slot: PadSlot? { session.pads[index] }

    var body: some View {
        let selected = session.selectedPad == index
        ZStack(alignment: .topLeading) {
            RoundedRectangle(cornerRadius: 9, style: .continuous)
                .fill(LinearGradient(colors: slot == nil
                                     ? [Color(hex: 0x34363A), Color(hex: 0x2A2C2F)]
                                     : [Palette.red.opacity(0.95), Color(hex: 0xC23A22)],
                                     startPoint: .top, endPoint: .bottom))
            RoundedRectangle(cornerRadius: 9, style: .continuous)
                .fill(Palette.label.opacity(flash))
            Text(session.padName(index))
                .font(.system(size: 9, weight: .black).monospacedDigit())
                .foregroundStyle(slot == nil ? Palette.cream.opacity(0.45) : Palette.ink.opacity(0.75))
                .padding(7)
            if slot != nil {
                Text(slot.map { String(format: "%.1fs", Double($0.end - $0.start) / session.sampleRate) } ?? "")
                    .font(.system(size: 17, weight: .black).monospacedDigit())
                    .fontWidth(.condensed)
                    .foregroundStyle(Palette.ink)
                    .opacity(session.revealedPads.contains(index) ? 1 : 0.3)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .transition(.scale(scale: 0.3).combined(with: .opacity))
            }
        }
        .frame(width: 68, height: 60)
        .overlay {
            // Which computer key plays this pad, until the pad is first pressed.
            if !session.revealedPads.contains(index), let key = padKeyLetters[index % 16] {
                RoundedRectangle(cornerRadius: 9, style: .continuous)
                    .fill(Color.black.opacity(0.3))
                    .overlay(Text(key)
                        .font(.system(size: 28, weight: .black))
                        .foregroundStyle(Palette.cream.opacity(0.95))
                        .shadow(color: .black.opacity(0.5), radius: 1.5))
                    .allowsHitTesting(false)
                    .transition(.opacity)
            }
        }
        .animation(.easeOut(duration: 0.25), value: session.revealedPads.contains(index))
        .overlay(
            RoundedRectangle(cornerRadius: 9, style: .continuous)
                .strokeBorder(selected ? Palette.cream : Color.black.opacity(0.4), lineWidth: selected ? 2 : 1)
        )
        .shadow(color: .black.opacity(0.4), radius: pressed ? 1 : 3, y: pressed ? 1 : 3)
        .scaleEffect(pressed ? 0.92 : 1)
        .animation(.spring(response: 0.2, dampingFraction: 0.45), value: pressed)
        .animation(Motion.bouncy, value: selected)
        .contentShape(Rectangle())
        .onLongPressGesture(minimumDuration: 0, pressing: { pressed = $0 }, perform: {})
        .simultaneousGesture(TapGesture().onEnded { session.hitPad(index, resetStart: NSEvent.modifierFlags.contains(.option)) })
        .help(ChopSession.isSeratoCue(index) ? "Exports to Serato as hot cue \(index % 16 + 1)" : "")
        .contextMenu {
            if slot != nil {
                Button("Clear \(session.padName(index))") {
                    session.selectedPad = index
                    session.clearPad()
                }
                Divider()
            }
            MIDILearnItem(target: .pad(index))
        }
        .onChange(of: session.hit.count) { _, _ in
            guard session.hit.pad == index else { return }
            flash = 0.8
            withAnimation(.easeOut(duration: 0.45)) { flash = 0 }
        }
    }
}

private struct PadInspector: View {
    @ObservedObject var session: ChopSession

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            VStack(alignment: .leading, spacing: 2) {
                Text("PAD")
                    .font(.system(size: 9, weight: .black))
                    .tracking(2)
                    .opacity(0.5)
                Text(session.padName(session.selectedPad))
                    .font(.system(size: 40, weight: .black))
                    .fontWidth(.condensed)
                    .contentTransition(.numericText())
                Text(detail)
                    .font(.system(size: 11, weight: .semibold).monospacedDigit())
                    .opacity(0.6)
            }
            .foregroundStyle(Palette.cream)
            .animation(Motion.snappy, value: session.selectedPad)

            HStack(spacing: 8) {
                action("ASSIGN SLICE ↩", fill: Palette.label) { session.assignSelected() }
                    .help("Put the selected slice on this pad, then move on to the next")
                action("AUTO MAP", fill: Palette.cream) { session.autoMap() }
                    .help("Every slice in order, starting at A01")
            }
            HStack(spacing: 8) {
                action("CLEAR PAD", fill: Color.white.opacity(0.1), text: Palette.cream) { session.clearPad() }
                action("CLEAR BANK", fill: Color.white.opacity(0.1), text: Palette.cream) { session.clearBank() }
            }

            HStack(spacing: 8) {
                action("◀", fill: Color.white.opacity(0.1), text: Palette.cream) {
                    session.nudge(pixels: NSEvent.modifierFlags.contains(.shift) ? -10 : -1)
                }
                Text("NUDGE START")
                    .font(.system(size: 9, weight: .black))
                    .tracking(1.5)
                    .foregroundStyle(Palette.cream.opacity(0.6))
                action("▶", fill: Color.white.opacity(0.1), text: Palette.cream) {
                    session.nudge(pixels: NSEvent.modifierFlags.contains(.shift) ? 10 : 1)
                }
                TimelineView(.periodic(from: .now, by: 0.25)) { _ in
                    Text(session.nudgeStepLabel)
                        .font(.system(size: 10, weight: .semibold).monospacedDigit())
                        .foregroundStyle(Palette.cream.opacity(0.45))
                }
            }
            .help("← / → nudge by 1 pixel at the current zoom, ⇧ for 10, ⌥ for a single sample")

            Text(session.status ?? "Press play (space), then tap an empty pad to drop a start point. The next pad you tap ends the one before. Tap a filled pad to jump back to it; ⌥-tap to move its start. Keys Z–V, A–F, Q–R, 1–4 are pads 1–16.")
                .font(.system(size: 11))
                .foregroundStyle(Palette.cream.opacity(0.55))
                .fixedSize(horizontal: false, vertical: true)
                .frame(maxWidth: 280, alignment: .leading)
                .animation(Motion.snappy, value: session.status)

            Text("\(session.pads.count) OF 128 PADS FILLED")
                .font(.system(size: 9, weight: .black))
                .tracking(2)
                .foregroundStyle(Palette.cream.opacity(0.4))
                .contentTransition(.numericText())
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var detail: String {
        guard let slot = session.pads[session.selectedPad] else { return "Empty" }
        let rate = session.sampleRate
        return "\(stamp(Double(slot.start) / rate)) – \(stamp(Double(slot.end) / rate))  ·  \(String(format: "%.2fs", Double(slot.end - slot.start) / rate))"
    }

    private func stamp(_ seconds: Double) -> String {
        String(format: "%d:%06.3f", Int(seconds) / 60, seconds.truncatingRemainder(dividingBy: 60))
    }

    private func action(_ title: String, fill: Color, text: Color = Palette.ink, perform: @escaping () -> Void) -> some View {
        Button(action: perform) {
            Text(title)
                .font(.system(size: 10, weight: .black))
                .tracking(1)
                .foregroundStyle(text)
                .padding(.horizontal, 12)
                .padding(.vertical, 8)
                .background(Capsule().fill(fill))
        }
        .buttonStyle(BouncyButtonStyle())
    }
}
