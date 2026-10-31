import Compression
import Foundation

// Akai MPC Sample project files (.xpj), following the format worked out by
// the MPC Sample Toolkit (github.com/tarikcampos/MPC-Sample-Toolkit):
// a gzip file holding five header lines ("ACVS", version, "SerialisableProjectData", "json", platform)
// followed by JSON. Projects are built by filling in a template saved from the MPC itself,
// because parts of the format are still unknown; anything we don't touch is kept exactly.

// MARK: - JSON that keeps key order and number spelling

indirect enum OrderedJSON {
    case object([(key: String, value: OrderedJSON)])
    case array([OrderedJSON])
    case string(String)
    /// Kept as written, so "1.0" stays "1.0" and "28" stays "28".
    case number(String)
    case bool(Bool)
    case null

    static func int(_ v: Int) -> OrderedJSON { .number(String(v)) }

    /// An object from an ordered literal: `.obj(["a": .int(1), "b": .bool(true)])`.
    static func obj(_ pairs: KeyValuePairs<String, OrderedJSON>) -> OrderedJSON {
        .object(pairs.map { (key: $0.key, value: $0.value) })
    }

    /// Python-style float spelling ("0.0", "0.5").
    static func double(_ v: Double) -> OrderedJSON {
        v == v.rounded() && abs(v) < 1e15 ? .number(String(format: "%.1f", v)) : .number("\(v)")
    }

    subscript(key: String) -> OrderedJSON? {
        get {
            guard case .object(let pairs) = self else { return nil }
            return pairs.first { $0.key == key }?.value
        }
        set {
            guard case .object(var pairs) = self else { return }
            if let i = pairs.firstIndex(where: { $0.key == key }) {
                if let newValue { pairs[i].value = newValue } else { pairs.remove(at: i) }
            } else if let newValue {
                pairs.append((key, newValue))
            }
            self = .object(pairs)
        }
    }

    var arrayValue: [OrderedJSON]? {
        if case .array(let items) = self { return items }
        return nil
    }

    var stringValue: String? {
        if case .string(let s) = self { return s }
        return nil
    }
}

enum XPJError: LocalizedError {
    case notGzip, badHeader, badJSON(String), missing(String), tooFewPads(Int)

    var errorDescription: String? {
        switch self {
        case .notGzip: return "That template isn't an MPC project file."
        case .badHeader: return "That template doesn't look like an MPC Sample project."
        case .badJSON(let why): return "Couldn't read the template (\(why))."
        case .missing(let what): return "The template is missing \(what)."
        case .tooFewPads(let n): return "The template only has \(n) pads."
        }
    }
}

struct OrderedJSONParser {
    private let bytes: [UInt8]
    private var i = 0

    static func parse(_ text: String) throws -> OrderedJSON {
        var parser = OrderedJSONParser(bytes: Array(text.utf8))
        parser.skipSpace()
        let value = try parser.value()
        return value
    }

    private init(bytes: [UInt8]) { self.bytes = bytes }

    private mutating func skipSpace() {
        while i < bytes.count, [0x20, 0x0A, 0x0D, 0x09].contains(bytes[i]) { i += 1 }
    }

    private mutating func expect(_ c: UInt8) throws {
        skipSpace()
        guard i < bytes.count, bytes[i] == c else { throw XPJError.badJSON("expected \(Character(UnicodeScalar(c))) at \(i)") }
        i += 1
    }

    private mutating func value() throws -> OrderedJSON {
        skipSpace()
        guard i < bytes.count else { throw XPJError.badJSON("unexpected end") }
        switch bytes[i] {
        case UInt8(ascii: "{"):
            i += 1
            var pairs: [(String, OrderedJSON)] = []
            skipSpace()
            if bytes[i] == UInt8(ascii: "}") { i += 1; return .object([]) }
            while true {
                skipSpace()
                let key = try string()
                try expect(UInt8(ascii: ":"))
                pairs.append((key, try value()))
                skipSpace()
                if bytes[i] == UInt8(ascii: ",") { i += 1; continue }
                try expect(UInt8(ascii: "}"))
                return .object(pairs.map { (key: $0.0, value: $0.1) } as [(key: String, value: OrderedJSON)])
            }
        case UInt8(ascii: "["):
            i += 1
            var items: [OrderedJSON] = []
            skipSpace()
            if bytes[i] == UInt8(ascii: "]") { i += 1; return .array([]) }
            while true {
                items.append(try value())
                skipSpace()
                if bytes[i] == UInt8(ascii: ",") { i += 1; continue }
                try expect(UInt8(ascii: "]"))
                return .array(items)
            }
        case UInt8(ascii: "\""):
            return .string(try string())
        case UInt8(ascii: "t"):
            i += 4; return .bool(true)
        case UInt8(ascii: "f"):
            i += 5; return .bool(false)
        case UInt8(ascii: "n"):
            i += 4; return .null
        default:
            let start = i
            while i < bytes.count, "+-0123456789.eE".utf8.contains(bytes[i]) { i += 1 }
            guard i > start else { throw XPJError.badJSON("bad value at \(start)") }
            return .number(String(decoding: bytes[start..<i], as: UTF8.self))
        }
    }

    private mutating func string() throws -> String {
        guard i < bytes.count, bytes[i] == UInt8(ascii: "\"") else { throw XPJError.badJSON("expected string at \(i)") }
        i += 1
        var out = [UInt8]()
        while i < bytes.count, bytes[i] != UInt8(ascii: "\"") {
            if bytes[i] == UInt8(ascii: "\\") {
                i += 1
                let c = bytes[i]
                switch c {
                case UInt8(ascii: "n"): out.append(0x0A)
                case UInt8(ascii: "r"): out.append(0x0D)
                case UInt8(ascii: "t"): out.append(0x09)
                case UInt8(ascii: "b"): out.append(0x08)
                case UInt8(ascii: "f"): out.append(0x0C)
                case UInt8(ascii: "u"):
                    let hex = String(decoding: bytes[(i + 1)...(i + 4)], as: UTF8.self)
                    i += 4
                    var code = UInt32(hex, radix: 16) ?? 0x3F
                    // Surrogate pair.
                    if (0xD800...0xDBFF).contains(code), bytes[i + 1] == UInt8(ascii: "\\"), bytes[i + 2] == UInt8(ascii: "u") {
                        let low = UInt32(String(decoding: bytes[(i + 3)...(i + 6)], as: UTF8.self), radix: 16) ?? 0
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00)
                        i += 6
                    }
                    out.append(contentsOf: Array(String(UnicodeScalar(code).map(Character.init) ?? "?").utf8))
                default: out.append(c)
                }
                i += 1
            } else {
                out.append(bytes[i])
                i += 1
            }
        }
        i += 1
        return String(decoding: out, as: UTF8.self)
    }
}

extension OrderedJSON {
    /// Same layout as Python's `json.dumps(indent=0, ensure_ascii=False)`, which the MPC files use.
    func serialized() -> String {
        var out = ""
        write(into: &out)
        return out
    }

    private func write(into out: inout String) {
        switch self {
        case .object(let pairs):
            if pairs.isEmpty { out += "{}"; return }
            out += "{\n"
            for (n, pair) in pairs.enumerated() {
                Self.writeString(pair.key, into: &out)
                out += ": "
                pair.value.write(into: &out)
                out += n < pairs.count - 1 ? ",\n" : "\n"
            }
            out += "}"
        case .array(let items):
            if items.isEmpty { out += "[]"; return }
            out += "[\n"
            for (n, item) in items.enumerated() {
                item.write(into: &out)
                out += n < items.count - 1 ? ",\n" : "\n"
            }
            out += "]"
        case .string(let s): Self.writeString(s, into: &out)
        case .number(let n): out += n
        case .bool(let b): out += b ? "true" : "false"
        case .null: out += "null"
        }
    }

    private static func writeString(_ s: String, into out: inout String) {
        out += "\""
        for scalar in s.unicodeScalars {
            switch scalar {
            case "\"": out += "\\\""
            case "\\": out += "\\\\"
            case "\n": out += "\\n"
            case "\r": out += "\\r"
            case "\t": out += "\\t"
            case "\u{08}": out += "\\b"
            case "\u{0C}": out += "\\f"
            default:
                if scalar.value < 0x20 {
                    out += String(format: "\\u%04x", scalar.value)
                } else {
                    out.unicodeScalars.append(scalar)
                }
            }
        }
        out += "\""
    }
}

// MARK: - Gzip

enum Gzip {
    private static let crcTable: [UInt32] = (0..<256).map { n -> UInt32 in
        var c = UInt32(n)
        for _ in 0..<8 { c = c & 1 != 0 ? 0xEDB88320 ^ (c >> 1) : c >> 1 }
        return c
    }

    static func crc32(_ data: Data) -> UInt32 {
        var c: UInt32 = 0xFFFFFFFF
        for b in data { c = crcTable[Int((c ^ UInt32(b)) & 0xFF)] ^ (c >> 8) }
        return c ^ 0xFFFFFFFF
    }

    static func compress(_ data: Data) -> Data {
        var deflated = Data(count: data.count + 4096)
        let size = deflated.withUnsafeMutableBytes { dst in
            data.withUnsafeBytes { src in
                compression_encode_buffer(dst.bindMemory(to: UInt8.self).baseAddress!, dst.count,
                                          src.bindMemory(to: UInt8.self).baseAddress!, src.count,
                                          nil, COMPRESSION_ZLIB)
            }
        }
        deflated.count = size
        // Header: magic, deflate, no flags, mtime 0, max compression, unknown OS (like Python's gzip).
        var out = Data([0x1F, 0x8B, 0x08, 0x00, 0, 0, 0, 0, 0x02, 0xFF])
        out.append(deflated)
        var crc = crc32(data).littleEndian
        var length = UInt32(truncatingIfNeeded: data.count).littleEndian
        out.append(Data(bytes: &crc, count: 4))
        out.append(Data(bytes: &length, count: 4))
        return out
    }

    static func decompress(_ data: Data) throws -> Data {
        let b = [UInt8](data)
        guard b.count > 18, b[0] == 0x1F, b[1] == 0x8B, b[2] == 0x08 else { throw XPJError.notGzip }
        let flags = b[3]
        var p = 10
        if flags & 0x04 != 0 { p += 2 + Int(b[p]) | Int(b[p + 1]) << 8 }   // extra
        if flags & 0x08 != 0 { while b[p] != 0 { p += 1 }; p += 1 }         // file name
        if flags & 0x10 != 0 { while b[p] != 0 { p += 1 }; p += 1 }         // comment
        if flags & 0x02 != 0 { p += 2 }                                     // header crc
        let n = b.count
        let expected = Int(b[n - 4]) | Int(b[n - 3]) << 8 | Int(b[n - 2]) << 16 | Int(b[n - 1]) << 24
        var out = Data(count: max(expected, 1) + 16)
        let body = Data(b[p..<(n - 8)])
        let size = out.withUnsafeMutableBytes { dst in
            body.withUnsafeBytes { src in
                compression_decode_buffer(dst.bindMemory(to: UInt8.self).baseAddress!, dst.count,
                                          src.bindMemory(to: UInt8.self).baseAddress!, src.count,
                                          nil, COMPRESSION_ZLIB)
            }
        }
        guard size > 0 else { throw XPJError.notGzip }
        out.count = size
        return out
    }
}

// MARK: - Project

struct XPJProject {
    var header: [String]
    var root: OrderedJSON

    init(contentsOf url: URL) throws {
        let text = String(decoding: try Gzip.decompress(Data(contentsOf: url)), as: UTF8.self)
        let lines = text.split(separator: "\n", maxSplits: 5, omittingEmptySubsequences: false).map(String.init)
        guard lines.count == 6, lines[0] == "ACVS", lines[2] == "SerialisableProjectData", lines[3] == "json" else {
            throw XPJError.badHeader
        }
        header = Array(lines[0..<5])
        root = try OrderedJSONParser.parse(lines[5])
    }

    func write(to url: URL) throws {
        let payload = header.joined(separator: "\n") + "\n" + root.serialized()
        try Gzip.compress(Data(payload.utf8)).write(to: url)
    }

    var padCount: Int {
        root["data"]?["tracks"]?.arrayValue?.first?["program"]?["drum"]?["instruments"]?.arrayValue?.count ?? 0
    }

    /// Puts a WAV (already in the ProjectData folder) on a pad: pad 0 = A01 … 127 = H16.
    mutating func place(fileName: String, frames: Int, onPad pad: Int) throws {
        guard var data = root["data"] else { throw XPJError.missing("project data") }
        guard var tracks = data["tracks"]?.arrayValue, var track = tracks.first else { throw XPJError.missing("a track") }
        guard var program = track["program"], var drum = program["drum"],
              var instruments = drum["instruments"]?.arrayValue else { throw XPJError.missing("a drum program") }
        guard instruments.indices.contains(pad) else { throw XPJError.tooFewPads(instruments.count) }
        var instrument = instruments[pad]
        guard var layers = instrument["layersv"]?.arrayValue, var layer = layers.first else { throw XPJError.missing("pad layers") }

        let name = (fileName as NSString).deletingPathExtension
        layer["active"] = .bool(true)
        layer["sampleName"] = .string(name)
        layer["sampleFile"] = .string(fileName)
        layer["sampleStart"] = .int(0)
        layer["sampleEnd"] = .int(0)
        layer["sliceIndex"] = .int(129)
        layer["sliceInfo"] = .obj([
            "Start": .int(0), "End": .int(frames - 1), "LoopStart": .int(0), "LoopMode": .int(0),
            "PulsePosition": .int(0), "LoopCrossfadeLength": .int(0), "LoopCrossfadeType": .int(0),
            "TailLength": .double(0), "TailLoopPosition": .double(0.5),
        ])

        layers[0] = layer
        instrument["layersv"] = .array(layers)
        instruments[pad] = instrument
        drum["instruments"] = .array(instruments)
        program["drum"] = drum
        track["program"] = program

        // The sample pools, project-wide and on the track, each get an entry (replacing any same-named one).
        let entry = OrderedJSON.obj([
            "loadImpl": .int(0),
            "metadata": .obj(["key": .string("G# Major"), "rootNote": .int(60), "tempo": .double(0), "tune": .double(0)]),
            "name": .string(name), "path": .string(fileName), "version": .int(1),
        ])
        func pooled(_ pool: OrderedJSON?) -> OrderedJSON {
            var items = (pool?.arrayValue ?? []).filter { $0["name"]?.stringValue != name && $0["path"]?.stringValue != fileName }
            items.append(entry)
            return .array(items)
        }
        track["samples"] = pooled(track["samples"])
        tracks[0] = track
        data["tracks"] = .array(tracks)
        data["samples"] = pooled(data["samples"])
        root["data"] = data
    }
}
