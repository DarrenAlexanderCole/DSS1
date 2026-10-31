import CoreMIDI
import SwiftUI

/// Transport actions, from MIDI buttons or MMC messages.
enum Transport: String, Codable, Hashable, CaseIterable {
    case play, pause, playPause, stop, rewind, forward, loop, retrigger, next, previous

    var label: String {
        switch self {
        case .play: return "PLAY"
        case .pause: return "PAUSE"
        case .playPause: return "play / pause"
        case .stop: return "STOP"
        case .rewind: return "REW"
        case .forward: return "FF"
        case .loop: return "LOOP"
        case .retrigger: return "RETRIGGER"
        case .next: return "next track"
        case .previous: return "previous track"
        }
    }
}

/// Something on screen a MIDI control can drive.
enum MIDITarget: Codable, Hashable {
    /// A chop pad, by absolute index (bank × 16 + pad).
    case pad(Int)
    case fxDepth(FXKind)
    case fxToggle(FXKind)
    case latch
    case padX
    case padY
    case playPause
    case transport(Transport)
    /// The big encoder: twists the depth of the selected FX knob.
    case encoder
    /// Clicking the encoder switches the selected FX on / off.
    case encoderPress
    /// The slider: Chop's pitch.
    case pitch

    var label: String {
        switch self {
        case .pad(let i): return "pad \(ChopSession.bankLetters[i / 16])\(String(format: "%02d", i % 16 + 1))"
        case .fxDepth(let k): return "\(k.label) depth"
        case .fxToggle(let k): return "\(k.label) on/off"
        case .latch: return "LATCH"
        case .padX: return "pad X"
        case .padY: return "pad Y"
        case .playPause: return "play / pause"
        case .transport(let t): return t.label
        case .encoder: return "FX encoder"
        case .encoderPress: return "FX encoder click"
        case .pitch: return "pitch slider"
        }
    }
}

/// A note, a CC, the pitch bend wheel, or another message a controller might send (channel ignored).
struct MIDIControl: Codable, Hashable {
    enum Kind: String, Codable { case note, cc, bend, mmc, program, aftertouch, polyAftertouch, nrpn, sysex }
    var kind: Kind
    var number: Int
}

enum MIDIEvent {
    case noteOn(note: Int, velocity: Int)
    case noteOff(note: Int)
    case cc(number: Int, value: Int)
    /// 0…16383, centre 8192.
    case bend(value: Int)
    /// A MIDI Machine Control message (many controllers' transport buttons).
    case transport(Transport)
    case program(Int)
    /// Channel pressure, 0…127.
    case aftertouch(Int)
    case polyAftertouch(note: Int, value: Int)
    /// An NRPN / RPN parameter: a new value (0…127) or a ±1 step (data increment / decrement).
    case nrpn(number: Int, value: Int?, step: Int?)
    /// Any other SysEx, as its bytes (without F0 / F7).
    case sysex([UInt8])

    var control: MIDIControl {
        switch self {
        case .noteOn(let n, _), .noteOff(let n): return MIDIControl(kind: .note, number: n)
        case .cc(let n, _): return MIDIControl(kind: .cc, number: n)
        case .bend: return MIDIControl(kind: .bend, number: 0)
        case .transport(let t): return MIDIControl(kind: .mmc, number: Transport.allCases.firstIndex(of: t) ?? 0)
        case .program: return MIDIControl(kind: .program, number: 0)
        case .aftertouch: return MIDIControl(kind: .aftertouch, number: 0)
        case .polyAftertouch(let n, _): return MIDIControl(kind: .polyAftertouch, number: n)
        case .nrpn(let n, _, _): return MIDIControl(kind: .nrpn, number: n)
        case .sysex(let bytes):
            var hash = 5381
            for b in bytes { hash = (hash &* 33) &+ Int(b) }
            return MIDIControl(kind: .sysex, number: hash & 0x7FFF_FFFF)
        }
    }
}

/// One line of the MIDI monitor.
struct MIDILogEntry: Identifiable {
    let id: Int
    let time: Date
    let port: String
    let hex: String
    let meaning: String
    var action: String
}

/// USB MIDI, plug and play. Lists every MIDI source, follows devices being plugged in and out,
/// picks an Akai (MPD / MPK / MPC) automatically, maps it out of the box, and lets any pad,
/// knob, LATCH or pad axis learn a control (right-click → MIDI Learn).
@MainActor
final class MIDIHub: ObservableObject {
    static let shared = MIDIHub()

    /// A device, with every one of its input ports (Akai units have several).
    struct Source: Identifiable, Hashable {
        let id: MIDIUniqueID
        let name: String
        let endpoints: [MIDIEndpointRef]
    }

    /// The choice in the dropdown: automatic, none, or one device.
    enum Choice: Hashable {
        case auto, none
        case device(MIDIUniqueID)
    }

    @Published private(set) var sources: [Source] = []
    @Published var choice: Choice {
        didSet {
            switch choice {
            case .auto: defaults.set("auto", forKey: "midiSource")
            case .none: defaults.set("none", forKey: "midiSource")
            case .device(let id): defaults.set(String(id), forKey: "midiSource")
            }
            connect()
        }
    }
    /// The device actually listened to.
    @Published private(set) var connected: Source?
    /// Bumped on every incoming message, for the activity lamp.
    @Published private(set) var activity = 0
    /// While set, the next message is bound to this target.
    @Published var learning: MIDITarget?
    @Published private(set) var learned: [MIDIControl: MIDITarget] = [:]
    /// The last message received, for a couple of seconds: how to see what a control sends.
    @Published private(set) var lastMessage = ""
    /// Everything received lately, newest last (the MIDI Monitor window).
    @Published private(set) var log: [MIDILogEntry] = []
    /// The encoder sends 0…127 positions instead of +/− steps.
    @Published var encoderAbsolute: Bool = UserDefaults.standard.bool(forKey: "midiEncoderAbsolute") {
        didSet { UserDefaults.standard.set(encoderAbsolute, forKey: "midiEncoderAbsolute") }
    }
    private var clearMessage: Task<Void, Never>?
    private var logCount = 0
    /// NRPN / RPN parameter selected by CC 99/98 (or 101/100), waiting for its data.
    private var parameterMSB: Int?
    private var parameterLSB: Int?
    /// Where events go (the app model sets this); returns what it did, for the monitor.
    var onEvent: ((MIDIEvent, MIDITarget?) -> String?)?

    private var client = MIDIClientRef()
    private var port = MIDIPortRef()
    private var listening: [MIDIEndpointRef] = []
    private var portNames: [MIDIEndpointRef: String] = [:]
    private let defaults = UserDefaults.standard

    private init() {
        switch defaults.string(forKey: "midiSource") ?? "auto" {
        case "auto": choice = .auto
        case "none": choice = .none
        case let s: choice = Int32(s).map { .device($0) } ?? .auto
        }
        if let data = defaults.data(forKey: "midiLearned"),
           let saved = try? JSONDecoder().decode([MIDIControl: MIDITarget].self, from: data) {
            learned = saved
        }
        MIDIClientCreateWithBlock("DSS1" as CFString, &client) { note in
            guard note.pointee.messageID == .msgSetupChanged else { return }
            Task { @MainActor in MIDIHub.shared.refresh() }
        }
        MIDIInputPortCreateWithProtocol(client, "DSS1 In" as CFString, ._1_0, &port) { list, refCon in
            let incoming = MIDIHub.parse(list)
            guard !incoming.isEmpty else { return }
            let endpoint = MIDIEndpointRef(UInt(bitPattern: refCon))
            Task { @MainActor in
                for m in incoming { MIDIHub.shared.receive(m.event, bytes: m.bytes, from: endpoint) }
            }
        }
        refresh()
    }

    // MARK: - Devices

    private static func name(of object: MIDIObjectRef) -> String? {
        var name: Unmanaged<CFString>?
        guard MIDIObjectGetStringProperty(object, kMIDIPropertyDisplayName, &name) == noErr else { return nil }
        return name?.takeRetainedValue() as String?
    }

    func refresh() {
        var devices: [MIDIUniqueID: (name: String, endpoints: [MIDIEndpointRef])] = [:]
        var order: [MIDIUniqueID] = []
        var names: [MIDIEndpointRef: String] = [:]
        for i in 0..<MIDIGetNumberOfSources() {
            let endpoint = MIDIGetSource(i)
            let portName = Self.name(of: endpoint) ?? "MIDI port"
            names[endpoint] = portName
            // Group ports by their device; a virtual source is its own device.
            var entity = MIDIEntityRef(), device = MIDIDeviceRef()
            var owner: MIDIObjectRef = endpoint
            if MIDIEndpointGetEntity(endpoint, &entity) == noErr, entity != 0,
               MIDIEntityGetDevice(entity, &device) == noErr, device != 0 {
                owner = device
            }
            var id: MIDIUniqueID = 0
            MIDIObjectGetIntegerProperty(owner, kMIDIPropertyUniqueID, &id)
            if devices[id] == nil {
                devices[id] = (owner == endpoint ? portName : (Self.name(of: owner) ?? portName), [])
                order.append(id)
            }
            devices[id]?.endpoints.append(endpoint)
        }
        portNames = names
        sources = order.compactMap { id in devices[id].map { Source(id: id, name: $0.name, endpoints: $0.endpoints) } }
        connected = nil
        connect()
    }

    private func connect() {
        let target: Source?
        switch choice {
        case .none: target = nil
        case .device(let id):
            // Older versions saved a port's id: find the device that has that port.
            target = sources.first { $0.id == id } ?? sources.first { s in
                s.endpoints.contains { e in
                    var eid: MIDIUniqueID = 0
                    MIDIObjectGetIntegerProperty(e, kMIDIPropertyUniqueID, &eid)
                    return eid == id
                }
            }
        case .auto:
            target = sources.first { s in ["MPD", "MPK", "MPC", "AKAI"].contains { s.name.uppercased().contains($0) } }
                ?? sources.first { !$0.name.contains("IAC") }
        }
        guard target != connected || (target != nil && listening.isEmpty) else { return }
        for e in listening { MIDIPortDisconnectSource(port, e) }
        listening = []
        connected = nil
        guard let target else { return }
        for e in target.endpoints {
            MIDIPortConnectSource(port, e, UnsafeMutableRawPointer(bitPattern: UInt(e)))
            listening.append(e)
        }
        connected = target
    }

    // MARK: - Messages

    struct Incoming {
        var event: MIDIEvent?
        var bytes: [UInt8]
    }

    /// MIDI 1.0 messages out of a UMP event list: channel voice (notes, CC, bend, program, pressure) and SysEx.
    nonisolated private static func parse(_ list: UnsafePointer<MIDIEventList>) -> [Incoming] {
        var out: [Incoming] = []
        var sysex: [UInt8] = []
        for packet in list.unsafeSequence() {
            let words = Array(MIDIEventPacket.WordCollection(packet))
            var i = 0
            while i < words.count {
                let word = words[i]
                switch word >> 28 {
                case 0x2:
                    let status = Int((word >> 16) & 0xF0)
                    let raw = UInt8((word >> 16) & 0xFF)
                    let d1 = Int((word >> 8) & 0x7F), d2 = Int(word & 0x7F)
                    let three = [raw, UInt8(d1), UInt8(d2)], two = [raw, UInt8(d1)]
                    switch status {
                    case 0x90 where d2 > 0: out.append(Incoming(event: .noteOn(note: d1, velocity: d2), bytes: three))
                    case 0x90, 0x80: out.append(Incoming(event: .noteOff(note: d1), bytes: three))
                    case 0xA0: out.append(Incoming(event: .polyAftertouch(note: d1, value: d2), bytes: three))
                    case 0xB0: out.append(Incoming(event: .cc(number: d1, value: d2), bytes: three))
                    case 0xC0: out.append(Incoming(event: .program(d1), bytes: two))
                    case 0xD0: out.append(Incoming(event: .aftertouch(d1), bytes: two))
                    case 0xE0: out.append(Incoming(event: .bend(value: d1 | (d2 << 7)), bytes: three))
                    default: out.append(Incoming(event: nil, bytes: three))
                    }
                    i += 1
                case 0x3:
                    // 7-bit SysEx, possibly split over several packets (start, continue, end).
                    guard i + 1 < words.count else { i += 1; break }
                    let next = words[i + 1]
                    let count = Int((word >> 16) & 0xF)
                    let shifts: [(UInt32, UInt32)] = [(word, 8), (word, 0), (next, 24), (next, 16), (next, 8), (next, 0)]
                    let state = (word >> 20) & 0xF
                    if state == 0 || state == 1 { sysex = [] }
                    for (w, shift) in shifts.prefix(count) { sysex.append(UInt8((w >> shift) & 0xFF)) }
                    if state == 0 || state == 3 {
                        out.append(Incoming(event: Self.sysexEvent(sysex), bytes: [0xF0] + sysex + [0xF7]))
                        sysex = []
                    }
                    i += 2
                case 0x1:
                    // System real-time: Start / Continue / Stop drive the transport; clock and the rest are too
                    // chatty to log.
                    switch (word >> 16) & 0xFF {
                    case 0xFA, 0xFB: out.append(Incoming(event: .transport(.play), bytes: [UInt8((word >> 16) & 0xFF)]))
                    case 0xFC: out.append(Incoming(event: .transport(.stop), bytes: [0xFC]))
                    default: break
                    }
                    i += 1
                case 0x4:
                    i += 2
                default:
                    i += 1
                }
            }
        }
        return out
    }

    /// MMC (7F <device> 06 <command>) becomes transport; anything else stays SysEx.
    nonisolated private static func sysexEvent(_ b: [UInt8]) -> MIDIEvent {
        if b.count >= 4, b[0] == 0x7F, b[2] == 0x06 {
            switch b[3] {
            case 0x01: return .transport(.stop)
            case 0x02, 0x03: return .transport(.play)    // 03 = deferred play (what the MPC Sample sends)
            case 0x04: return .transport(.forward)
            case 0x05: return .transport(.rewind)
            case 0x09: return .transport(.pause)
            default: break
            }
        }
        return .sysex(b)
    }

    private static func describe(_ event: MIDIEvent) -> String {
        switch event {
        case .noteOn(let n, let v): return "NOTE \(n) ON \(v)"
        case .noteOff(let n): return "NOTE \(n) OFF"
        case .cc(let n, let v): return "CC \(n) = \(v)"
        case .bend(let v): return "BEND \(v)"
        case .transport(let t): return "MMC \(t.label.uppercased())"
        case .program(let p): return "PROGRAM \(p)"
        case .aftertouch(let v): return "PRESSURE \(v)"
        case .polyAftertouch(let n, let v): return "POLY PRESSURE \(n) = \(v)"
        case .nrpn(let n, let v, let st):
            if let st { return "NRPN \(n) \(st > 0 ? "+1" : "−1")" }
            return "NRPN \(n) = \(v ?? 0)"
        case .sysex(let b): return "SYSEX \(b.count) bytes"
        }
    }

    /// CC 99/98 (NRPN) or 101/100 (RPN) select a parameter; CC 6 / 38 then set it and 96 / 97 step it.
    private func assembleParameter(_ event: MIDIEvent) -> MIDIEvent? {
        guard case .cc(let n, let v) = event else { return event }
        switch n {
        case 99, 101: parameterMSB = v; return nil
        case 98, 100: parameterLSB = v; return nil
        case 6, 38, 96, 97:
            guard parameterMSB != nil || parameterLSB != nil else { return event }
            let number = ((parameterMSB ?? 0) << 7) | (parameterLSB ?? 0)
            switch n {
            case 96: return .nrpn(number: number, value: nil, step: 1)
            case 97: return .nrpn(number: number, value: nil, step: -1)
            default: return .nrpn(number: number, value: v, step: nil)
            }
        default: return event
        }
    }

    func receive(_ raw: MIDIEvent?, bytes: [UInt8] = [], from endpoint: MIDIEndpointRef = 0) {
        activity &+= 1
        let hex = bytes.map { String(format: "%02X", $0) }.joined(separator: " ")
        let portName = portNames[endpoint] ?? (endpoint == 0 ? "test" : "port")
        let event = raw.flatMap { assembleParameter($0) }
        let meaning = event.map(Self.describe) ?? (raw == nil ? "(not understood)" : "(parameter select)")
        lastMessage = event.map(Self.describe) ?? lastMessage
        clearMessage?.cancel()
        clearMessage = Task { [weak self] in
            try? await Task.sleep(for: .seconds(2.2))
            if !Task.isCancelled { self?.lastMessage = "" }
        }
        var action = "unmapped"
        defer {
            logCount += 1
            log.append(MIDILogEntry(id: logCount, time: Date(), port: portName, hex: hex, meaning: meaning, action: action))
            if log.count > 200 { log.removeFirst(log.count - 200) }
        }
        guard let event else { action = "—"; return }
        if let target = learning {
            if case .noteOff = event { action = "ignored while learning"; return }
            // A knob being learned shouldn't also be stolen by a pad mapping and vice versa.
            learned = learned.filter { $0.value != target }
            learned[event.control] = target
            learning = nil
            if let data = try? JSONEncoder().encode(learned) { defaults.set(data, forKey: "midiLearned") }
            action = "learned → \(target.label)"
            return
        }
        var target = learned[event.control]
        // Data increment and decrement are one control: learning either covers both.
        if target == nil, case .cc(let n, _) = event, n == 96 || n == 97 {
            target = learned[MIDIControl(kind: .cc, number: n == 96 ? 97 : 96)]
        }
        if let done = onEvent?(event, target) { action = "→ \(done)" }
    }

    func clearLearned() {
        learned = [:]
        defaults.removeObject(forKey: "midiLearned")
    }

    func clearLog() { log = [] }

    /// The monitor log as plain text, for pasting into a message.
    var logText: String {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss.SSS"
        let device = connected.map { "\($0.name) (\($0.endpoints.count) port\($0.endpoints.count == 1 ? "" : "s"))" } ?? "no device"
        return (["DSS1 MIDI log · \(device)"] + log.map { "\(f.string(from: $0.time))  \($0.port)  \($0.hex)  \($0.meaning)  \($0.action)" })
            .joined(separator: "\n")
    }

    /// For tests: hand an event in as if it came from the device.
    func inject(_ event: MIDIEvent) { receive(event) }

    /// For tests: raw MIDI 1.0 bytes, as a device would send them.
    func injectBytes(_ bytes: [UInt8]) {
        guard let status = bytes.first else { return }
        let d1 = bytes.count > 1 ? Int(bytes[1]) : 0, d2 = bytes.count > 2 ? Int(bytes[2]) : 0
        let event: MIDIEvent?
        switch status & 0xF0 {
        case 0x90: event = d2 > 0 ? .noteOn(note: d1, velocity: d2) : .noteOff(note: d1)
        case 0x80: event = .noteOff(note: d1)
        case 0xA0: event = .polyAftertouch(note: d1, value: d2)
        case 0xB0: event = .cc(number: d1, value: d2)
        case 0xC0: event = .program(d1)
        case 0xD0: event = .aftertouch(d1)
        case 0xE0: event = .bend(value: d1 | (d2 << 7))
        case 0xF0 where status == 0xF0: event = Self.sysexEvent(Array(bytes.dropFirst().prefix { $0 != 0xF7 }))
        default: event = nil
        }
        receive(event, bytes: bytes)
    }

    // MARK: - Akai preset

    /// MPK mini knobs (CC 70–77) and MPD218 knobs (CC 3, 9, 12–17), in FX order.
    static let knobCCs: [[Int]] = [[70, 3], [71, 9], [72, 12], [73, 13], [74, 14], [75, 15], [76, 16], [77, 17]]

    static func presetFX(forCC cc: Int) -> FXKind? {
        guard let i = knobCCs.firstIndex(where: { $0.contains(cc) }) else { return nil }
        return FXKind.allCases[i]
    }
}

// MARK: - Dropdown

/// Device dropdown with an activity lamp; shows "MOVE A CONTROL" while learning.
struct MIDIChip: View {
    @ObservedObject private var hub = MIDIHub.shared
    @Environment(\.openWindow) private var openWindow
    @State private var flash = false

    var body: some View {
        Menu {
            Picker("MIDI device", selection: $hub.choice) {
                Text("Automatic").tag(MIDIHub.Choice.auto)
                Text("None").tag(MIDIHub.Choice.none)
                if !hub.sources.isEmpty { Divider() }
                ForEach(hub.sources) { s in Text(s.endpoints.count > 1 ? "\(s.name) (\(s.endpoints.count) ports)" : s.name).tag(MIDIHub.Choice.device(s.id)) }
            }
            .pickerStyle(.inline)
            Divider()
            Button("MIDI Monitor…") { openWindow(id: "midiMonitor") }
            Menu("MIDI Learn") {
                MIDILearnItem(target: .encoder)
                MIDILearnItem(target: .encoderPress)
                MIDILearnItem(target: .pitch)
                Divider()
                ForEach(Transport.allCases, id: \.self) { MIDILearnItem(target: .transport($0)) }
            }
            Toggle("Encoder sends absolute values", isOn: $hub.encoderAbsolute)
            Divider()
            if hub.learning != nil {
                Button("Cancel MIDI Learn") { hub.learning = nil }
            }
            Button("Clear MIDI Mappings (\(hub.learned.count))") { hub.clearLearned() }
                .disabled(hub.learned.isEmpty)
        } label: {
            HStack(spacing: 6) {
                Circle()
                    .fill(hub.learning != nil ? Palette.red : (flash ? Color(hex: 0x3FD15A) : Palette.cream.opacity(hub.connected == nil ? 0.15 : 0.35)))
                    .frame(width: 7, height: 7)
                    .shadow(color: flash ? Color(hex: 0x3FD15A) : .clear, radius: 3)
                Text(label)
                    .font(.system(size: 10, weight: .black))
                    .tracking(1.2)
                    .lineLimit(1)
            }
            .foregroundStyle(Palette.cream.opacity(0.9))
            .padding(.horizontal, 12)
            .padding(.vertical, 7)
            .background(Capsule().fill(hub.learning != nil ? Palette.red.opacity(0.25) : Color.black.opacity(0.4)))
            .overlay(Capsule().strokeBorder(Color.white.opacity(0.12), lineWidth: 1))
        }
        .menuStyle(.button)
        .buttonStyle(.plain)
        .fixedSize()
        .help("MIDI controller. Right-click any pad, knob, LATCH or the XY pad to MIDI Learn.")
        .onChange(of: hub.activity) { _, _ in
            flash = true
            Task {
                try? await Task.sleep(for: .milliseconds(90))
                flash = false
            }
        }
    }

    private var label: String {
        if let t = hub.learning { return "MOVE A CONTROL → \(t.label.uppercased())" }
        guard let c = hub.connected else { return hub.sources.isEmpty ? "NO MIDI" : "MIDI OFF" }
        return hub.lastMessage.isEmpty ? c.name.uppercased() : hub.lastMessage
    }
}

/// "MIDI Learn" for a context menu.
struct MIDILearnItem: View {
    let target: MIDITarget

    var body: some View {
        Button("MIDI Learn \(target.label)…") { MIDIHub.shared.learning = target }
        if let control = MIDIHub.shared.learned.first(where: { $0.value == target })?.key {
            Text("Mapped to \(control.kind == .note ? "note" : control.kind == .cc ? "CC" : "pitch bend") \(control.kind == .bend ? "" : String(control.number))")
        }
    }
}
