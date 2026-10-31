import Foundation

/// Writes Serato DJ hot cues and saved loops into a copy of a song, the way Serato stores them itself:
/// an ID3 `GEOB` frame named "Serato Markers2" (layout per the community-documented format,
/// github.com/Holzhaus/serato-tags). WAV and AIFF carry the ID3 tag in a chunk, MP3 at the start of the file;
/// anything else is converted to WAV first.
enum SeratoExport {
    struct Cue {
        var index: Int        // 0…7 → hot cue 1…8
        var ms: UInt32
        var name: String
    }

    struct Loop {
        var index: Int
        var startMs: UInt32
        var endMs: UInt32
        var name: String
    }

    enum Failure: LocalizedError {
        case unreadable
        var errorDescription: String? { "Couldn't read the song to tag it" }
    }

    /// Serato's default hot-cue colours, by cue number.
    static let cueColors: [UInt32] = [0xCC0000, 0xCC4400, 0xCC8800, 0xCCCC00, 0x00CC00, 0x00CCCC, 0x0000CC, 0xCC00CC]

    // MARK: - Exporting

    /// Copies `source` into `folder` (as WAV when it isn't WAV, AIFF or MP3) with the cues and loops, replacing an
    /// earlier export of the same song. Returns the new file.
    static func export(_ source: URL, to folder: URL, cues: [Cue], loops: [Loop]) throws -> URL {
        let fm = FileManager.default
        try fm.createDirectory(at: folder, withIntermediateDirectories: true)
        let ext = source.pathExtension.lowercased()
        let stem = source.deletingPathExtension().lastPathComponent
        let kind: Container
        switch ext {
        case "wav": kind = .wav
        case "aif", "aiff": kind = .aiff
        case "mp3": kind = .mp3
        default: kind = .wav
        }
        let target = folder.appendingPathComponent("\(stem).\(kind == .wav ? "wav" : ext)")
        let temp = folder.appendingPathComponent(".\(UUID().uuidString).\(target.pathExtension)")
        defer { try? fm.removeItem(at: temp) }
        if ["wav", "aif", "aiff", "mp3"].contains(ext) {
            try fm.copyItem(at: source, to: temp)
        } else {
            try ImportEngine.convert(source, to: temp)
        }
        var data = try Data(contentsOf: temp)
        let geob = geobFrameBody(markers2(cues: cues, loops: loops))
        switch kind {
        case .wav: data = try retag(riff: data, geob: geob, bigEndian: false)
        case .aiff: data = try retag(riff: data, geob: geob, bigEndian: true)
        case .mp3: data = retag(mp3: data, geob: geob)
        }
        try data.write(to: temp)
        if fm.fileExists(atPath: target.path) { try fm.removeItem(at: target) }
        try fm.moveItem(at: temp, to: target)
        return target
    }

    private enum Container { case wav, aiff, mp3 }

    // MARK: - Serato Markers2

    /// The decoded payload: version, track colour, cues, loops, BPM lock, terminator.
    static func markers2(cues: [Cue], loops: [Loop]) -> Data {
        var out = Data([0x01, 0x01])
        func entry(_ name: String, _ body: Data) {
            out.append(contentsOf: Array(name.utf8) + [0])
            out.append(be32(UInt32(body.count)))
            out.append(body)
        }
        entry("COLOR", Data([0x00, 0xFF, 0xFF, 0xFF]))
        for cue in cues.sorted(by: { $0.index < $1.index }) {
            var b = Data([0x00, UInt8(cue.index)])
            b.append(be32(cue.ms))
            let rgb = cueColors[cue.index % cueColors.count]
            b.append(contentsOf: [0x00, UInt8((rgb >> 16) & 0xFF), UInt8((rgb >> 8) & 0xFF), UInt8(rgb & 0xFF), 0x00, 0x00])
            b.append(contentsOf: Array(cue.name.utf8) + [0])
            entry("CUE", b)
        }
        for loop in loops.sorted(by: { $0.index < $1.index }) {
            var b = Data([0x00, UInt8(loop.index)])
            b.append(be32(loop.startMs))
            b.append(be32(loop.endMs))
            b.append(contentsOf: [0xFF, 0xFF, 0xFF, 0xFF])
            b.append(contentsOf: [0x00, 0x27, 0xAA, 0xE1])   // ARGB: Serato's loop blue
            b.append(contentsOf: [0x00, 0x00])               // unknown, locked = false
            b.append(contentsOf: Array(loop.name.utf8) + [0])
            entry("LOOP", b)
        }
        entry("BPMLOCK", Data([0x00]))
        out.append(0x00)
        return out
    }

    /// GEOB body: text encoding, MIME type, file name, description, then `01 01` + Serato's base64
    /// ('=' written as 'A', a line feed every 72 characters), null-padded to 470 bytes.
    static func geobFrameBody(_ payload: Data) -> Data {
        var b64 = Array(payload.base64EncodedString().replacingOccurrences(of: "=", with: "A").utf8)
        var i = 72
        while i < b64.count {
            b64.insert(0x0A, at: i)
            i += 73
        }
        var content = Data([0x01, 0x01]) + Data(b64)
        if content.count < 470 { content.append(Data(count: 470 - content.count)) } else { content.append(0) }
        var body = Data([0x00])
        body.append(contentsOf: Array("application/octet-stream".utf8) + [0])
        body.append(0)                                                  // no file name
        body.append(contentsOf: Array("Serato Markers2".utf8) + [0])
        body.append(content)
        return body
    }

    // MARK: - ID3

    /// A new ID3v2 tag: every frame of `existing` (if any) except an old Serato Markers2, plus ours.
    static func id3Tag(existing: Data?, geob: Data) -> Data {
        var major: UInt8 = 4
        var frames = Data()
        if let tag = existing, tag.count >= 10, tag.prefix(3) == Data("ID3".utf8) {
            let version = tag[tag.startIndex + 3]
            let flags = tag[tag.startIndex + 5]
            // Unsynchronised or extended-header tags are rare; rebuild those from scratch.
            if (version == 3 || version == 4), flags & 0xC0 == 0 {
                major = version
                let size = syncsafe(tag, at: 6)
                let end = min(tag.count, 10 + size)
                var p = 10
                while p + 10 <= end {
                    let base = tag.startIndex + p
                    let id = tag[base..<base + 4]
                    if id.first == 0 { break }
                    let fsize = version == 4 ? syncsafe(tag, at: p + 4) : Int(readBE32(tag, at: p + 4))
                    let next = p + 10 + fsize
                    guard fsize > 0, next <= end else { break }
                    let frame = tag[base..<tag.startIndex + next]
                    if !(id == Data("GEOB".utf8) && isSeratoMarkers2(frame.dropFirst(10))) {
                        frames.append(frame)
                    }
                    p = next
                }
            }
        }
        var geobFrame = Data("GEOB".utf8)
        geobFrame.append(major == 4 ? syncsafeBytes(geob.count) : be32(UInt32(geob.count)))
        geobFrame.append(contentsOf: [0x00, 0x00])
        geobFrame.append(geob)
        frames.append(geobFrame)
        var tag = Data("ID3".utf8)
        tag.append(contentsOf: [major, 0x00, 0x00])
        tag.append(syncsafeBytes(frames.count))
        tag.append(frames)
        return tag
    }

    private static func isSeratoMarkers2(_ body: Data) -> Bool {
        let bytes = Array(body)
        guard bytes.count > 1 else { return false }
        // encoding, MIME\0, filename\0, description\0
        var i = 1
        for _ in 0..<2 {
            guard let z = bytes[i...].firstIndex(of: 0) else { return false }
            i = z + 1
        }
        guard i < bytes.count, let z = bytes[i...].firstIndex(of: 0) else { return false }
        return String(decoding: bytes[i..<z], as: UTF8.self) == "Serato Markers2"
    }

    // MARK: - Containers

    /// WAV (RIFF, little-endian) or AIFF (FORM, big-endian): replaces or adds the `id3 ` / `ID3 ` chunk.
    static func retag(riff data: Data, geob: Data, bigEndian: Bool) throws -> Data {
        let bytes = [UInt8](data)
        guard bytes.count >= 12 else { throw Failure.unreadable }
        func size(at p: Int) -> Int {
            let v = UInt32(bytes[p]) << 24 | UInt32(bytes[p + 1]) << 16 | UInt32(bytes[p + 2]) << 8 | UInt32(bytes[p + 3])
            return Int(bigEndian ? v : v.byteSwapped)
        }
        var chunks: [(id: [UInt8], body: ArraySlice<UInt8>)] = []
        var existing: Data?
        var p = 12
        while p + 8 <= bytes.count {
            let id = Array(bytes[p..<p + 4])
            let len = size(at: p + 4)
            let end = min(bytes.count, p + 8 + len)
            let body = bytes[(p + 8)..<end]
            let name = String(decoding: id, as: UTF8.self)
            if name == "id3 " || name == "ID3 " {
                existing = Data(body)
            } else {
                chunks.append((id, body))
            }
            p = p + 8 + len + (len & 1)
        }
        let tag = id3Tag(existing: existing, geob: geob)
        chunks.append((Array((bigEndian ? "ID3 " : "id3 ").utf8), ArraySlice(tag)))
        var out = [UInt8](bytes[0..<12])
        func put(_ v: Int) {
            let u = UInt32(v)
            out.append(contentsOf: bigEndian
                ? [UInt8(u >> 24), UInt8((u >> 16) & 0xFF), UInt8((u >> 8) & 0xFF), UInt8(u & 0xFF)]
                : [UInt8(u & 0xFF), UInt8((u >> 8) & 0xFF), UInt8((u >> 16) & 0xFF), UInt8(u >> 24)])
        }
        for c in chunks {
            out.append(contentsOf: c.id)
            put(c.body.count)
            out.append(contentsOf: c.body)
            if c.body.count & 1 == 1 { out.append(0) }
        }
        // Fix the RIFF / FORM size.
        let total = UInt32(out.count - 8)
        let sizeBytes: [UInt8] = bigEndian
            ? [UInt8(total >> 24), UInt8((total >> 16) & 0xFF), UInt8((total >> 8) & 0xFF), UInt8(total & 0xFF)]
            : [UInt8(total & 0xFF), UInt8((total >> 8) & 0xFF), UInt8((total >> 16) & 0xFF), UInt8(total >> 24)]
        out.replaceSubrange(4..<8, with: sizeBytes)
        return Data(out)
    }

    /// MP3: swaps the ID3v2 tag at the front for one with the Serato frame (other frames kept).
    static func retag(mp3 data: Data, geob: Data) -> Data {
        var existing: Data?
        var audioStart = 0
        if data.count >= 10, data.prefix(3) == Data("ID3".utf8) {
            let footer = data[data.startIndex + 5] & 0x10 != 0
            audioStart = min(data.count, 10 + syncsafe(data, at: 6) + (footer ? 10 : 0))
            existing = data.prefix(audioStart)
        }
        return id3Tag(existing: existing, geob: geob) + data.dropFirst(audioStart)
    }

    // MARK: - Bytes

    private static func be32(_ v: UInt32) -> Data {
        Data([UInt8(v >> 24), UInt8((v >> 16) & 0xFF), UInt8((v >> 8) & 0xFF), UInt8(v & 0xFF)])
    }

    private static func readBE32(_ d: Data, at p: Int) -> UInt32 {
        let b = d.startIndex + p
        return UInt32(d[b]) << 24 | UInt32(d[b + 1]) << 16 | UInt32(d[b + 2]) << 8 | UInt32(d[b + 3])
    }

    private static func syncsafe(_ d: Data, at p: Int) -> Int {
        let b = d.startIndex + p
        return Int(d[b] & 0x7F) << 21 | Int(d[b + 1] & 0x7F) << 14 | Int(d[b + 2] & 0x7F) << 7 | Int(d[b + 3] & 0x7F)
    }

    private static func syncsafeBytes(_ v: Int) -> Data {
        Data([UInt8((v >> 21) & 0x7F), UInt8((v >> 14) & 0x7F), UInt8((v >> 7) & 0x7F), UInt8(v & 0x7F)])
    }
}
