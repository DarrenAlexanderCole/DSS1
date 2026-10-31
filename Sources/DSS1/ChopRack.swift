import AppKit
import SwiftUI

/// The pedal on the right of the chop station, after a Chroma Console:
/// a bar looper on top, eight coloured effect knobs, and a turntable pitch fader with a range button.
struct ChopRack: View {
    @ObservedObject var session: ChopSession

    private static let pedal = Color(hex: 0xECE8DF)
    private static let pedalLine = Color(hex: 0xCFC9BC)

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            VStack(alignment: .leading, spacing: 12) {
                LooperSection(session: session)
                Rectangle().fill(Self.pedalLine).frame(height: 1)
                    .padding(.top, 2)
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 6), count: 4), spacing: 6) {
                    ForEach(FXKind.allCases) { kind in
                        FXKnob(kind: kind, state: binding(kind), selected: session.selectedFX == kind) { session.selectedFX = kind }
                    }
                }
            }
            Rectangle().fill(Self.pedalLine).frame(width: 1)
            VStack(spacing: 6) {
                PitchFader(pitch: $session.pitch, range: session.pitchRange) { session.cyclePitchRange() }
                KeyLockSwitch(on: $session.keyLock)
            }
        }
        .padding(14)
        .background(
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .fill(Self.pedal)
                .shadow(color: .black.opacity(0.35), radius: 8, y: 4)
        )
        .overlay(RoundedRectangle(cornerRadius: 16, style: .continuous).strokeBorder(Self.pedalLine, lineWidth: 1))
    }

    private func binding(_ kind: FXKind) -> Binding<FXState> {
        Binding(get: { session.fx[kind] ?? FXState() }, set: { session.fx[kind] = $0 })
    }
}

extension FXKind {
    /// Knob cap colours, Chroma Console style.
    var color: Color {
        switch self {
        case .delay: return Color(hex: 0xE0332F)
        case .echo: return Color(hex: 0xF2C230)
        case .lofi: return Color(hex: 0x2FA84F)
        case .lpf: return Color(hex: 0x49B6E5)
        case .hpf: return Color(hex: 0x9B6BD3)
        case .reverb: return Color(hex: 0xDADAD6)
        case .spaceEcho: return Color(hex: 0xF08A24)
        case .stutter: return Color(hex: 0xF25FA8)
        }
    }
}

// MARK: - Looper

/// The bar looper and its five memories. Row one picks a length and starts the loop; row two recalls
/// saved loops; row three retriggers the loop from its start and saves it into a memory.
private struct LooperSection: View {
    @ObservedObject var session: ChopSession
    @State private var pulse = false

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 10) {
                label("LOOPER", "BARS")
                VStack(alignment: .leading, spacing: 3) {
                    sizeRow([1, 2, 4, 8, 16, 32])
                    sizeRow([0.5, 0.25, 0.125, 0.0625])
                }
                Spacer(minLength: 2)
                loopButton
            }

            HStack(spacing: 10) {
                label("MEMORY", session.savingLoop ? "PICK ONE" : "RECALL")
                HStack(spacing: 4) {
                    ForEach(0..<5, id: \.self) { slot in
                        MemoryKey(slot: slot, session: session)
                    }
                }
                // Loop memories export to Serato as saved loops.
                .padding(4)
                .overlay(
                    RoundedRectangle(cornerRadius: 8)
                        .strokeBorder(Palette.red, lineWidth: 1)
                        .overlay(alignment: .topTrailing) {
                            Text("SERATO LOOPS")
                                .font(.system(size: 6.5, weight: .black))
                                .tracking(0.8)
                                .foregroundStyle(Palette.red)
                                .padding(.horizontal, 3)
                                .background(Color(hex: 0xECE8DF))
                                .offset(x: -8, y: -5)
                        }
                        .allowsHitTesting(false)
                )
                .help("Loop memories export to Serato as saved loops")
            }

            HStack(spacing: 10) {
                Color.clear.frame(width: 68, height: 1)
                PedalButton(title: "RETRIGGER", symbol: "arrow.counterclockwise") { session.retriggerLoop() }
                    .contextMenu { MIDILearnItem(target: .transport(.retrigger)) }
                    .help("Jump back to the start of the loop and play it from the top")
                PedalButton(title: session.savingLoop ? "PICK A KEY" : "SAVE", symbol: "square.and.arrow.down", armed: session.savingLoop) {
                    withAnimation(Motion.snappy) { session.armLoopSave() }
                }
                .help("Save this loop: press SAVE, then one of the five memory keys")
            }
        }
        .onAppear {
            withAnimation(.easeInOut(duration: 0.6).repeatForever(autoreverses: true)) { pulse = true }
        }
    }

    private func sizeRow(_ sizes: [Double]) -> some View {
        HStack(spacing: 3) {
            ForEach(sizes, id: \.self) { bars in
                let on = session.loopBars == bars
                Text(bars < 1 ? "1/\(Int(1 / bars))" : "\(Int(bars))")
                    .font(.system(size: bars < 1 ? 9 : 11, weight: .black).monospacedDigit())
                    .foregroundStyle(on ? Palette.cream : Palette.ink.opacity(0.7))
                    .frame(width: 24, height: 22)
                    .background(RoundedRectangle(cornerRadius: 5).fill(on ? Palette.ink : Color.black.opacity(0.06)))
                    .contentShape(Rectangle())
                    .onTapGesture { withAnimation(Motion.jelly) { session.loopBars = bars } }
            }
        }
    }

    private func label(_ top: String, _ bottom: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(top)
                .font(.system(size: 11, weight: .black))
                .tracking(1)
            Text(bottom)
                .font(.system(size: 8, weight: .bold))
                .tracking(1)
                .opacity(session.savingLoop && bottom == "PICK ONE" ? 1 : 0.5)
                .foregroundStyle(session.savingLoop && bottom == "PICK ONE" ? Palette.red : Palette.ink)
        }
        .foregroundStyle(Palette.ink)
        .frame(width: 68, alignment: .leading)
    }

    private var loopButton: some View {
        Button { session.toggleLoop() } label: {
            HStack(spacing: 6) {
                Circle()
                    .fill(session.isLooping ? Color(hex: 0x3FD15A) : Palette.ink.opacity(0.2))
                    .frame(width: 8, height: 8)
                    .shadow(color: session.isLooping ? Color(hex: 0x3FD15A) : .clear, radius: pulse ? 6 : 2)
                Text("LOOP")
                    .font(.system(size: 11, weight: .black))
                    .tracking(1.5)
                    .foregroundStyle(Palette.ink)
                    .fixedSize()
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .background(Capsule().fill(Color.white))
            .overlay(Capsule().strokeBorder(Palette.ink.opacity(0.15), lineWidth: 1))
            .shadow(color: .black.opacity(0.15), radius: 0, y: 2)
        }
        .buttonStyle(BouncyButtonStyle(pressedScale: 0.9))
        .help("Loop the chosen number of bars from the playhead (or the selected pad)")
        .contextMenu { MIDILearnItem(target: .transport(.loop)) }
    }
}

/// A small pedal switch: white capsule with an icon; red while it's waiting for you.
private struct PedalButton: View {
    var title: String
    var symbol: String
    var armed = false
    var action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 5) {
                Image(systemName: symbol)
                    .font(.system(size: 10, weight: .black))
                Text(title)
                    .font(.system(size: 10, weight: .black))
                    .tracking(1.2)
                    .fixedSize()
            }
            .foregroundStyle(armed ? Palette.cream : Palette.ink)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 6)
            .background(Capsule().fill(armed ? Palette.red : Color.white))
            .overlay(Capsule().strokeBorder(Palette.ink.opacity(0.15), lineWidth: 1))
            .shadow(color: .black.opacity(0.15), radius: 0, y: 2)
        }
        .buttonStyle(BouncyButtonStyle(pressedScale: 0.92))
    }
}

/// One of the five loop memories: shows its bar count when filled, glows green while it's the loop playing,
/// and gets a red outline while SAVE is waiting for a slot. Right-click to clear.
private struct MemoryKey: View {
    let slot: Int
    @ObservedObject var session: ChopSession

    var body: some View {
        let memory = session.loopMemories[slot]
        let active = session.activeMemory == slot
        let armed = session.savingLoop
        Button { withAnimation(Motion.snappy) { session.pressLoopMemory(slot) } } label: {
            VStack(spacing: 1) {
                Text("\(slot + 1)")
                    .font(.system(size: 11, weight: .black).monospacedDigit())
                Text(memory.map { barLabel($0.bars) } ?? "EMPTY")
                    .font(.system(size: 6, weight: .bold))
                    .tracking(0.4)
            }
            .foregroundStyle(active ? Palette.cream : Palette.ink.opacity(memory == nil ? 0.35 : 0.9))
            .frame(maxWidth: .infinity, minHeight: 24)
            .background(RoundedRectangle(cornerRadius: 6).fill(active ? Palette.ink : (memory == nil ? Color.black.opacity(0.06) : Color.white)))
            .overlay(RoundedRectangle(cornerRadius: 6)
                .strokeBorder(armed ? Palette.red : Palette.ink.opacity(memory == nil ? 0.12 : 0.25), lineWidth: armed ? 1.8 : 1))
            .overlay(alignment: .topTrailing) {
                if active {
                    Circle()
                        .fill(Color(hex: 0x3FD15A))
                        .frame(width: 5, height: 5)
                        .shadow(color: Color(hex: 0x3FD15A), radius: 3)
                        .padding(4)
                }
            }
            .shadow(color: .black.opacity(memory == nil ? 0 : 0.12), radius: 0, y: 2)
        }
        .buttonStyle(BouncyButtonStyle(pressedScale: 0.9))
        .help(memory.map { "Memory \(slot + 1): \(barLabel($0.bars).lowercased()) from \(session.timestamp($0.start)). Right-click to clear." }
              ?? "Empty. Press SAVE, then this key, to store the loop.")
        .contextMenu {
            if memory != nil {
                Button("Clear memory \(slot + 1)") { session.clearLoopMemory(slot) }
            }
        }
    }
}

// MARK: - Effect knob

/// Tap to switch the effect on or off; drag up or down for depth.
/// The performance page uses it too, where "on" means armed for the pad.
struct FXKnob: View {
    let kind: FXKind
    @Binding var state: FXState
    var offCaption: String? = nil
    /// The knob the MIDI encoder is on (the last one clicked).
    var selected = false
    var onSelect: () -> Void = {}
    @State private var dragStart: Double?

    private let size: CGFloat = 34

    var body: some View {
        VStack(spacing: 4) {
            ZStack {
                if selected {
                    Circle()
                        .strokeBorder(kind.color, lineWidth: 2)
                        .frame(width: size + 18, height: size + 18)
                        .shadow(color: kind.color.opacity(0.7), radius: 4)
                }
                // Depth arc around the knob.
                Circle()
                    .trim(from: 0, to: 0.75)
                    .stroke(Palette.ink.opacity(0.12), style: StrokeStyle(lineWidth: 2.5, lineCap: .round))
                    .rotationEffect(.degrees(135))
                    .frame(width: size + 10, height: size + 10)
                Circle()
                    .trim(from: 0, to: 0.75 * state.depth)
                    .stroke(state.on ? kind.color : Palette.ink.opacity(0.3), style: StrokeStyle(lineWidth: 2.5, lineCap: .round))
                    .rotationEffect(.degrees(135))
                    .frame(width: size + 10, height: size + 10)

                // Knurled black skirt, coloured cap, pointer.
                Circle().fill(Color(hex: 0x2A2A2A)).frame(width: size, height: size)
                Circle()
                    .fill(RadialGradient(colors: [kind.color.opacity(0.85), kind.color], center: .init(x: 0.35, y: 0.3), startRadius: 1, endRadius: size * 0.45))
                    .frame(width: size * 0.74, height: size * 0.74)
                Capsule()
                    .fill(Color.white.opacity(0.9))
                    .frame(width: 2.5, height: size * 0.22)
                    .offset(y: -size * 0.24)
                    .rotationEffect(.degrees(-135 + 270 * state.depth))
            }
            .frame(width: size + 12, height: size + 12)
            .shadow(color: .black.opacity(0.25), radius: 3, y: 2)
            .scaleEffect(dragStart == nil ? 1 : 1.08)
            .animation(Motion.jelly, value: dragStart == nil)
            .contentShape(Circle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        if dragStart == nil { dragStart = state.depth; onSelect() }
                        guard abs(g.translation.height) > 2 else { return }
                        state.depth = min(max((dragStart ?? 0.5) - g.translation.height / 140, 0), 1)
                        if !state.on { state.on = true }
                    }
                    .onEnded { g in
                        if abs(g.translation.height) <= 2 {
                            withAnimation(Motion.bouncy) { state.on.toggle() }
                        }
                        dragStart = nil
                    }
            )
            .help("\(kind.label): tap to switch on/off and put it on the MIDI encoder, drag up/down for depth. Right-click to MIDI Learn.")
            .contextMenu {
                MIDILearnItem(target: .fxDepth(kind))
                MIDILearnItem(target: .fxToggle(kind))
                Divider()
                MIDILearnItem(target: .encoder)
                MIDILearnItem(target: .encoderPress)
            }

            Text(kind.label)
                .font(.system(size: 8.5, weight: .black))
                .foregroundStyle(Palette.ink)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
            HStack(spacing: 4) {
                RoundedRectangle(cornerRadius: 1.5)
                    .fill(state.on ? kind.color : Palette.ink.opacity(0.15))
                    .frame(width: 7, height: 7)
                    .shadow(color: state.on ? kind.color : .clear, radius: 3)
                Text(state.on ? (kind == .stutter && offCaption == nil ? "1/\(FXState.stutterDivision(state.depth))" : "\(Int(state.depth * 100))") : (offCaption ?? kind.detail))
                    .font(.system(size: 7, weight: .bold).monospacedDigit())
                    .foregroundStyle(Palette.ink.opacity(0.5))
                    .lineLimit(1)
                    .minimumScaleFactor(0.7)
            }
        }
        .frame(maxWidth: .infinity)
    }
}

// MARK: - Key lock

/// A small steel push switch with a green lamp: when lit, the pitch fader changes tempo but not key.
private struct KeyLockSwitch: View {
    @Binding var on: Bool

    var body: some View {
        Button { withAnimation(Motion.snappy) { on.toggle() } } label: {
            VStack(spacing: 3) {
                HStack(spacing: 4) {
                    RoundedRectangle(cornerRadius: 1)
                        .fill(on ? Color(hex: 0x3FD15A) : Palette.ink.opacity(0.25))
                        .frame(width: 8, height: 3)
                        .shadow(color: on ? Color(hex: 0x3FD15A) : .clear, radius: 2)
                    Text("KEY")
                        .font(.system(size: 9, weight: .black))
                        .foregroundStyle(Palette.ink.opacity(0.85))
                }
                .frame(width: 52, height: 22)
                .background(BrushedSteel())
                .clipShape(RoundedRectangle(cornerRadius: 3, style: .continuous))
                .overlay(RoundedRectangle(cornerRadius: 3, style: .continuous).strokeBorder(Color.black.opacity(0.35), lineWidth: 1))
                .background(RoundedRectangle(cornerRadius: 4).fill(Color.black.opacity(0.5)).offset(y: 2))
                Text("lock")
                    .font(.system(size: 8, weight: .medium))
                    .foregroundStyle(Palette.ink.opacity(0.6))
            }
        }
        .buttonStyle(BouncyButtonStyle(pressedScale: 0.9))
        .help("Key lock: the pitch fader changes the tempo but keeps the key (Rubber Band)")
    }
}

// MARK: - Pitch fader

/// A turntable pitch slider: −range at the top, +range at the bottom, green lamp at zero,
/// and a silver button above it that switches the range between ±8, ±16 and ±50 %.
/// Double-click the fader to snap back to 0.
private struct PitchFader: View {
    @Binding var pitch: Double
    var range: Int
    var cycleRange: () -> Void
    @State private var dragStart: Double?

    private var span: Double { Double(range) }

    var body: some View {
        VStack(spacing: 6) {
            Button(action: cycleRange) {
                HStack(spacing: 4) {
                    RoundedRectangle(cornerRadius: 1)
                        .fill(range == 8 ? Palette.ink.opacity(0.25) : Palette.red)
                        .frame(width: 8, height: 3)
                        .shadow(color: range == 8 ? .clear : Palette.red, radius: 2)
                    Text("±\(range)")
                        .font(.system(size: 10, weight: .black).monospacedDigit())
                        .foregroundStyle(Palette.ink.opacity(0.85))
                }
                .frame(width: 52, height: 22)
                .background(BrushedSteel())
                .clipShape(RoundedRectangle(cornerRadius: 3, style: .continuous))
                .overlay(RoundedRectangle(cornerRadius: 3, style: .continuous).strokeBorder(Color.black.opacity(0.35), lineWidth: 1))
                .background(RoundedRectangle(cornerRadius: 4).fill(Color.black.opacity(0.5)).offset(y: 2))
            }
            .buttonStyle(BouncyButtonStyle(pressedScale: 0.9))
            .help("Pitch range: ±8, ±16 or ±50 %")
            Text(String(format: range == 50 ? "%+.0f%%" : "%+.1f%%", pitch))
                .font(.system(size: 11, weight: .black).monospacedDigit())
                .foregroundStyle(Palette.ink)
                .contentTransition(.numericText())
            GeometryReader { geo in
                faderBody(height: geo.size.height)
            }
            .frame(width: 58)
            Text("pitch adj.")
                .font(.system(size: 8, weight: .medium))
                .foregroundStyle(Palette.ink.opacity(0.6))
        }
        .help("Pitch (speed and pitch together, like a turntable). Double-click to reset. Right-click to MIDI Learn a slider.")
        .contextMenu { MIDILearnItem(target: .pitch) }
    }

    private let capHeight: CGFloat = 26

    private func faderBody(height h: CGFloat) -> some View {
        let travel: CGFloat = h - capHeight
        let y: CGFloat = CGFloat((pitch + span) / (2 * span)) * travel  // −range at the top
        let atZero = abs(pitch) < 0.05
        return ZStack(alignment: .topLeading) {
            ForEach(-8...8, id: \.self) { v in
                tick(v).offset(y: CGFloat(Double(v) + 8) / 16 * travel + capHeight / 2 - 4)
            }
            RoundedRectangle(cornerRadius: 1)
                .fill(Color(hex: atZero ? 0x3FD15A : 0x2C5E35))
                .frame(width: 10, height: 5)
                .shadow(color: atZero ? Color(hex: 0x3FD15A) : .clear, radius: 3)
                .offset(x: 2, y: travel / 2 + capHeight / 2 - 2.5)
            RoundedRectangle(cornerRadius: 3)
                .fill(Color(hex: 0x1E1E1E))
                .frame(width: 6, height: h - 8)
                .offset(x: 34, y: 4)
            cap.offset(x: 22, y: y)
        }
        .frame(width: 58, height: h, alignment: .topLeading)
        .contentShape(Rectangle())
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { g in
                    if dragStart == nil { dragStart = pitch }
                    let value = (dragStart ?? 0) + Double(g.translation.height / max(travel, 1)) * 2 * span
                    // A little detent at zero, like the real thing.
                    pitch = abs(value) < span / 32 ? 0 : min(max(value, -span), span)
                }
                .onEnded { _ in dragStart = nil }
        )
        .simultaneousGesture(TapGesture(count: 2).onEnded { withAnimation(Motion.bouncy) { pitch = 0 } })
    }

    private func tick(_ v: Int) -> some View {
        let major = v % 2 == 0
        return HStack(spacing: 3) {
            Text(major && v != 0 ? "\(abs(v) * range / 8)" : "")
                .font(.system(size: 7, weight: .bold).monospacedDigit())
                .foregroundStyle(Palette.ink.opacity(0.6))
                .frame(width: 10, alignment: .trailing)
            Rectangle()
                .fill(Palette.ink.opacity(major ? 0.6 : 0.3))
                .frame(width: major ? 7 : 4, height: 1)
        }
    }

    private var cap: some View {
        VStack(spacing: 3) {
            ForEach(0..<3, id: \.self) { _ in
                Rectangle().fill(Color.black.opacity(0.35)).frame(height: 1)
            }
        }
        .padding(.horizontal, 3)
        .frame(width: 30, height: capHeight)
        .background(BrushedSteel())
        .clipShape(RoundedRectangle(cornerRadius: 3))
        .overlay(RoundedRectangle(cornerRadius: 3).strokeBorder(Color.black.opacity(0.4), lineWidth: 1))
        .shadow(color: .black.opacity(0.35), radius: 2, y: 2)
    }
}
