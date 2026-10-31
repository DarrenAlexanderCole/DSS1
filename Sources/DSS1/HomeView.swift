import SwiftUI

/// The import screen: red paper, one stamped mark to drop on, a row of grass for progress.
struct HomeView: View {
    @EnvironmentObject private var model: AppModel
    @EnvironmentObject private var player: AudioPlayer
    @EnvironmentObject private var library: Library
    @State private var eye: CGFloat = 1
    @State private var bob = false
    @State private var showDamaged = false

    private var targeted: Bool { model.isDropTargeted }

    private var headline: String {
        switch model.phase {
        case .idle: return targeted ? "LET IT GO" : "DROP AUDIO"
        case let .extracting(_, done, total): return total > 0 ? "UNZIPPING \(done) / \(total)" : "UNZIPPING"
        case let .copying(done, total, _): return "\(min(done + 1, total)) / \(total)"
        case .done(let n): return "\(n) IN"
        }
    }

    private var subline: String {
        switch model.phase {
        case .idle: return model.note ?? (targeted ? "audio only, straight in" : "files, folders or zips · or click the record")
        case let .extracting(name, _, _): return name
        case let .copying(_, _, name): return name
        case .done: return model.note ?? "into \(model.destination?.lastPathComponent ?? "your folder")"
        }
    }

    var body: some View {
        ZStack {
            Palette.red
            GrainOverlay(image: Grain.paper)

            VStack(spacing: 0) {
                ZStack {
                    Wordmark(color: Palette.ink)
                    HStack {
                        Spacer()
                        if !library.tracks.isEmpty {
                            playerButton
                                .transition(.scale(scale: 0.4).combined(with: .opacity))
                        }
                    }
                }
                .frame(height: 30)
                .animation(Motion.jelly, value: library.tracks.isEmpty)

                Spacer(minLength: 12)
                mark
                status.padding(.top, 22)
                Spacer(minLength: 12)

                SpikeRow(progress: model.phase.progress)
                    .padding(.bottom, 24)
                bottomBar
            }
            .padding(.horizontal, 34)
            .padding(.top, 14)
            .padding(.bottom, 26)

            GrainOverlay(image: Grain.speckle)
        }
        .task { await blinkForever() }
        .onAppear {
            withAnimation(.easeInOut(duration: 2.4).repeatForever(autoreverses: true)) { bob = true }
        }
    }

    private var mark: some View {
        Button { model.pickSources() } label: {
            TimelineView(.animation(minimumInterval: nil, paused: !model.phase.isBusy)) { context in
                CrateMark(size: 280,
                          spin: .degrees(model.phase.isBusy ? context.date.timeIntervalSinceReferenceDate * 240 : 0),
                          eye: targeted ? 1.6 : eye)
            }
        }
        .buttonStyle(BouncyButtonStyle(pressedScale: 0.92))
        .scaleEffect(targeted ? 1.14 : 1)
        .rotationEffect(.degrees(targeted ? -5 : 0))
        .animation(Motion.jelly, value: targeted)
        .offset(y: bob ? -5 : 5)
        .help("Choose audio files, folders or zips")
    }

    private var status: some View {
        VStack(spacing: 8) {
            Text(headline)
                .font(.system(size: 52, weight: .black))
                .fontWidth(.condensed)
                .contentTransition(.numericText())
            Text(subline.uppercased())
                .font(.system(size: 11, weight: .bold))
                .tracking(2.2)
                .opacity(0.6)
                .lineLimit(1)
                .truncationMode(.middle)
                .frame(maxWidth: 520)
            if !model.damaged.isEmpty && !model.phase.isBusy {
                Button { showDamaged = true } label: {
                    HStack(spacing: 5) {
                        Image(systemName: "exclamationmark.triangle.fill")
                        Text("\(model.damaged.count) COULDN'T BE READ · SHOW")
                    }
                    .font(.system(size: 10, weight: .black))
                    .tracking(1.5)
                    .foregroundStyle(Palette.cream)
                    .padding(.horizontal, 12)
                    .padding(.vertical, 6)
                    .background(Capsule().fill(Palette.ink))
                }
                .buttonStyle(BouncyButtonStyle())
                .popover(isPresented: $showDamaged) { DamagedList(names: model.damaged) }
                .padding(.top, 4)
            }
            if let p = library.analysisProgress, !model.phase.isBusy {
                AnalysisReadout(done: p.done, total: p.total, color: Palette.ink)
                    .padding(.top, 6)
            }
        }
        .foregroundStyle(Palette.ink)
        .animation(Motion.snappy, value: headline)
    }

    private var bottomBar: some View {
        HStack(alignment: .bottom) {
            Button { model.pickDestination() } label: {
                VStack(alignment: .leading, spacing: 3) {
                    Text("SAVES TO")
                        .font(.system(size: 9, weight: .bold))
                        .tracking(2)
                        .opacity(0.55)
                    HStack(spacing: 6) {
                        Text(model.destination?.lastPathComponent.uppercased() ?? "CHOOSE A FOLDER")
                            .font(.system(size: 18, weight: .black))
                            .fontWidth(.condensed)
                        Image(systemName: "arrow.right")
                            .font(.system(size: 11, weight: .black))
                    }
                }
                .foregroundStyle(Palette.ink)
            }
            .buttonStyle(BouncyButtonStyle())
            .help("Change destination")

            Spacer()

            Button { model.openDestination() } label: {
                HStack(spacing: 5) {
                    Text("OPEN")
                    Image(systemName: "arrow.up.forward")
                }
                .font(.system(size: 11, weight: .black))
                .tracking(2)
                .foregroundStyle(Palette.ink)
            }
            .buttonStyle(BouncyButtonStyle())
            .help("Show the folder in Finder")
        }
    }

    private var playerButton: some View {
        Button { model.showPlayer() } label: {
            HStack(spacing: 6) {
                Image(systemName: player.isPlaying ? "waveform" : "play.fill")
                    .symbolEffect(.variableColor.iterative, isActive: player.isPlaying)
                Text("THE CRATE  \(library.tracks.count)")
                    .tracking(2)
            }
            .font(.system(size: 11, weight: .black))
            .foregroundStyle(Palette.cream)
            .padding(.horizontal, 14)
            .padding(.vertical, 8)
            .background(Capsule().fill(Palette.ink))
        }
        .buttonStyle(BouncyButtonStyle())
    }

    private func blinkForever() async {
        while !Task.isCancelled {
            try? await Task.sleep(for: .seconds(Double.random(in: 2.5...5.5)))
            withAnimation(.easeIn(duration: 0.07)) { eye = 0.08 }
            try? await Task.sleep(for: .milliseconds(110))
            withAnimation(Motion.jelly) { eye = 1 }
        }
    }
}

/// "ANALYSING 37 / 120 · BPM, KEY & LOUDNESS" with a thin bar.
struct AnalysisReadout: View {
    var done: Int
    var total: Int
    var color: Color

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("ANALYSING \(done) / \(total) · BPM, KEY & LOUDNESS")
                .font(.system(size: 9, weight: .black).monospacedDigit())
                .tracking(1.5)
                .contentTransition(.numericText())
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    Capsule().fill(color.opacity(0.15))
                    Capsule().fill(color).frame(width: geo.size.width * Double(done) / Double(max(total, 1)))
                }
            }
            .frame(height: 3)
        }
        .foregroundStyle(color.opacity(0.75))
        .frame(width: 260)
        .animation(Motion.snappy, value: done)
    }
}

/// The files an import had to leave out.
private struct DamagedList: View {
    var names: [String]

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Couldn't read these")
                .font(.system(size: 13, weight: .bold))
            Text("They may be damaged, cut short, or not really audio, so they were left out.")
                .font(.system(size: 11))
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            ScrollView {
                VStack(alignment: .leading, spacing: 4) {
                    ForEach(names, id: \.self) { Text($0).font(.system(size: 11).monospaced()) }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .frame(maxHeight: 220)
        }
        .padding(14)
        .frame(width: 360)
    }
}

/// The moose's grass, doubling as a progress bar: blades spring up as files land.
struct SpikeRow: View {
    var progress: Double
    var count = 36

    var body: some View {
        let lit = Int((progress * Double(count)).rounded())
        HStack(alignment: .bottom, spacing: 5) {
            ForEach(0..<count, id: \.self) { i in
                let on = i < lit
                Spike()
                    .fill(on ? Palette.ink : Palette.ink.opacity(0.14))
                    .frame(maxWidth: .infinity)
                    .frame(height: 30)
                    .scaleEffect(x: 1, y: on ? 1 : 0.4, anchor: .bottom)
                    .animation(Motion.jelly.delay(Double(i % 9) * 0.012), value: on)
            }
        }
        .frame(maxWidth: 640)
    }
}
