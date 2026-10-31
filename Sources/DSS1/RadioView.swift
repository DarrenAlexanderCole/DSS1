import AppKit
import SwiftUI

/// The radio page: a cassette deck in scratched brushed aluminium, mounted in a painted steel cabinet,
/// showing whatever you're hearing (library player or chop station): spinning reels,
/// two aged-paper VU meters, a mechanical tape counter, the track's details, and piano-key transport.
///
/// Drawn in three layers: the metal (rendered once), the moving parts (every frame), and glass on top.
struct RadioView: View {
    @EnvironmentObject private var model: AppModel
    @EnvironmentObject private var player: AudioPlayer
    @EnvironmentObject private var library: Library
    @State private var motion = DeckMotion()

    var body: some View {
        GeometryReader { geo in
            // Racked top to bottom: cassette deck, graphic EQ, live recording deck, take monitor.
            let gap: CGFloat = 8
            let stack = DeckLayout.size.height + EQUnit.size.height + RecorderDeckView.size.height
                + TakeMonitorView.size.height + gap * 3
            let margin: CGFloat = 22
            let scale = min(1.15, (geo.size.width - 300) / DeckLayout.size.width, (geo.size.height - margin * 2) / stack)
            let rack = CGSize(width: DeckLayout.size.width * scale, height: stack * scale)
            let gutter = (geo.size.width - rack.width) / 2
            let top = (geo.size.height - rack.height) / 2

            ZStack(alignment: .topLeading) {
                CabinetBackground(gutter: gutter, rackTop: top, rackHeight: rack.height)

                VStack(spacing: gap) {
                    ZStack(alignment: .topLeading) {
                        DeckBody()
                        TimelineView(.animation) { _ in
                            let now = NowPlaying.current(model: model, player: player, library: library)
                            let readings = motion.update(at: Date(), rate: now.rate, meter: now.meter, gain: now.volumeGain, trackID: now.trackID)
                            DeckLive(now: now, readings: readings,
                                     resetCounter: { motion.counterOffset = motion.counter },
                                     actions: actions(for: now))
                        }
                        DeckGlass()
                    }
                    .frame(width: DeckLayout.size.width, height: DeckLayout.size.height)
                    EQUnit()
                    RecorderDeckView()
                    TakeMonitorView()
                }
                .frame(width: DeckLayout.size.width, height: stack)
                .scaleEffect(scale)
                .frame(width: rack.width, height: rack.height)
                .position(x: geo.size.width / 2, y: top + rack.height / 2)

                backButton
                    .position(x: gutter / 2, y: max(top - 4, 34) + 12)
            }
        }
        .ignoresSafeArea()
    }

    private var backButton: some View {
        BackButton(label: model.radioReturn.backLabel) { model.closeRadio() }
    }

    /// The piano keys act on whatever is playing.
    private func actions(for now: NowPlaying) -> DeckActions {
        if now.isChop, let chop = model.chop {
            return DeckActions(
                rewind: { model.transport(.rewind) },
                play: { model.transport(.play) },
                pause: { model.transport(.pause) },
                stop: { model.transport(.stop) },
                forward: { model.transport(.forward) },
                eject: { model.ejectToCrate() },
                previousTrack: { model.previousTrack() },
                nextTrack: { model.nextTrack() })
        }
        return DeckActions(
            rewind: { model.transport(.rewind) },
            play: { model.transport(.play) },
            pause: { model.transport(.pause) },
            stop: { model.transport(.stop) },
            forward: { model.transport(.forward) },
            eject: { model.ejectToCrate() },
            previousTrack: { model.previousTrack() },
            nextTrack: { model.nextTrack() })
    }
}

// MARK: - What's playing

/// Whatever is sounding right now: the chop station if it's running, otherwise the library player.
@MainActor
struct NowPlaying {
    var title: String
    var artist: String
    var format: String
    var bpm: Double?
    var key: String?
    var camelot: String?
    var elapsed: Double
    var duration: Double
    var rate: Double
    var playing: Bool
    var source: String
    var isChop: Bool
    var meter: LevelMeter?
    /// The volume knob (half the dB of its real taper, so the needles follow it without going slack at low volume).
    var volumeGain: Double = 1
    /// Identifies the song, so the cassette can change colourway when it changes.
    var trackID: String

    var progress: Double { duration > 0 ? min(max(elapsed / duration, 0), 1) : 0 }

    static func current(model: AppModel, player: AudioPlayer, library: Library) -> NowPlaying {
        if let chop = model.chop, chop.isPlaying || model.radioReturn == .chop {
            let info = chop.track.info
            let rate = chop.isPlaying ? chop.direction * (1 + chop.pitch / 100) : 0
            let source: String
            if chop.isLooping {
                source = "LOOP · \(barLabel(chop.loopBars))"
            } else {
                source = "CHOP STATION"
            }
            return NowPlaying(title: info.title, artist: info.artist, format: info.format,
                              bpm: chop.analysis?.bpm, key: chop.analysis?.key, camelot: chop.analysis?.camelot,
                              elapsed: Double(chop.playhead) / chop.sampleRate,
                              duration: Double(chop.frameCount) / chop.sampleRate,
                              rate: rate, playing: chop.isPlaying, source: source, isChop: true,
                              meter: chop.sampler.meter, volumeGain: player.volume, trackID: chop.track.url.path)
        }
        let info = player.current
        let analysis = player.currentURL.flatMap { library.result(for: $0) }
        return NowPlaying(title: info?.title ?? "No tape", artist: info?.artist ?? "Pick something from the crate",
                          format: info?.format ?? "", bpm: analysis?.bpm, key: analysis?.key, camelot: analysis?.camelot,
                          elapsed: player.currentTime, duration: player.duration,
                          rate: player.isPlaying ? player.direction * player.speed : 0, playing: player.isPlaying,
                          source: player.tracks.isEmpty ? "RADIO" : "RADIO · \(player.index + 1) OF \(player.tracks.count)",
                          isChop: false, meter: player.meter, volumeGain: player.volume, trackID: player.currentURL?.path ?? "")
    }
}

struct DeckActions {
    var rewind: () -> Void
    var play: () -> Void
    var pause: () -> Void
    var stop: () -> Void
    var forward: () -> Void
    var eject: () -> Void
    var previousTrack: () -> Void = {}
    var nextTrack: () -> Void = {}
}

/// A cassette colourway: shell, reel discs and label stripes. Every song gets one.
struct Colorway {
    let name: String
    let shell, edge, disc, discLight, discDark, stripeA, stripeB: UInt32

    static let all: [Colorway] = [
        Colorway(name: "Gold Reel", shell: 0x1E1E20, edge: 0x0E0E0F, disc: 0xD59A4C, discLight: 0xF2C98A, discDark: 0x8A5A24, stripeA: 0xC9442C, stripeB: 0xD9A93A),
        Colorway(name: "Chrome", shell: 0x2C2F33, edge: 0x16181A, disc: 0xC9CCD0, discLight: 0xF4F5F6, discDark: 0x82868B, stripeA: 0x3A6EA5, stripeB: 0xC9442C),
        Colorway(name: "Ruby", shell: 0x4A1216, edge: 0x230709, disc: 0xD2392F, discLight: 0xF28A7E, discDark: 0x7A1712, stripeA: 0xF2C230, stripeB: 0xF4EAD2),
        Colorway(name: "Teal", shell: 0x15464B, edge: 0x0A2326, disc: 0x6FC8C0, discLight: 0xB6EDE6, discDark: 0x2C7F7A, stripeA: 0xF08A24, stripeB: 0xF4EAD2),
        Colorway(name: "Violet", shell: 0x2A1B40, edge: 0x120A1F, disc: 0xA074DD, discLight: 0xD3B8F5, discDark: 0x5B3A8E, stripeA: 0xF25FA8, stripeB: 0x6AD97F),
        Colorway(name: "Bone", shell: 0xD8D2C0, edge: 0x9E987F, disc: 0x2D2D2F, discLight: 0x6A6A6D, discDark: 0x121213, stripeA: 0xC9442C, stripeB: 0x2F6FA8),
        Colorway(name: "Moss", shell: 0x1A3322, edge: 0x0B1710, disc: 0x86C466, discLight: 0xC4EBA8, discDark: 0x437A2E, stripeA: 0xF2C230, stripeB: 0xC9442C),
        Colorway(name: "Tangerine", shell: 0x35190B, edge: 0x180A04, disc: 0xF08A24, discLight: 0xFFC17A, discDark: 0xA6510E, stripeA: 0x1D1A16, stripeB: 0xF4EAD2),
    ]

    var colors: CassetteColors { Self.blend(self, self, 1) }

    /// Cross-fades between two colourways (t: 0 = `a`, 1 = `b`).
    static func blend(_ a: Colorway, _ b: Colorway, _ t: Double) -> CassetteColors {
        func mix(_ x: UInt32, _ y: UInt32) -> (r: Double, g: Double, b: Double) {
            func ch(_ v: UInt32, _ shift: UInt32) -> Double { Double((v >> shift) & 0xFF) / 255 }
            return (ch(x, 16) + (ch(y, 16) - ch(x, 16)) * t, ch(x, 8) + (ch(y, 8) - ch(x, 8)) * t, ch(x, 0) + (ch(y, 0) - ch(x, 0)) * t)
        }
        func color(_ c: (r: Double, g: Double, b: Double)) -> Color { Color(.sRGB, red: c.r, green: c.g, blue: c.b) }
        let shell = mix(a.shell, b.shell)
        return CassetteColors(
            shell: color(shell),
            shellTop: color((min(1, shell.r + 0.09), min(1, shell.g + 0.09), min(1, shell.b + 0.09))),
            edge: color(mix(a.edge, b.edge)),
            disc: color(mix(a.disc, b.disc)),
            discLight: color(mix(a.discLight, b.discLight)),
            discDark: color(mix(a.discDark, b.discDark)),
            stripeA: color(mix(a.stripeA, b.stripeA)),
            stripeB: color(mix(a.stripeB, b.stripeB)),
            isLight: (0.3 * shell.r + 0.59 * shell.g + 0.11 * shell.b) > 0.5,
            name: t < 0.5 ? a.name : b.name)
    }
}

struct CassetteColors: Equatable {
    var shell, shellTop, edge, disc, discLight, discDark, stripeA, stripeB: Color
    var isLight: Bool
    var name: String
}

/// A snapshot of the moving parts, handed to the views each frame (plain values, so SwiftUI redraws them).
struct DeckReadings: Equatable {
    var leftReel = 0.0
    var rightReel = 0.0
    var leftNeedle = 0.0
    var rightNeedle = 0.0
    var peak = 0.0
    var counter = 0
    var colors = Colorway.all[0].colors
}

/// Things that move between frames: reel angles, VU needles (with real VU ballistics), the tape counter.
@MainActor
final class DeckMotion {
    var leftReel = 0.0
    var rightReel = 0.0
    var leftNeedle = 0.0
    var rightNeedle = 0.0
    var peak = 0.0
    var counter = 0.0
    var counterOffset = 0.0
    private var last: Date?
    private var trackID = ""
    private var colorIndex = 0
    private var previousColor = 0
    private var changedAt = Date.distantPast

    /// dBFS (RMS) that reads 0 VU. Level-matched masters run around −10 dBFS RMS, so they swing around 0 and +3
    /// at full volume instead of sitting against the stop.
    static let referenceDB = -14.0

    /// Where a level in dB sits across a VU meter's arc (0…1). The meter is linear in voltage,
    /// so 0 VU lands about 70% of the way across, as on the real thing.
    static func arc(dB: Double) -> Double {
        let v = pow(10, dB / 20)
        return min(max((v - 0.1) / (1.413 - 0.1), 0), 1.06)
    }

    func update(at date: Date, rate: Double, meter: LevelMeter?, gain: Double = 1, trackID: String) -> DeckReadings {
        step(at: date, rate: rate, meter: meter, gain: gain)
        select(trackID, at: date)
        let t = min(1, max(0, date.timeIntervalSince(changedAt) / 0.9))
        let eased = t * t * (3 - 2 * t)
        return DeckReadings(leftReel: leftReel, rightReel: rightReel, leftNeedle: leftNeedle, rightNeedle: rightNeedle,
                            peak: peak, counter: Int(max(0, counter - counterOffset)) % 1000,
                            colors: Colorway.blend(Colorway.all[previousColor], Colorway.all[colorIndex], eased))
    }

    /// A new song gets a new colourway: always different from the one before, and the same song
    /// tends to come back in the same colours (it's picked from the file name).
    private func select(_ id: String, at date: Date) {
        guard id != trackID else { return }
        let first = trackID.isEmpty
        trackID = id
        var hash: UInt64 = 14695981039346656037
        for byte in id.utf8 { hash = (hash ^ UInt64(byte)) &* 1099511628211 }
        var next = Int(hash % UInt64(Colorway.all.count))
        if !first && next == colorIndex { next = (next + 1) % Colorway.all.count }
        previousColor = first ? next : colorIndex
        colorIndex = next
        changedAt = first ? .distantPast : date
    }

    private func step(at date: Date, rate: Double, meter: LevelMeter?, gain: Double) {
        let dt = min(max(date.timeIntervalSince(last ?? date), 0), 0.1)
        last = date

        // Reels: about one turn a second at normal speed; the right (take-up) reel turns the same way.
        leftReel += rate * dt * 360
        rightReel += rate * dt * 360
        counter += rate * dt * 1.25

        // VU: 0 VU is `referenceDB` RMS at full volume, with a ~300 ms rise and fall. The reading follows the
        // volume knob, so the needles show what you hear.
        let (l, r) = meter?.stereo() ?? (0, 0)
        func target(_ rms: Float) -> Double {
            guard rms > 0, gain > 0 else { return 0 }
            return Self.arc(dB: 20 * log10(Double(rms) * gain) - Self.referenceDB)
        }
        let k = 1 - exp(-dt / 0.09)
        leftNeedle += (target(l) - leftNeedle) * k
        rightNeedle += (target(r) - rightNeedle) * k
        let loudest = max(leftNeedle, rightNeedle)
        peak = loudest > Self.arc(dB: 1) ? 1 : max(0, peak - dt * 2.5)
    }
}

// MARK: - Layout

/// Where everything sits on the 1000 × 440 faceplate, shared by the metal, live and glass layers.
private enum DeckLayout {
    static let size = CGSize(width: 1000, height: 440)
    static let door = CGRect(x: 52, y: 34, width: 384, height: 236)
    static let window = door.insetBy(dx: 16, dy: 14)
    static let cassette = door.insetBy(dx: 30, dy: 26)
    static let keys = CGRect(x: 66, y: 302, width: 356, height: 70)
    static let badge = CGPoint(x: 543, y: 64)
    static let counter = CGRect(x: 470, y: 128, width: 146, height: 62)
    static let source = CGPoint(x: 543, y: 226)
    /// The compressor, under the tape counter.
    static let comp = CGRect(x: 456, y: 246, width: 174, height: 180)
    /// The CL 1B-style opto compressor, in the strip under the info panel.
    static let opto = CGRect(x: 650, y: 312, width: 300, height: 114)
    /// Previous / next track keys under the transport.
    static let track = CGRect(x: 66, y: 380, width: 356, height: 52)
    static let bridge = CGRect(x: 650, y: 34, width: 300, height: 176)
    static let meterL = CGRect(x: 664, y: 48, width: 126, height: 114)
    static let meterR = CGRect(x: 810, y: 48, width: 126, height: 114)
    static let peak = CGPoint(x: 800, y: 105)
    static let info = CGRect(x: 650, y: 228, width: 300, height: 74)
    static let screws: [CGPoint] = [CGPoint(x: 22, y: 26), CGPoint(x: 978, y: 26), CGPoint(x: 22, y: 414), CGPoint(x: 978, y: 414)]
}

private extension View {
    func place(_ r: CGRect) -> some View {
        frame(width: r.width, height: r.height).position(x: r.midX, y: r.midY)
    }

    /// Ink printed on the metal: dark, with a hairline of light catching the edge below.
    func engraved(_ opacity: Double = 0.78) -> some View {
        foregroundStyle(Color(hex: 0xD9D9D4).opacity(opacity))
            .shadow(color: .black.opacity(0.8), radius: 0, x: 0, y: -0.5)
    }
}

// MARK: - Cabinet

/// Grey-green hammertone steel with perforated vents, a lit top edge and a brass bolt.
private struct CabinetBackground: View {
    var gutter: CGFloat
    var rackTop: CGFloat
    var rackHeight: CGFloat

    var body: some View {
        GeometryReader { geo in
            ZStack(alignment: .topLeading) {
                Image(decorative: Materials.hammertone, scale: 2)
                    .resizable(resizingMode: .tile)
                Canvas { ctx, size in
                    func vent(_ r: CGRect, pitch: CGFloat, hole: CGFloat) {
                        var y = r.minY
                        var row = 0
                        while y < r.maxY {
                            var x = r.minX + (row % 2 == 0 ? 0 : pitch / 2)
                            while x < r.maxX {
                                let c = CGRect(x: x, y: y, width: hole, height: hole)
                                ctx.fill(Path(ellipseIn: c.offsetBy(dx: 0, dy: 1)), with: .color(.white.opacity(0.12)))
                                ctx.fill(Path(ellipseIn: c), with: .color(Color(hex: 0x0B0D0C).opacity(0.85)))
                                x += pitch
                            }
                            y += pitch * 0.87
                            row += 1
                        }
                    }
                    // Grilles centred in the space either side of the rack, running its height
                    // (below the Back button on the left, matched on the right).
                    let w = size.width
                    let ventW = min(110, max(40, gutter - 60))
                    let y0 = max(rackTop, 34) + 40, y1 = rackTop + rackHeight
                    vent(CGRect(x: (gutter - ventW) / 2, y: y0, width: ventW, height: y1 - y0), pitch: 13, hole: 6)
                    vent(CGRect(x: w - (gutter + ventW) / 2, y: y0, width: ventW, height: y1 - y0), pitch: 13, hole: 6)
                }
                // Rolled top edge catching the light, and soft falloff toward the corners.
                LinearGradient(colors: [.white.opacity(0.22), .white.opacity(0.04), .clear], startPoint: .top, endPoint: .init(x: 0.5, y: 0.08))
                RadialGradient(colors: [.clear, .black.opacity(0.45)], center: .center, startRadius: geo.size.width * 0.3, endRadius: geo.size.width * 0.8)
                BrassBolt().position(x: 18, y: geo.size.height * 0.55)
                BrassBolt().position(x: geo.size.width - 18, y: geo.size.height * 0.55)
            }
        }
        .ignoresSafeArea()
    }
}

private struct BrassBolt: View {
    var body: some View {
        Circle()
            .fill(RadialGradient(colors: [Color(hex: 0xD8B26A), Color(hex: 0x8C6A2E), Color(hex: 0x4A3816)],
                                 center: .init(x: 0.35, y: 0.3), startRadius: 1, endRadius: 12))
            .frame(width: 18, height: 18)
            .shadow(color: .black.opacity(0.5), radius: 2, x: 1, y: 2)
    }
}

// MARK: - Metal (static)

private struct DeckBody: View {
    var body: some View {
        ZStack(alignment: .topLeading) {
            // Black anodised faceplate, matching the EQ and the sampler below it.
            BlackPlate()

            // Rack-style mounting holes and screws.
            ForEach(Array(DeckLayout.screws.enumerated()), id: \.offset) { i, p in
                Capsule()
                    .fill(Color(hex: 0x141414))
                    .frame(width: 22, height: 13)
                    .overlay(Capsule().strokeBorder(LinearGradient(colors: [.black.opacity(0.7), .white.opacity(0.7)], startPoint: .top, endPoint: .bottom), lineWidth: 1))
                    .position(p)
                Screw(angle: Double(i) * 37 + 12, size: 11).position(p)
            }

            // Recesses.
            Recess(corner: 5).place(DeckLayout.door)
            RoundedRectangle(cornerRadius: 3)
                .fill(LinearGradient(colors: [Color(hex: 0x0E0F10), Color(hex: 0x1D1F21)], startPoint: .top, endPoint: .bottom))
                .overlay(RoundedRectangle(cornerRadius: 3).strokeBorder(Color.black, lineWidth: 1))
                .place(DeckLayout.window)
            Recess(corner: 4).place(DeckLayout.bridge)
            Recess(corner: 3, depth: 0.5).place(DeckLayout.counter)
            Recess(corner: 3, depth: 0.35).place(DeckLayout.info)
            Recess(corner: 3, depth: 0.35).place(DeckLayout.opto)
            RoundedRectangle(cornerRadius: 4)
                .fill(Color(hex: 0x1A1A1A))
                .overlay(RoundedRectangle(cornerRadius: 4).strokeBorder(LinearGradient(colors: [.black, .white.opacity(0.5)], startPoint: .top, endPoint: .bottom), lineWidth: 1))
                .place(CGRect(x: DeckLayout.keys.minX - 6, y: DeckLayout.keys.minY + 20, width: DeckLayout.keys.width + 12, height: 52))

            // VU meter faces, in bakelite bezels.
            MeterFace().place(DeckLayout.meterL)
            MeterFace().place(DeckLayout.meterR)

            // Printing.
            Text("DSS1")
                .font(.system(size: 31, weight: .bold))
                .tracking(4)
                .engraved(0.82)
                .position(DeckLayout.badge)
            Text("tape counter").font(.system(size: 10)).engraved(0.6)
                .position(x: DeckLayout.counter.midX, y: DeckLayout.counter.minY - 10)
            Text("source").font(.system(size: 10)).engraved(0.6)
                .position(x: DeckLayout.source.x, y: DeckLayout.source.y - 16)
            Text("Full Auto Stop  ·  DSS1 Auto-Sort").font(.system(size: 11, design: .serif)).engraved(0.62)
                .position(x: DeckLayout.door.midX, y: DeckLayout.door.maxY + 14)
            Text("left").font(.system(size: 11, design: .serif)).engraved(0.7)
                .position(x: DeckLayout.meterL.midX, y: DeckLayout.meterL.maxY + 34)
            Text("right").font(.system(size: 11, design: .serif)).engraved(0.7)
                .position(x: DeckLayout.meterR.midX, y: DeckLayout.meterR.maxY + 34)
            Text("peak").font(.system(size: 8)).engraved(0.6)
                .position(x: DeckLayout.peak.x, y: DeckLayout.bridge.minY + 7)
            HStack(spacing: 0) {
                Text("tempo").frame(width: 82, alignment: .leading)
                Text("key").frame(width: 112, alignment: .leading)
                Text("time").frame(maxWidth: .infinity, alignment: .leading)
            }
            .font(.system(size: 10))
            .engraved(0.6)
            .padding(.horizontal, 14)
            .place(CGRect(x: DeckLayout.info.minX, y: DeckLayout.info.minY + 6, width: DeckLayout.info.width, height: 16))
        }
        .frame(width: DeckLayout.size.width, height: DeckLayout.size.height)
        .drawingGroup()
    }
}

/// A pocket pressed into the metal: darker floor, shadowed top edge, light catching the bottom lip.
private struct Recess: View {
    var corner: CGFloat
    var depth: Double = 1

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: corner)
                .fill(LinearGradient(colors: [Color.black.opacity(0.5 * depth), Color.black.opacity(0.2 * depth)], startPoint: .top, endPoint: .bottom))
            RoundedRectangle(cornerRadius: corner)
                .strokeBorder(LinearGradient(colors: [.black.opacity(0.8), .black.opacity(0.3), .white.opacity(0.22)], startPoint: .top, endPoint: .bottom), lineWidth: 1.2)
            // Inner shadow under the top lip.
            RoundedRectangle(cornerRadius: corner)
                .stroke(Color.black.opacity(0.35 * depth), lineWidth: 6)
                .blur(radius: 4)
                .offset(y: 3)
                .clipShape(RoundedRectangle(cornerRadius: corner))
        }
    }
}

/// A Phillips-head screw: domed steel, a cross slot, a little rust.
struct Screw: View {
    var angle: Double
    var size: CGFloat
    var copper = false

    var body: some View {
        ZStack {
            Circle()
                .fill(RadialGradient(colors: copper ? [Color(hex: 0xF0BE98), Color(hex: 0xB06F42), Color(hex: 0x5A3418)]
                                                    : [Color(hex: 0xE9E9E6), Color(hex: 0x9A9A96), Color(hex: 0x4E4E4B)],
                                     center: .init(x: 0.35, y: 0.3), startRadius: 0.5, endRadius: size * 0.7))
            Circle().fill(Color(hex: 0x8A5A2B).opacity(0.18))
            Group {
                Capsule().frame(width: size * 0.72, height: size * 0.16)
                Capsule().frame(width: size * 0.16, height: size * 0.72)
            }
            .foregroundStyle(Color(hex: 0x2A2A28))
            .shadow(color: .white.opacity(0.5), radius: 0, x: 0.5, y: 0.5)
            .rotationEffect(.degrees(angle))
        }
        .frame(width: size, height: size)
        .shadow(color: .black.opacity(0.45), radius: 1, x: 0.5, y: 1)
    }
}

/// A VU meter face: aged paper in a black bakelite bezel, with a printed scale and a brand line.
private struct MeterFace: View {
    private static let marks: [(dB: Double, text: String)] = [
        (-20, "20"), (-10, "10"), (-7, "7"), (-5, "5"), (-3, "3"), (-2, ""), (-1, ""), (0, "0"), (1, "1"), (2, ""), (3, "3"),
    ]

    var body: some View {
        ZStack {
            // Bezel.
            RoundedRectangle(cornerRadius: 6)
                .fill(LinearGradient(colors: [Color(hex: 0x3A3A3A), Color(hex: 0x111111), Color(hex: 0x1E1E1E)], startPoint: .top, endPoint: .bottom))
                .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(LinearGradient(colors: [.white.opacity(0.35), .black], startPoint: .top, endPoint: .bottom), lineWidth: 1))
                .shadow(color: .black.opacity(0.5), radius: 3, y: 2)
            // Paper, backlit from below.
            ZStack {
                Image(decorative: Materials.meterPaper, scale: 2).resizable()
                RadialGradient(colors: [Color(hex: 0xFFE7A8).opacity(0.45), .clear], center: .init(x: 0.5, y: 0.95), startRadius: 0, endRadius: 110)
                    .blendMode(.screen)
                Canvas { ctx, size in drawScale(ctx, size) }
                LinearGradient(colors: [.black.opacity(0.25), .clear], startPoint: .top, endPoint: .init(x: 0.5, y: 0.18))
            }
            .clipShape(RoundedRectangle(cornerRadius: 2))
            .padding(7)
        }
    }

    private func drawScale(_ ctx: GraphicsContext, _ size: CGSize) {
        let g = VUGeometry(size: size)
        let ink = Color(hex: 0x1F1A12)
        let red = Color(hex: 0xB3261E)
        var black = Path(), redArc = Path()
        let zero = DeckMotion.arc(dB: 0)
        for i in 0...60 {
            let a = Double(i) / 60
            let p = g.point(a, g.radius * 0.84)
            if a <= zero { if black.isEmpty { black.move(to: p) } else { black.addLine(to: p) } }
            if a >= zero { if redArc.isEmpty { redArc.move(to: p) } else { redArc.addLine(to: p) } }
        }
        ctx.stroke(black, with: .color(ink.opacity(0.85)), lineWidth: 1)
        ctx.stroke(redArc, with: .color(red), lineWidth: 3.5)
        // Lower percentage arc, like real meters.
        var pct = Path()
        for i in 0...40 {
            let p = g.point(Double(i) / 40 * zero, g.radius * 0.66)
            if i == 0 { pct.move(to: p) } else { pct.addLine(to: p) }
        }
        ctx.stroke(pct, with: .color(ink.opacity(0.5)), lineWidth: 0.6)

        for mark in Self.marks {
            let a = DeckMotion.arc(dB: mark.dB)
            let col = mark.dB > 0 ? red : ink
            var tick = Path()
            tick.move(to: g.point(a, g.radius * 0.84))
            tick.addLine(to: g.point(a, g.radius * 0.76))
            ctx.stroke(tick, with: .color(col.opacity(0.9)), lineWidth: 0.9)
            guard !mark.text.isEmpty else { continue }
            let label = mark.dB > 0 ? "+\(mark.text)" : (mark.dB < 0 ? "−\(mark.text)" : "0")
            ctx.draw(Text(label).font(.system(size: 7.5, weight: .medium, design: .serif)).foregroundColor(col.opacity(0.9)),
                     at: g.point(a, g.radius * 0.94))
        }
        ctx.draw(Text("VU").font(.system(size: 13, weight: .bold, design: .serif)).foregroundColor(ink.opacity(0.85)),
                 at: CGPoint(x: size.width / 2, y: size.height * 0.56))
        ctx.draw(Text("DSS1 PRECISION").font(.system(size: 5.5, weight: .semibold, design: .serif)).foregroundColor(ink.opacity(0.6)),
                 at: CGPoint(x: size.width / 2, y: size.height * 0.72))
    }
}

/// The arc geometry shared by the printed scale and the moving needle.
private struct VUGeometry {
    let size: CGSize
    var pivot: CGPoint { CGPoint(x: size.width / 2, y: size.height * 1.12) }
    var radius: Double { Double(size.height) * 0.98 }
    private let sweep = 0.82

    func point(_ a: Double, _ r: Double) -> CGPoint {
        let t = -.pi / 2 - sweep + a * 2 * sweep
        return CGPoint(x: pivot.x + CGFloat(cos(t) * r), y: pivot.y + CGFloat(sin(t) * r))
    }
}

// MARK: - Moving parts

private struct DeckLive: View {
    let now: NowPlaying
    let readings: DeckReadings
    let resetCounter: () -> Void
    let actions: DeckActions

    var body: some View {
        ZStack(alignment: .topLeading) {
            Cassette(now: now, readings: readings).place(DeckLayout.cassette)

            Needle(value: readings.leftNeedle).place(DeckLayout.meterL.insetBy(dx: 7, dy: 7))
            Needle(value: readings.rightNeedle).place(DeckLayout.meterR.insetBy(dx: 7, dy: 7))
            PeakLamp(level: readings.peak).position(DeckLayout.peak)

            CounterWheels(value: readings.counter, reset: resetCounter).place(DeckLayout.counter)

            CompressorPanel().place(DeckLayout.comp)
            OptoPanel().place(DeckLayout.opto)

            Text(now.source)
                .font(.system(size: 11, weight: .black))
                .tracking(1.5)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
                .foregroundStyle(now.isChop ? Color(hex: 0xFF5A45) : Color(hex: 0xD9D9D4).opacity(0.85))
                .shadow(color: .black.opacity(0.8), radius: 0, y: -0.5)
                .frame(width: 190)
                .position(DeckLayout.source)

            InfoValues(now: now).place(CGRect(x: DeckLayout.info.minX, y: DeckLayout.info.minY + 22, width: DeckLayout.info.width, height: 42))

            PianoKeys(now: now, actions: actions).place(DeckLayout.keys)
            TrackKeys(actions: actions).place(DeckLayout.track)
        }
        .frame(width: DeckLayout.size.width, height: DeckLayout.size.height)
    }
}

/// COMPRESS · GAIN · ATTACK · RELEASE with a ratio switch and a tube-warmth toggle: the opto leveling amp.
private struct OptoPanel: View {
    @ObservedObject private var opto = OptoComp.shared

    var body: some View {
        VStack(spacing: 2) {
            HStack(spacing: 8) {
                Text("opto compressor").font(.system(size: 10)).engraved(0.6)
                Button { withAnimation(Motion.snappy) { opto.enabled.toggle() } } label: {
                    Circle()
                        .fill(opto.enabled ? Color(hex: 0xFFB238) : Color(hex: 0x2A2A2A))
                        .frame(width: 7, height: 7)
                        .shadow(color: opto.enabled ? Color(hex: 0xFFB238) : .clear, radius: 4)
                        .overlay(Circle().strokeBorder(.black.opacity(0.6), lineWidth: 1))
                        .padding(4)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .help(opto.enabled ? "Opto compressor on (click to bypass)" : "Opto compressor off (click to switch on)")
                Button { opto.warm.toggle() } label: {
                    Text("TUBE")
                        .font(.system(size: 7, weight: .black))
                        .tracking(0.8)
                        .foregroundStyle(opto.warm ? Color(hex: 0x1A1A1A) : Color(hex: 0xD9D9D4).opacity(0.6))
                        .padding(.horizontal, 5).padding(.vertical, 2)
                        .background(RoundedRectangle(cornerRadius: 2).fill(opto.warm ? Color(hex: 0xFFB238) : Color(hex: 0x262626)))
                }
                .buttonStyle(.plain)
                .help("Tube warmth: gentle even-harmonic saturation")
                Spacer(minLength: 0)
                ReductionLEDs(on: opto.enabled) { OptoComp.shared.reduction }
            }
            .padding(.horizontal, 10)
            HStack(alignment: .top, spacing: 6) {
                VStack(spacing: 8) {
                    PowerKey(on: $opto.enabled)
                    VStack(spacing: 3) {
                        HStack(spacing: 2) {
                            ForEach(OptoComp.ratios, id: \.self) { r in
                                Text("\(Int(r))")
                                    .font(.system(size: 9, weight: .black).monospacedDigit())
                                    .foregroundStyle(opto.ratio == r ? Color(hex: 0x1A1A1A) : Color(hex: 0xD9D9D4).opacity(0.7))
                                    .frame(width: 18, height: 16)
                                    .background(RoundedRectangle(cornerRadius: 2).fill(opto.ratio == r ? Color(hex: 0xFFB238) : Color(hex: 0x262626)))
                                    .contentShape(Rectangle())
                                    .onTapGesture { opto.ratio = r }
                            }
                        }
                        Text("ratio").font(.system(size: 9)).engraved(0.6)
                    }
                }
                .frame(width: 64)
                Group {
                    CompKnob(label: "compress", value: $opto.compress, range: 0...1, reset: 0.4, size: 42) { String(format: "%.0f%%", $0 * 100) }
                    CompKnob(label: "gain", value: $opto.gain, range: OptoComp.gainRange, reset: 3, size: 42) { String(format: "+%.0f dB", $0) }
                    CompKnob(label: "attack", value: $opto.attack, range: OptoComp.attackRange, reset: 8, logScale: true, size: 42) {
                        $0 < 10 ? String(format: "%.1f ms", $0) : String(format: "%.0f ms", $0)
                    }
                    CompKnob(label: "release", value: $opto.release, range: OptoComp.releaseRange, reset: 600, logScale: true, size: 42) {
                        $0 < 1000 ? String(format: "%.0f ms", $0) : String(format: "%.1f s", $0 / 1000)
                    }
                }
                .opacity(opto.enabled ? 1 : 0.55)
                .padding(.top, 6)
            }
        }
        .padding(.vertical, 6)
    }
}

/// THRESH · GAIN · ATTACK on machined knobs, an on/off lamp and a gain-reduction LED row.
/// Drives the library player's compressor only.
private struct CompressorPanel: View {
    @ObservedObject private var comp = LibraryCompressor.shared

    var body: some View {
        VStack(spacing: 5) {
            HStack(spacing: 7) {
                Text("compressor").font(.system(size: 10)).engraved(0.6)
                Button { withAnimation(Motion.snappy) { comp.enabled.toggle() } } label: {
                    Circle()
                        .fill(comp.enabled ? Color(hex: 0xFFB238) : Color(hex: 0x2A2A2A))
                        .frame(width: 8, height: 8)
                        .shadow(color: comp.enabled ? Color(hex: 0xFFB238) : .clear, radius: 4)
                        .overlay(Circle().strokeBorder(.black.opacity(0.6), lineWidth: 1))
                        .padding(4)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .help(comp.enabled ? "Compressor on (click to bypass)" : "Compressor off (click to switch on)")
            }
            Grid(horizontalSpacing: 18, verticalSpacing: 8) {
                GridRow {
                    CompKnob(label: "thresh", value: $comp.threshold, range: LibraryCompressor.thresholdRange, reset: -18, size: 42) {
                        String(format: "%.0f dB", $0)
                    }
                    .opacity(comp.enabled ? 1 : 0.55)
                    CompKnob(label: "gain", value: $comp.gain, range: LibraryCompressor.gainRange, reset: 4, size: 42) {
                        String(format: "+%.0f dB", $0)
                    }
                    .opacity(comp.enabled ? 1 : 0.55)
                }
                GridRow {
                    CompKnob(label: "attack", value: $comp.attack, range: LibraryCompressor.attackRange, reset: 10, logScale: true, size: 42) {
                        $0 < 10 ? String(format: "%.1f ms", $0) : String(format: "%.0f ms", $0)
                    }
                    .opacity(comp.enabled ? 1 : 0.55)
                    PowerKnob(on: $comp.enabled, size: 42)
                }
            }
            ReductionLEDs(on: comp.enabled)
        }
    }
}

/// The compressor's 4th "knob": a round silver push button that switches it on and off, amber ring when on.
private struct PowerKnob: View {
    @Binding var on: Bool
    var size: CGFloat = 42

    var body: some View {
        VStack(spacing: 3) {
            Button { withAnimation(Motion.snappy) { on.toggle() } } label: {
                ZStack {
                    Circle()
                        .fill(AngularGradient(colors: [Color(hex: 0xE8E8E4), Color(hex: 0x8C8C88), Color(hex: 0xF4F4F0), Color(hex: 0x7E7E7A), Color(hex: 0xE8E8E4)],
                                              center: .center))
                    Circle().strokeBorder(on ? Color(hex: 0xFFB238) : Color.black.opacity(0.55), lineWidth: on ? 2.5 : 1.2)
                        .shadow(color: on ? Color(hex: 0xFFB238) : .clear, radius: 5)
                    Image(systemName: "power")
                        .font(.system(size: size * 0.36, weight: .bold))
                        .foregroundStyle(on ? Color(hex: 0xE08A10) : Color(hex: 0x2A2A2A).opacity(0.7))
                }
                .frame(width: size, height: size)
                .shadow(color: .black.opacity(0.6), radius: 2, y: 2)
            }
            .buttonStyle(BouncyButtonStyle(pressedScale: 0.9))
            .help(on ? "Compressor on (click to bypass)" : "Compressor off (click to switch on)")
            Text(on ? "on" : "off")
                .font(.system(size: 9))
                .engraved(on ? 0.9 : 0.6)
        }
        .frame(width: max(44, size + 14))
    }
}

/// A big silver on/off key with an amber lamp, for the opto compressor.
private struct PowerKey: View {
    @Binding var on: Bool

    var body: some View {
        Button { withAnimation(Motion.snappy) { on.toggle() } } label: {
            ZStack {
                RoundedRectangle(cornerRadius: 3)
                    .fill(LinearGradient(colors: [Color(hex: 0x8E8E8A), Color(hex: 0x5A5A57)], startPoint: .top, endPoint: .bottom))
                    .frame(width: 60, height: 46)
                    .offset(y: 3)
                Image(decorative: Materials.aluminumSmall, scale: 2)
                    .resizable()
                    .frame(width: 60, height: 40)
                    .overlay(LinearGradient(colors: [.white.opacity(0.35), .clear, .black.opacity(0.12)], startPoint: .top, endPoint: .bottom))
                    .clipShape(RoundedRectangle(cornerRadius: 3))
                    .overlay(RoundedRectangle(cornerRadius: 3).strokeBorder(LinearGradient(colors: [.white.opacity(0.9), .black.opacity(0.35)], startPoint: .top, endPoint: .bottom), lineWidth: 1))
                VStack(spacing: 4) {
                    Capsule()
                        .fill(on ? Color(hex: 0xFFB238) : Color(hex: 0x3A3A3A))
                        .frame(width: 20, height: 4)
                        .shadow(color: on ? Color(hex: 0xFFB238) : .clear, radius: 4)
                    Text(on ? "ON" : "OFF")
                        .font(.system(size: 10, weight: .black))
                        .tracking(1.5)
                        .foregroundStyle(Color(hex: 0x2A2A2A).opacity(0.8))
                }
            }
            .shadow(color: .black.opacity(0.55), radius: 2, y: 2)
        }
        .buttonStyle(PianoKeyPress())
        .help(on ? "Opto compressor on (click to bypass)" : "Opto compressor off (click to switch on)")
    }
}

/// A small silver knob: drag up/down to turn, double-click to reset. Shows its value while turning.
private struct CompKnob: View {
    let label: String
    @Binding var value: Float
    let range: ClosedRange<Float>
    let reset: Float
    var logScale = false
    var size: CGFloat = 30
    let format: (Float) -> String
    @State private var dragStart: Double?

    private var fraction: Double {
        if logScale {
            return Double(log(value / range.lowerBound) / log(range.upperBound / range.lowerBound))
        }
        return Double((value - range.lowerBound) / (range.upperBound - range.lowerBound))
    }

    private func value(at f: Double) -> Float {
        let f = Float(min(max(f, 0), 1))
        if logScale { return range.lowerBound * pow(range.upperBound / range.lowerBound, f) }
        return range.lowerBound + (range.upperBound - range.lowerBound) * f
    }

    var body: some View {
        VStack(spacing: 3) {
            ZStack {
                Circle()
                    .fill(AngularGradient(colors: [Color(hex: 0xE8E8E4), Color(hex: 0x8C8C88), Color(hex: 0xF4F4F0), Color(hex: 0x7E7E7A), Color(hex: 0xE8E8E4)],
                                          center: .center))
                Circle()
                    .strokeBorder(Color.black.opacity(0.55), lineWidth: 1.2)
                Circle()
                    .fill(RadialGradient(colors: [.white.opacity(0.5), .clear], center: .init(x: 0.35, y: 0.3), startRadius: 0, endRadius: size * 0.47))
                    .padding(4)
                Capsule()
                    .fill(Color(hex: 0x141414))
                    .frame(width: size > 34 ? 2.5 : 2, height: size * 0.27)
                    .offset(y: -size * 0.27)
                    .rotationEffect(.degrees(-135 + 270 * fraction))
            }
            .frame(width: size, height: size)
            .shadow(color: .black.opacity(0.6), radius: 2, y: 2)
            .scaleEffect(dragStart == nil ? 1 : 1.06)
            .contentShape(Circle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        if dragStart == nil { dragStart = fraction }
                        value = value(at: (dragStart ?? 0) - g.translation.height / 160)
                    }
                    .onEnded { _ in dragStart = nil }
            )
            .simultaneousGesture(TapGesture(count: 2).onEnded { withAnimation(Motion.snappy) { value = reset } })
            .help("\(label): \(format(value)). Drag up/down; double-click to reset.")
            Text(dragStart == nil ? label : format(value))
                .font(.system(size: 9).monospacedDigit())
                .engraved(dragStart == nil ? 0.6 : 0.9)
                .fixedSize()
        }
        .frame(width: max(44, size + 14))
    }
}

/// Ten amber LEDs showing how many dB the compressor is taking off (2 dB each).
private struct ReductionLEDs: View {
    var on: Bool
    var read: () -> Float = { LibraryCompressor.shared.reduction }

    var body: some View {
        TimelineView(.periodic(from: .now, by: 1 / 20)) { _ in
            let db = on ? read() : 0
            HStack(spacing: 3) {
                ForEach(0..<10, id: \.self) { i in
                    let lit = db >= Float(i * 2) + 0.5
                    RoundedRectangle(cornerRadius: 1)
                        .fill(lit ? Color(hex: i < 6 ? 0xFFB238 : 0xFF5A3A) : Color(hex: 0x262626))
                        .frame(width: 8, height: 4)
                        .shadow(color: lit ? Color(hex: 0xFFB238).opacity(0.8) : .clear, radius: 2)
                }
            }
            .help(String(format: "Gain reduction: %.1f dB", db))
        }
    }
}

/// A slim black needle casting a soft shadow on the paper, pivoting from below the face.
private struct Needle: View {
    let value: Double

    var body: some View {
        Canvas { ctx, size in
            let g = VUGeometry(size: size)
            let tip = g.point(min(max(value, 0), 1.06), g.radius * 0.9)
            var needle = Path()
            needle.move(to: g.pivot)
            needle.addLine(to: tip)
            var shadow = ctx
            shadow.addFilter(.blur(radius: 1.2))
            shadow.translateBy(x: 2.5, y: 2)
            shadow.stroke(needle, with: .color(.black.opacity(0.3)), lineWidth: 1.4)
            ctx.stroke(needle, with: .color(Color(hex: 0x121212)), lineWidth: 1.2)
            // Pivot housing.
            let cap = CGRect(x: size.width / 2 - 20, y: size.height - 11, width: 40, height: 28)
            ctx.fill(Path(ellipseIn: cap), with: .linearGradient(Gradient(colors: [Color(hex: 0x3A3A3A), Color(hex: 0x0E0E0E)]),
                                                                 startPoint: CGPoint(x: cap.midX, y: cap.minY), endPoint: CGPoint(x: cap.midX, y: cap.maxY)))
        }
        .clipShape(RoundedRectangle(cornerRadius: 2))
    }
}

/// A red jewel lamp.
private struct PeakLamp: View {
    let level: Double

    var body: some View {
        ZStack {
            Circle().fill(Color(hex: 0x1A1A1A)).frame(width: 18, height: 18)
            Circle()
                .fill(RadialGradient(colors: [Color(hex: 0xFF8A7A).opacity(0.35 + 0.65 * level), Color(hex: 0xC1271C).opacity(0.45 + 0.55 * level), Color(hex: 0x4A0D08)],
                                     center: .init(x: 0.4, y: 0.35), startRadius: 0, endRadius: 7))
                .frame(width: 12, height: 12)
                .shadow(color: Color(hex: 0xFF3B2A).opacity(level), radius: 6)
            Circle().fill(.white.opacity(0.55)).frame(width: 3, height: 3).offset(x: -2, y: -2)
        }
    }
}

/// Mechanical counter: three number wheels behind a slot, and a bakelite reset knob.
private struct CounterWheels: View {
    let value: Int
    let reset: () -> Void

    var body: some View {
        HStack(spacing: 14) {
            HStack(spacing: 1.5) {
                ForEach(Array(String(format: "%03d", value).enumerated()), id: \.offset) { _, d in
                    Text(String(d))
                        .font(.system(size: 21, weight: .medium, design: .monospaced))
                        .foregroundStyle(Color(hex: 0xEDEDE6))
                        .contentTransition(.numericText())
                        .frame(width: 20, height: 30)
                        .background(LinearGradient(stops: [
                            .init(color: Color(hex: 0x050505), location: 0),
                            .init(color: Color(hex: 0x2E2E2E), location: 0.45),
                            .init(color: Color(hex: 0x2E2E2E), location: 0.55),
                            .init(color: Color(hex: 0x050505), location: 1),
                        ], startPoint: .top, endPoint: .bottom))
                        .overlay(LinearGradient(colors: [.black.opacity(0.55), .clear, .black.opacity(0.55)], startPoint: .top, endPoint: .bottom))
                }
            }
            .padding(3)
            .background(Color.black)
            .overlay(Rectangle().strokeBorder(LinearGradient(colors: [.black, .white.opacity(0.4)], startPoint: .top, endPoint: .bottom), lineWidth: 1))
            Button(action: reset) {
                ZStack {
                    Circle().fill(RadialGradient(colors: [Color(hex: 0x4A4A4A), Color(hex: 0x141414)], center: .init(x: 0.35, y: 0.3), startRadius: 1, endRadius: 14))
                    Circle().strokeBorder(Color.black, lineWidth: 1)
                    Ellipse().fill(.white.opacity(0.25)).frame(width: 8, height: 4).offset(x: -3, y: -5)
                }
                .frame(width: 24, height: 24)
                .shadow(color: .black.opacity(0.5), radius: 2, y: 2)
            }
            .buttonStyle(BouncyButtonStyle(pressedScale: 0.85))
            .help("Reset the counter")
        }
    }
}

private struct InfoValues: View {
    let now: NowPlaying

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 0) {
            value(now.bpm.map { String(format: "%.0f", $0) } ?? "––", "BPM").frame(width: 82, alignment: .leading)
            HStack(spacing: 6) {
                Text(now.key ?? "––")
                    .font(.system(size: 20, weight: .black))
                    .fontWidth(.condensed)
                if let camelot = now.camelot { CamelotChip(code: camelot) }
            }
            .frame(width: 112, alignment: .leading)
            value(clock(now.elapsed), "/ \(clock(now.duration))").frame(maxWidth: .infinity, alignment: .leading)
        }
        .foregroundStyle(Color(hex: 0xECECE7))
        .shadow(color: .black.opacity(0.8), radius: 0, y: -0.5)
        .padding(.horizontal, 14)
    }

    private func value(_ v: String, _ suffix: String) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 4) {
            Text(v).font(.system(size: 20, weight: .black).monospacedDigit()).fontWidth(.condensed)
            Text(suffix).font(.system(size: 9, weight: .bold).monospacedDigit()).opacity(0.55)
        }
    }
}

/// The cassette, open-reel style: a shell in this song's colourway, a paper label, and two metal
/// reel discs with three windows each, the wound tape showing through as it moves from one to the other.
private struct Cassette: View {
    let now: NowPlaying
    let readings: DeckReadings

    var body: some View {
        let c = readings.colors
        let ink = c.isLight ? Color(hex: 0x1D1A16) : Color(hex: 0xD8D5CC)
        GeometryReader { geo in
            let w = geo.size.width, h = geo.size.height
            let reelY = h * 0.6
            let gap = w * 0.245
            let discSize = h * 0.56
            ZStack {
                // Shell.
                RoundedRectangle(cornerRadius: 7)
                    .fill(LinearGradient(colors: [c.shellTop, c.shell, c.edge], startPoint: .top, endPoint: .bottom))
                GrainOverlay(image: Grain.dust).opacity(0.5).clipShape(RoundedRectangle(cornerRadius: 7))
                RoundedRectangle(cornerRadius: 7)
                    .strokeBorder(LinearGradient(colors: [.white.opacity(0.22), .black.opacity(0.55)], startPoint: .top, endPoint: .bottom), lineWidth: 1)

                // Paper label with printed stripes.
                ZStack(alignment: .leading) {
                    Image(decorative: Materials.labelPaper, scale: 2).resizable()
                    HStack(spacing: 0) {
                        Rectangle().fill(c.stripeA.opacity(0.94)).frame(width: 16)
                        VStack(alignment: .leading, spacing: 1) {
                            Text(now.title)
                                .font(.system(size: 15, weight: .black))
                                .fontWidth(.condensed)
                                .lineLimit(1)
                            Text(now.artist.uppercased())
                                .font(.system(size: 7.5, weight: .bold))
                                .tracking(1)
                                .lineLimit(1)
                                .opacity(0.65)
                        }
                        .foregroundStyle(Color(hex: 0x1D1A16).opacity(0.9))
                        .padding(.horizontal, 8)
                        Spacer(minLength: 0)
                        Text("C-60")
                            .font(.system(size: 8, weight: .black))
                            .foregroundStyle(Color(hex: 0x1D1A16).opacity(0.5))
                            .padding(.trailing, 6)
                        Rectangle().fill(c.stripeB.opacity(0.94)).frame(width: 16)
                    }
                }
                .frame(height: 38)
                .clipShape(RoundedRectangle(cornerRadius: 2))
                .shadow(color: .black.opacity(0.3), radius: 1, y: 1)
                .padding(.horizontal, 10)
                .frame(maxHeight: .infinity, alignment: .top)
                .padding(.top, 10)

                // Clear inner panel the reels sit behind.
                RoundedRectangle(cornerRadius: 6)
                    .fill(Color.black.opacity(c.isLight ? 0.28 : 0.4))
                    .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(Color.black.opacity(0.5), lineWidth: 1))
                    .frame(width: w - 24, height: h * 0.64)
                    .position(x: w / 2, y: reelY + 2)

                // Wound tape, seen through the reel windows.
                pack(h * (0.54 - 0.26 * now.progress)).position(x: w / 2 - gap, y: reelY)
                pack(h * (0.28 + 0.26 * now.progress)).position(x: w / 2 + gap, y: reelY)

                disc(c, size: discSize, angle: readings.leftReel, lines: [c.name.uppercased()])
                    .position(x: w / 2 - gap, y: reelY)
                disc(c, size: discSize, angle: readings.rightReel, lines: ["BIAS/HIGH", "EQ/70µs"])
                    .position(x: w / 2 + gap, y: reelY)

                // Base: moulded holes and screws.
                ForEach([w * 0.3, w * 0.7], id: \.self) { x in
                    Circle().fill(Color.black.opacity(0.75)).frame(width: 7, height: 7).position(x: x, y: h - 12)
                }
                ForEach([w * 0.42, w * 0.58], id: \.self) { x in
                    RoundedRectangle(cornerRadius: 1.5).fill(Color.black.opacity(0.75)).frame(width: 7, height: 7).position(x: x, y: h - 12)
                }
                ForEach([CGPoint(x: 9, y: 9), CGPoint(x: w - 9, y: 9), CGPoint(x: 9, y: h - 9), CGPoint(x: w - 9, y: h - 9), CGPoint(x: w / 2, y: h - 10)], id: \.x) { p in
                    Screw(angle: Double(p.x), size: 6, copper: true).position(p)
                }
                Text("A")
                    .font(.system(size: 13, weight: .black))
                    .foregroundStyle(ink.opacity(0.75))
                    .position(x: 20, y: h - 28)
                Text(now.format.isEmpty ? "TYPE I" : now.format)
                    .font(.system(size: 8, weight: .black))
                    .foregroundStyle(ink.opacity(0.55))
                    .position(x: w - 24, y: h - 28)
            }
        }
        .shadow(color: .black.opacity(0.5), radius: 4, y: 3)
    }

    /// Wound tape: glossy brown with a soft sheen.
    private func pack(_ size: CGFloat) -> some View {
        Circle()
            .fill(RadialGradient(colors: [Color(hex: 0x6A4632), Color(hex: 0x40281C), Color(hex: 0x24160F)], center: .init(x: 0.4, y: 0.35), startRadius: 0, endRadius: size * 0.6))
            .overlay(Circle().stroke(Color.white.opacity(0.08), lineWidth: 1).padding(size * 0.12))
            .frame(width: size, height: size)
    }

    /// A brushed-metal reel disc with three windows, a printed legend and a clear hub in a black ring.
    private func disc(_ c: CassetteColors, size: CGFloat, angle: Double, lines: [String]) -> some View {
        ZStack {
            // The sheen stays put while the disc turns, as light would.
            LinearGradient(stops: [
                .init(color: .white.opacity(0.24), location: 0),
                .init(color: .white.opacity(0.0), location: 0.42),
                .init(color: .black.opacity(0.22), location: 1),
            ], startPoint: .topLeading, endPoint: .bottomTrailing)
                .frame(width: size, height: size)
                .mask(ReelDisc().fill(Color.black, style: FillStyle(eoFill: true)).rotationEffect(.degrees(angle)))

            ZStack {
                ZStack {
                    // Brushed metal in this colourway: a solid colour with see-through streaks over it.
                    Circle().fill(c.disc)
                    Image(decorative: Materials.brushOverlay, scale: 2)
                        .resizable()
                        .scaledToFill()
                        .frame(width: size, height: size)
                        .clipped()
                    Circle().fill(AngularGradient(colors: [c.discLight.opacity(0.4), .clear, c.discDark.opacity(0.45), .clear, c.discLight.opacity(0.35), .clear, c.discLight.opacity(0.4)], center: .center))
                    ForEach([0.6, 0.74, 0.88], id: \.self) { r in
                        Circle().stroke(Color.black.opacity(0.07), lineWidth: 0.5).frame(width: size * r, height: size * r)
                    }
                    VStack(spacing: 0) {
                        ForEach(lines, id: \.self) { line in
                            Text(line)
                                .font(.system(size: 5, weight: .bold))
                                .tracking(0.4)
                                .foregroundStyle(Color.black.opacity(0.7))
                        }
                    }
                    .offset(y: size * 0.31)
                }
                .frame(width: size, height: size)
                .clipShape(ReelDisc(), style: FillStyle(eoFill: true))

                ReelDisc()
                    .stroke(Color.black.opacity(0.5), lineWidth: 0.8)
                    .frame(width: size, height: size)

                // Hub: black ring, clear centre, drive teeth.
                Circle().fill(Color(hex: 0x141414)).frame(width: size * 0.3, height: size * 0.3)
                    .overlay(Circle().strokeBorder(Color.white.opacity(0.12), lineWidth: 0.6))
                Circle().fill(Color(hex: 0xC4C7CA).opacity(0.85)).frame(width: size * 0.2, height: size * 0.2)
                ForEach(0..<6, id: \.self) { i in
                    RoundedRectangle(cornerRadius: 0.8)
                        .fill(Color(hex: 0x141414))
                        .frame(width: 2.6, height: 4.2)
                        .offset(y: -size * 0.075)
                        .rotationEffect(.degrees(Double(i) * 60))
                }
            }
            .rotationEffect(.degrees(angle))
        }
        .frame(width: size, height: size)
        .shadow(color: .black.opacity(0.35), radius: 2, y: 1)
    }
}

/// A reel disc: a circle with three wedge windows. Fill it with the even-odd rule so the windows are holes.
private struct ReelDisc: Shape {
    func path(in rect: CGRect) -> Path {
        var p = Path()
        p.addEllipse(in: rect)
        let cx = Double(rect.midX), cy = Double(rect.midY)
        let radius = Double(min(rect.width, rect.height)) / 2
        let inner = radius * 0.42, outer = radius * 0.9
        for k in 0..<3 {
            let centre = Double(k) * 120 + 30
            let a0 = (centre - 27) * Double.pi / 180
            let a1 = (centre + 27) * Double.pi / 180
            let steps = 14
            var points: [CGPoint] = []
            for i in 0...steps {
                let a = a0 + (a1 - a0) * Double(i) / Double(steps)
                points.append(CGPoint(x: cx + cos(a) * outer, y: cy + sin(a) * outer))
            }
            for i in 0...steps {
                let a = a1 - (a1 - a0) * Double(i) / Double(steps)
                points.append(CGPoint(x: cx + cos(a) * inner, y: cy + sin(a) * inner))
            }
            p.addLines(points)
            p.closeSubpath()
        }
        return p
    }
}

/// Piano-key transport: machined steel keys that sink when pressed, labels printed above.
private struct PianoKeys: View {
    let now: NowPlaying
    let actions: DeckActions

    var body: some View {
        HStack(spacing: 4) {
            key("rew", "backward.fill", lit: now.playing && now.rate < 0, learn: .transport(.rewind), action: actions.rewind)
            key("play", "play.fill", lit: now.playing && now.rate > 0 && now.rate <= 1.01, learn: .transport(.play), action: actions.play)
            key("pause", "pause.fill", lit: !now.playing && now.elapsed > 0, learn: .transport(.pause), action: actions.pause)
            key("stop", "stop.fill", learn: .transport(.stop), action: actions.stop)
            key("ff", "forward.fill", lit: now.playing && now.rate > 1.01, learn: .transport(.forward), action: actions.forward)
            key("eject", "eject.fill", action: actions.eject)
        }
    }

    private func key(_ label: String, _ symbol: String, lit: Bool = false, learn: MIDITarget? = nil, action: @escaping () -> Void) -> some View {
        SilverKey(label: label, symbol: symbol, lit: lit, learn: learn, action: action)
    }
}

/// Previous / next track: two silver keys that match the transport.
private struct TrackKeys: View {
    let actions: DeckActions

    var body: some View {
        HStack(spacing: 14) {
            SilverKey(label: "prev track", symbol: "backward.end.fill", width: 110, learn: .transport(.previous), action: actions.previousTrack)
            SilverKey(label: "next track", symbol: "forward.end.fill", width: 110, learn: .transport(.next), action: actions.nextTrack)
        }
    }
}

/// One brushed-steel piano key with an engraved label above it.
private struct SilverKey: View {
    let label: String
    let symbol: String
    var lit = false
    var width: CGFloat = 54
    var learn: MIDITarget? = nil
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            VStack(spacing: 5) {
                Text(label)
                    .font(.system(size: 9.5))
                    .engraved(0.7)
                ZStack(alignment: .top) {
                    // Key front (the lip you see below the top face).
                    RoundedRectangle(cornerRadius: 2)
                        .fill(LinearGradient(colors: [Color(hex: 0x8E8E8A), Color(hex: 0x5A5A57)], startPoint: .top, endPoint: .bottom))
                        .frame(width: width, height: 38)
                    // Top face: brushed steel.
                    Image(decorative: Materials.aluminumSmall, scale: 2)
                        .resizable()
                        .frame(width: width, height: 30)
                        .overlay(LinearGradient(colors: [.white.opacity(0.35), .clear, .black.opacity(0.12)], startPoint: .top, endPoint: .bottom))
                        .clipShape(RoundedRectangle(cornerRadius: 2))
                        .overlay(RoundedRectangle(cornerRadius: 2).strokeBorder(LinearGradient(colors: [.white.opacity(0.9), .black.opacity(0.35)], startPoint: .top, endPoint: .bottom), lineWidth: 1))
                    Image(systemName: symbol)
                        .font(.system(size: 10, weight: .bold))
                        .foregroundStyle(lit ? Color(hex: 0xC8321F) : Color(hex: 0x2A2A2A).opacity(0.75))
                        .shadow(color: lit ? Color(hex: 0xFF4A30).opacity(0.6) : .white.opacity(0.6), radius: lit ? 3 : 0, y: lit ? 0 : 1)
                        .frame(width: width, height: 30)
                }
                .shadow(color: .black.opacity(0.55), radius: 2, y: 2)
            }
        }
        .buttonStyle(PianoKeyPress())
        .contextMenu { if let learn { MIDILearnItem(target: learn) } }
    }
}

private struct PianoKeyPress: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .offset(y: configuration.isPressed ? 4 : 0)
            .brightness(configuration.isPressed ? -0.08 : 0)
            .animation(.spring(response: 0.16, dampingFraction: 0.5), value: configuration.isPressed)
    }
}

// MARK: - Glass (static, on top)

/// Smoked glass over the cassette and glass over the meters: reflections that sit above everything moving.
private struct DeckGlass: View {
    var body: some View {
        ZStack(alignment: .topLeading) {
            glass(corner: 3, strength: 1).place(DeckLayout.window)
            glass(corner: 2, strength: 0.7).place(DeckLayout.meterL.insetBy(dx: 7, dy: 7))
            glass(corner: 2, strength: 0.7).place(DeckLayout.meterR.insetBy(dx: 7, dy: 7))
        }
        .frame(width: DeckLayout.size.width, height: DeckLayout.size.height)
        .allowsHitTesting(false)
    }

    private func glass(corner: CGFloat, strength: Double) -> some View {
        ZStack {
            // Broad diagonal sheen and a crisp reflection band.
            LinearGradient(stops: [
                .init(color: .white.opacity(0.16 * strength), location: 0),
                .init(color: .white.opacity(0.03 * strength), location: 0.35),
                .init(color: .clear, location: 0.5),
                .init(color: .white.opacity(0.06 * strength), location: 0.85),
            ], startPoint: .topLeading, endPoint: .bottomTrailing)
            LinearGradient(stops: [
                .init(color: .clear, location: 0.18),
                .init(color: .white.opacity(0.12 * strength), location: 0.22),
                .init(color: .clear, location: 0.3),
            ], startPoint: .topLeading, endPoint: .bottomTrailing)
            RoundedRectangle(cornerRadius: corner).strokeBorder(Color.white.opacity(0.18 * strength), lineWidth: 0.6)
        }
        .clipShape(RoundedRectangle(cornerRadius: corner))
    }
}
