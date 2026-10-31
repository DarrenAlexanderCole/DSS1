import AVFoundation
import Foundation
import os

enum ImportPhase: Sendable {
    case extracting(name: String, done: Int, total: Int)
    case copying(done: Int, total: Int, name: String)
}

struct ImportOutcome: Sendable {
    var imported = 0
    var skipped = 0
    var failed = 0
    /// Files that wouldn't decode (damaged or not really audio); they're left out.
    var damaged: [String] = []
    /// Every audio file from the source as it now sits in the destination,
    /// including ones that were already there.
    var tracks: [URL] = []
}

enum ImportError: LocalizedError {
    case unzipFailed(String)

    var errorDescription: String? {
        switch self {
        case .unzipFailed(let name): return "Couldn't open \(name)"
        }
    }
}

/// Pulls audio files out of a zip, folder, or loose file and drops them flat into `destination`.
/// Folder structure inside the zip is thrown away: only the audio files land, nothing else.
/// Nested zips are opened too (a few levels deep); Mac junk like `__MACOSX` and `._` files is ignored.
///
/// Every file is checked (it must decode), optionally converted to a 24-bit WAV, and landed:
/// a file already in the destination is left alone, and a *different* file with the same name
/// (e.g. CLEAN/x.wav and INSTRUMENTAL/x.wav) is kept as "x [INSTRUMENTAL].wav".
/// Checking and converting run on all cores.
struct ImportEngine: Sendable {
    let destination: URL
    let extensions: Set<String>
    var convertToWAV = false

    private struct Candidate: Sendable {
        let url: URL
        let fromTemp: Bool
        /// The folder it sat in, used to tell same-named files apart.
        let folder: String?
    }

    /// What the check found out about a candidate.
    private struct Probe: Sendable {
        var ok = false
        var duration = 0.0
        var needsConversion = false
    }

    private var fm: FileManager { .default }

    func run(source: URL, onProgress: @escaping @Sendable (ImportPhase) -> Void) throws -> ImportOutcome {
        let workDir = fm.temporaryDirectory.appendingPathComponent("DSS1-\(UUID().uuidString)")
        defer { try? fm.removeItem(at: workDir) }

        var candidates: [Candidate] = []
        try collect(source, workDir: workDir, depth: 0, fromTemp: false, into: &candidates, onProgress: onProgress)

        var outcome = ImportOutcome()
        try fm.createDirectory(at: destination, withIntermediateDirectories: true)
        let total = candidates.count
        let progress = Progress(total: total, report: onProgress)

        // 1. Check every file decodes (in parallel).
        var probes = [Probe](repeating: Probe(), count: total)
        probes.withUnsafeMutableBufferPointer { out in
            let slots = out
            DispatchQueue.concurrentPerform(iterations: total) { i in
                slots[i] = probe(candidates[i].url)
                progress.tick(candidates[i].url.lastPathComponent, weight: 0.3)
            }
        }

        // 2. Decide where each one lands (in order, so names never collide).
        var jobs: [(index: Int, target: URL)] = []
        var claimed: [String: Claim] = [:]
        for (i, item) in candidates.enumerated() {
            guard probes[i].ok else {
                outcome.damaged.append(item.url.lastPathComponent)
                progress.tick(item.url.lastPathComponent, weight: 0.7)
                continue
            }
            switch landing(for: item, probe: probes[i], claimed: claimed) {
            case .existing(let url):
                outcome.skipped += 1
                outcome.tracks.append(url)
                progress.tick(item.url.lastPathComponent, weight: 0.7)
            case .new(let url):
                claimed[url.lastPathComponent.lowercased()] = Claim(size: fileSize(item.url), duration: probes[i].duration)
                jobs.append((i, url))
            }
        }

        // 3. Copy, move or convert (in parallel).
        let landed = OSAllocatedUnfairLock(initialState: [(Int, URL?)]())
        DispatchQueue.concurrentPerform(iterations: jobs.count) { j in
            let (i, target) = jobs[j]
            let item = candidates[i]
            let ok: Bool
            if probes[i].needsConversion {
                ok = (try? Self.convert(item.url, to: target)) != nil
                if !ok { try? fm.removeItem(at: target) }
            } else if item.fromTemp {
                ok = (try? fm.moveItem(at: item.url, to: target)) != nil
            } else {
                ok = (try? fm.copyItem(at: item.url, to: target)) != nil
            }
            landed.withLock { $0.append((i, ok ? target : nil)) }
            progress.tick(item.url.lastPathComponent, weight: 0.7)
        }
        for (_, url) in landed.withLock({ $0 }).sorted(by: { $0.0 < $1.0 }) {
            if let url {
                outcome.imported += 1
                outcome.tracks.append(url)
            } else {
                outcome.failed += 1
            }
        }

        onProgress(.copying(done: total, total: total, name: ""))
        return outcome
    }

    // MARK: - Checking & converting

    /// Opens the file and reads a moment from the start and the end; anything that throws is damaged.
    private func probe(_ url: URL) -> Probe {
        guard let file = try? AVAudioFile(forReading: url), file.length > 0 else { return Probe() }
        let format = file.processingFormat
        let rate = format.sampleRate
        let chunk = AVAudioFrameCount(min(Double(file.length), rate))
        guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: chunk) else { return Probe() }
        do {
            try file.read(into: buffer, frameCount: chunk)
            if file.length > AVAudioFramePosition(chunk) {
                file.framePosition = file.length - AVAudioFramePosition(chunk)
                try file.read(into: buffer, frameCount: chunk)
            }
        } catch {
            return Probe()
        }
        let ext = url.pathExtension.lowercased()
        return Probe(ok: true, duration: Double(file.length) / rate,
                     needsConversion: convertToWAV && ext != "wav")
    }

    /// Decodes any format AVFoundation reads and writes it as 24-bit PCM WAV at the same sample rate,
    /// a second at a time.
    static func convert(_ source: URL, to target: URL) throws {
        let input = try AVAudioFile(forReading: source)
        let format = input.processingFormat
        let settings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: format.sampleRate,
            AVNumberOfChannelsKey: format.channelCount,
            AVLinearPCMBitDepthKey: 24,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsBigEndianKey: false,
            AVLinearPCMIsNonInterleaved: false,
        ]
        let output = try AVAudioFile(forWriting: target, settings: settings,
                                     commonFormat: .pcmFormatFloat32, interleaved: false)
        let chunk = AVAudioFrameCount(format.sampleRate)
        guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: chunk) else { return }
        while input.framePosition < input.length {
            try input.read(into: buffer, frameCount: chunk)
            if buffer.frameLength == 0 { break }
            try output.write(from: buffer)
        }
    }

    private static func duration(of url: URL) -> Double? {
        guard let file = try? AVAudioFile(forReading: url) else { return nil }
        return Double(file.length) / file.processingFormat.sampleRate
    }

    // MARK: - Landing

    /// A name an earlier file in this batch is about to land under.
    private struct Claim {
        let size: Int?
        let duration: Double
    }

    private enum Landing {
        case new(URL)
        case existing(URL)
    }

    /// Where a file should go, or the copy of it that's already there.
    /// Tries the plain name, then "name [FOLDER]", then "name (2)", "name (3)"…
    /// "Already there" means the same size, or for a conversion, a WAV of the same length.
    private func landing(for item: Candidate, probe: Probe, claimed: [String: Claim]) -> Landing {
        let base = item.url.deletingPathExtension().lastPathComponent
        let ext = probe.needsConversion ? "wav" : item.url.pathExtension
        var names = ["\(base).\(ext)"]
        if let folder = item.folder { names.append("\(base) [\(folder)].\(ext)") }
        names += (2...99).map { "\(base) (\($0)).\(ext)" }

        let size = probe.needsConversion ? nil : fileSize(item.url)
        for candidate in names {
            let url = destination.appendingPathComponent(candidate)
            // The same file twice in one drop (e.g. CLEAN/ and DIRTY/ copies) lands once.
            if let other = claimed[candidate.lowercased()] {
                let same = probe.needsConversion ? abs(other.duration - probe.duration) < 0.05
                                                 : (other.size == fileSize(item.url) && other.size != nil)
                if same { return .existing(url) }
                continue
            }
            guard fm.fileExists(atPath: url.path) else { return .new(url) }
            if probe.needsConversion {
                if let d = Self.duration(of: url), abs(d - probe.duration) < 0.05 { return .existing(url) }
            } else if fileSize(url) == size {
                return .existing(url)
            }
        }
        return .existing(destination.appendingPathComponent(names[0]))
    }

    private func fileSize(_ url: URL) -> Int? {
        (try? url.resourceValues(forKeys: [.fileSizeKey]))?.fileSize
    }

    // MARK: - Collecting

    private func collect(_ url: URL, workDir: URL, depth: Int, fromTemp: Bool,
                         into list: inout [Candidate], onProgress: @escaping @Sendable (ImportPhase) -> Void) throws {
        var isDir: ObjCBool = false
        guard fm.fileExists(atPath: url.path, isDirectory: &isDir) else { return }

        if isDir.boolValue {
            guard let walker = fm.enumerator(at: url, includingPropertiesForKeys: [.isRegularFileKey],
                                             options: [.skipsHiddenFiles, .skipsPackageDescendants]) else { return }
            var files: [URL] = []
            for case let file as URL in walker {
                if file.lastPathComponent == "__MACOSX" {
                    walker.skipDescendants()
                    continue
                }
                if (try? file.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true {
                    files.append(file)
                }
            }
            for file in files.sorted(by: { $0.path < $1.path }) {
                try consider(file, root: url, workDir: workDir, depth: depth, fromTemp: fromTemp, into: &list, onProgress: onProgress)
            }
        } else {
            try consider(url, root: url.deletingLastPathComponent(), workDir: workDir, depth: depth, fromTemp: fromTemp, into: &list, onProgress: onProgress)
        }
    }

    private func consider(_ file: URL, root: URL, workDir: URL, depth: Int, fromTemp: Bool,
                          into list: inout [Candidate], onProgress: @escaping @Sendable (ImportPhase) -> Void) throws {
        let name = file.lastPathComponent
        if name.hasPrefix("._") || file.pathComponents.contains("__MACOSX") { return }

        let ext = file.pathExtension.lowercased()
        if extensions.contains(ext) {
            let parent = file.deletingLastPathComponent()
            let folder = parent.standardizedFileURL == root.standardizedFileURL ? nil : parent.lastPathComponent
            list.append(Candidate(url: file, fromTemp: fromTemp, folder: folder))
        } else if ext == "zip" && depth < 4 {
            onProgress(.extracting(name: name, done: 0, total: 0))
            let out = workDir.appendingPathComponent(UUID().uuidString)
            do {
                try unzip(file, to: out, onProgress: onProgress)
            } catch {
                // A broken zip at the top level is an error; a broken one nested inside is just skipped.
                if depth == 0 { throw error }
                return
            }
            try collect(out, workDir: workDir, depth: depth + 1, fromTemp: true, into: &list, onProgress: onProgress)
        }
    }

    // MARK: - Unzipping

    /// Extracts only the audio (and any zips inside), reporting each file as it comes out.
    /// Falls back to `ditto` for archives `unzip` can't handle.
    private func unzip(_ zip: URL, to out: URL, onProgress: @escaping @Sendable (ImportPhase) -> Void) throws {
        try fm.createDirectory(at: out, withIntermediateDirectories: true)
        let wanted = extensions.union(["zip"])
        let entries = (try? lines(of: "/usr/bin/zipinfo", ["-1", zip.path])) ?? []
        let total = entries.filter { entry in
            let e = (entry as NSString).pathExtension.lowercased()
            return wanted.contains(e) && !entry.contains("__MACOSX") && !(entry as NSString).lastPathComponent.hasPrefix("._")
        }.count
        if total > 0 {
            // -C: patterns match case-insensitively, so *.wav also takes .WAV.
            let patterns = wanted.sorted().map { "*.\($0)" }
            var done = 0
            var last = Date.distantPast
            _ = try? run("/usr/bin/unzip", ["-o", "-C", zip.path] + patterns + ["-x", "__MACOSX/*", "*/._*", "._*", "-d", out.path]) { line in
                guard line.contains("inflating:") || line.contains("extracting:") else { return }
                done += 1
                if Date().timeIntervalSince(last) > 0.08 || done == total {
                    last = Date()
                    let file = (line.components(separatedBy: ": ").last ?? "").trimmingCharacters(in: .whitespaces)
                    onProgress(.extracting(name: (file as NSString).lastPathComponent, done: min(done, total), total: total))
                }
            }
            // unzip exits non-zero for mere warnings, so judge it by what came out.
            if done > 0 { return }
        }
        try? fm.removeItem(at: out)
        try fm.createDirectory(at: out, withIntermediateDirectories: true)
        if (try? run("/usr/bin/ditto", ["-x", "-k", zip.path, out.path])) == true { return }
        throw ImportError.unzipFailed(zip.lastPathComponent)
    }

    private func lines(of tool: String, _ args: [String]) throws -> [String] {
        var out: [String] = []
        _ = try run(tool, args) { out.append($0) }
        return out
    }

    /// Runs a tool, handing each line of its output to `onLine` as it arrives.
    @discardableResult
    private func run(_ path: String, _ args: [String], onLine: ((String) -> Void)? = nil) throws -> Bool {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: path)
        process.arguments = args
        let pipe = Pipe()
        process.standardOutput = onLine == nil ? FileHandle.nullDevice : pipe
        process.standardError = FileHandle.nullDevice
        try process.run()
        if let onLine {
            var pending = Data()
            let handle = pipe.fileHandleForReading
            while true {
                let data = handle.availableData
                if data.isEmpty { break }
                pending.append(data)
                while let nl = pending.firstIndex(of: 0x0A) {
                    let line = String(decoding: pending[pending.startIndex..<nl], as: UTF8.self)
                    pending.removeSubrange(pending.startIndex...nl)
                    onLine(line)
                }
            }
            if !pending.isEmpty { onLine(String(decoding: pending, as: UTF8.self)) }
        }
        process.waitUntilExit()
        return process.terminationStatus == 0
    }
}

/// Counts finished work across threads and reports it about ten times a second.
private final class Progress: @unchecked Sendable {
    private let total: Int
    private let report: @Sendable (ImportPhase) -> Void
    private let lock = OSAllocatedUnfairLock(initialState: (done: 0.0, last: Date.distantPast))

    init(total: Int, report: @escaping @Sendable (ImportPhase) -> Void) {
        self.total = total
        self.report = report
    }

    /// `weight` is the share of a file's work just finished (check + land add up to 1).
    func tick(_ name: String, weight: Double) {
        let (send, done) = lock.withLock { st -> (Bool, Int) in
            st.done += weight
            let now = Date()
            guard now.timeIntervalSince(st.last) > 0.1 else { return (false, 0) }
            st.last = now
            return (true, Int(st.done))
        }
        if send { report(.copying(done: min(done, total), total: total, name: name)) }
    }
}
