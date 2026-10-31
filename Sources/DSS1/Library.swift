import AVFoundation
import Foundation
import SwiftUI

struct LibraryTrack: Identifiable, Equatable {
    let url: URL
    let info: TrackInfo
    let added: Date
    var id: String { url.path }
}

/// How the crate is ordered. Newest drops first is the default.
enum CrateSort: String, CaseIterable, Identifiable {
    case newest, name, artist, album, length, bpm, key
    var id: String { rawValue }

    var label: String {
        switch self {
        case .newest: return "Newest"
        case .name: return "Name"
        case .artist: return "Artist"
        case .album: return "Album"
        case .length: return "Length"
        case .bpm: return "BPM"
        case .key: return "Key"
        }
    }
}

/// Everything playable in the destination folder. Kept in sync with a folder watcher,
/// with each track's BPM and length worked out once and remembered.
@MainActor
final class Library: ObservableObject {
    @Published private(set) var tracks: [LibraryTrack] = [] {
        didSet { reorder() }
    }
    @Published private(set) var analysis: [String: TrackAnalyzer.Result] {
        didSet { if sort == .bpm || sort == .key || sort == .length { reorder() } }
    }
    /// The crate in the chosen order; this is also the play queue. Recomputed only when something it
    /// depends on changes, not on every redraw.
    @Published private(set) var ordered: [LibraryTrack] = []
    /// Background analysis (BPM, key, loudness): how far through the current batch it is.
    @Published private(set) var analysisProgress: (done: Int, total: Int)?
    @Published private(set) var played: Set<String>
    /// Album tags read from the files ("" when a file has none).
    @Published private(set) var albums: [String: String] {
        didSet { if sort == .album { reorder() } }
    }
    @Published var sort: CrateSort {
        didSet {
            defaults.set(sort.rawValue, forKey: "crateSort")
            reorder()
        }
    }
    @Published var ascending: Bool {
        didSet {
            defaults.set(ascending, forKey: "crateAscending")
            reorder()
        }
    }

    private var folder: URL?
    private var watcher: DispatchSourceFileSystemObject?
    private var rescanTask: Task<Void, Never>?
    private var analyzing = false
    private let defaults = UserDefaults.standard

    init() {
        played = Set(defaults.stringArray(forKey: "played") ?? [])
        albums = defaults.dictionary(forKey: "albums") as? [String: String] ?? [:]
        sort = CrateSort(rawValue: defaults.string(forKey: "crateSort") ?? "") ?? .newest
        ascending = defaults.object(forKey: "crateAscending") as? Bool ?? true
        if let data = defaults.data(forKey: "analysis"),
           let saved = try? JSONDecoder().decode([String: TrackAnalyzer.Result].self, from: data) {
            analysis = saved
        } else {
            analysis = [:]
        }
    }

    func setFolder(_ url: URL?) {
        folder = url
        watch()
        Task { await rescan() }
    }

    func result(for url: URL) -> TrackAnalyzer.Result? { analysis[url.path] }
    func bpm(for url: URL) -> Double? { analysis[url.path]?.bpm }
    func duration(for url: URL) -> Double? { analysis[url.path]?.duration }
    func isPlayed(_ url: URL) -> Bool { played.contains(url.path) }
    func album(for url: URL) -> String? { albums[url.path].flatMap { $0.isEmpty ? nil : $0 } }

    private func reorder() {
        let next = sorted()
        if next.map(\.id) != ordered.map(\.id) { ordered = next }
    }

    /// Tracks missing the sorted-on value (no album, not analysed yet) always sit at the bottom.
    private func sorted() -> [LibraryTrack] {
        if sort == .newest { return ascending ? tracks : tracks.reversed() }
        func text(_ a: String, _ b: String) -> ComparisonResult { a.localizedStandardCompare(b) }
        func value(_ t: LibraryTrack) -> Any? {
            switch sort {
            case .newest: return nil
            case .name: return t.info.title
            case .artist: return t.info.artist.isEmpty ? nil : t.info.artist
            case .album: return album(for: t.url)
            case .length: return duration(for: t.url)
            case .bpm: return bpm(for: t.url)
            case .key: return result(for: t.url)?.camelot.flatMap(Library.camelotRank)
            }
        }
        // Ties fall back to the filename, which keeps an album in track-number order.
        func tie(_ a: LibraryTrack, _ b: LibraryTrack) -> Bool {
            text(a.url.lastPathComponent, b.url.lastPathComponent) == .orderedAscending
        }
        let keyed = tracks.map { ($0, value($0)) }
        return keyed.sorted { a, b in
            switch (a.1, b.1) {
            case (nil, nil): return tie(a.0, b.0)
            case (nil, _): return false
            case (_, nil): return true
            case let (x?, y?):
                var order: ComparisonResult
                if let x = x as? String, let y = y as? String { order = text(x, y) }
                else if let x = x as? Double, let y = y as? Double { order = x == y ? .orderedSame : (x < y ? .orderedAscending : .orderedDescending) }
                else if let x = x as? Int, let y = y as? Int { order = x == y ? .orderedSame : (x < y ? .orderedAscending : .orderedDescending) }
                else { order = .orderedSame }
                if order == .orderedSame { return tie(a.0, b.0) }
                return ascending ? order == .orderedAscending : order == .orderedDescending
            }
        }.map(\.0)
    }

    /// "8A" → 15, "8B" → 16: round the Camelot wheel, minor before major.
    nonisolated static func camelotRank(_ code: String) -> Int? {
        guard let letter = code.last, let n = Int(code.dropLast()) else { return nil }
        return n * 2 + (letter == "B" ? 1 : 0)
    }

    func markPlayed(_ url: URL) {
        guard !played.contains(url.path) else { return }
        played.insert(url.path)
        defaults.set(Array(played), forKey: "played")
    }

    func rescan() async {
        guard let folder else {
            tracks = []
            return
        }
        let found = await Task.detached(priority: .utility) {
            Library.scan(folder, extensions: AppModel.audioExtensions)
        }.value
        withAnimation(Motion.bouncy) { tracks = found }
        analyzeMissing()
        readMissingAlbums()
    }

    /// Newest drops first; inside a drop, in track order.
    nonisolated private static func scan(_ folder: URL, extensions: Set<String>) -> [LibraryTrack] {
        let keys: [URLResourceKey] = [.isRegularFileKey, .addedToDirectoryDateKey, .creationDateKey]
        guard let walker = FileManager.default.enumerator(at: folder, includingPropertiesForKeys: keys,
                                                          options: [.skipsHiddenFiles, .skipsPackageDescendants]) else { return [] }
        var found: [LibraryTrack] = []
        for case let url as URL in walker {
            // Serato exports are tagged copies of crate songs: keep them out of the crate.
            if walker.level == 1, url.lastPathComponent == "Serato" {
                walker.skipDescendants()
                continue
            }
            guard extensions.contains(url.pathExtension.lowercased()) else { continue }
            let values = try? url.resourceValues(forKeys: Set(keys))
            guard values?.isRegularFile == true else { continue }
            let added = values?.addedToDirectoryDate ?? values?.creationDate ?? .distantPast
            found.append(LibraryTrack(url: url, info: TrackInfo(url: url), added: added))
        }
        func bucket(_ d: Date) -> Double { (d.timeIntervalSince1970 / 600).rounded(.down) }
        return found.sorted {
            let a = bucket($0.added), b = bucket($1.added)
            if a != b { return a > b }
            return $0.url.lastPathComponent.localizedStandardCompare($1.url.lastPathComponent) == .orderedAscending
        }
    }

    /// Tracks that wouldn't decode for analysis; flagged in the crate and not retried this session.
    @Published private(set) var unreadable: Set<String> = []
    func isUnreadable(_ url: URL) -> Bool { unreadable.contains(url.path) }

    private func needsAnalysis(_ url: URL) -> Bool {
        if unreadable.contains(url.path) { return false }
        guard let r = analysis[url.path] else { return true }
        return (r.version ?? 0) < TrackAnalyzer.version || r.loudness == nil
    }

    /// Heavy file reading runs here, on its own threads, so it never ties up Swift's shared pool
    /// (which the app's timers and UI tasks need).
    nonisolated private static let workers: OperationQueue = {
        let q = OperationQueue()
        q.name = "DSS1 analysis"
        q.qualityOfService = .utility
        q.maxConcurrentOperationCount = max(1, ProcessInfo.processInfo.activeProcessorCount - 2)
        return q
    }()

    /// Measures one track: the full analysis, or just loudness when BPM/key are already known.
    /// A track that reads but has no measurable loudness (silence) is stored as −70 LUFS so it isn't retried.
    nonisolated private static func measure(_ url: URL, previous: TrackAnalyzer.Result?) async -> TrackAnalyzer.Result? {
        await withCheckedContinuation { done in
            workers.addOperation {
                if var r = previous, (r.version ?? 0) >= TrackAnalyzer.version {
                    guard (try? AVAudioFile(forReading: url)) != nil else { return done.resume(returning: nil) }
                    let l = TrackAnalyzer.loudness(url)
                    r.loudness = l?.lufs ?? -70
                    r.peak = l?.peak ?? -70
                    return done.resume(returning: r)
                }
                guard var r = TrackAnalyzer.analyze(url) else { return done.resume(returning: nil) }
                if r.loudness == nil { r.loudness = -70; r.peak = -70 }
                done.resume(returning: r)
            }
        }
    }

    /// Analyses everything new on all but two cores, publishing results in batches
    /// (so the crate isn't redrawn once per track) and saving once per batch.
    private func analyzeMissing() {
        guard !analyzing else { return }
        let pending = tracks.map(\.url).filter { needsAnalysis($0) }
        guard !pending.isEmpty else { return }
        analyzing = true
        analysisProgress = (0, pending.count)
        let known = analysis
        Task {
            var buffered: [String: TrackAnalyzer.Result] = [:]
            var done = 0
            var lastFlush = Date()
            await withTaskGroup(of: (String, TrackAnalyzer.Result?).self) { group in
                for url in pending {
                    let previous = known[url.path]
                    group.addTask { (url.path, await Library.measure(url, previous: previous)) }
                }
                for await (path, result) in group {
                    if let result { buffered[path] = result } else { unreadable.insert(path) }
                    done += 1
                    if Date().timeIntervalSince(lastFlush) > 0.5 || done == pending.count {
                        analysis.merge(buffered) { $1 }
                        buffered = [:]
                        analysisProgress = (done, pending.count)
                        lastFlush = Date()
                    }
                }
            }
            if !buffered.isEmpty { analysis.merge(buffered) { $1 } }
            if let data = try? JSONEncoder().encode(analysis) { defaults.set(data, forKey: "analysis") }
            analyzing = false
            analysisProgress = nil
            // Files may have landed while we were busy.
            if tracks.contains(where: { needsAnalysis($0.url) }) { analyzeMissing() }
        }
    }

    private var readingAlbums = false

    private func readMissingAlbums() {
        guard !readingAlbums else { return }
        let pending = tracks.map(\.url).filter { albums[$0.path] == nil }
        guard !pending.isEmpty else { return }
        readingAlbums = true
        Task {
            var found: [String: String] = [:]
            await withTaskGroup(of: (String, String).self) { group in
                for url in pending {
                    group.addTask(priority: .utility) { (url.path, await Library.readAlbum(url) ?? "") }
                }
                for await (path, album) in group { found[path] = album }
            }
            albums.merge(found) { $1 }
            defaults.set(albums, forKey: "albums")
            readingAlbums = false
            if tracks.contains(where: { albums[$0.url.path] == nil }) { readMissingAlbums() }
        }
    }

    /// The album tag (ID3, iTunes, Vorbis) if the file has one.
    nonisolated private static func readAlbum(_ url: URL) async -> String? {
        let asset = AVURLAsset(url: url)
        var items = (try? await asset.load(.commonMetadata)) ?? []
        if items.isEmpty {
            for format in (try? await asset.load(.availableMetadataFormats)) ?? [] {
                items += (try? await asset.loadMetadata(for: format)) ?? []
            }
        }
        let album = AVMetadataItem.metadataItems(from: items, filteredByIdentifier: .commonIdentifierAlbumName).first
            ?? items.first { ($0.commonKey == .commonKeyAlbumName) || (($0.key as? String)?.uppercased() == "ALBUM") }
        guard let album, let name = try? await album.load(.stringValue) else { return nil }
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? nil : trimmed
    }

    private func watch() {
        watcher?.cancel()
        watcher = nil
        guard let folder else { return }
        let fd = open(folder.path, O_EVTONLY)
        guard fd >= 0 else { return }
        let source = DispatchSource.makeFileSystemObjectSource(fileDescriptor: fd, eventMask: [.write, .rename, .delete], queue: .main)
        source.setEventHandler { [weak self] in
            Task { @MainActor in self?.scheduleRescan() }
        }
        source.setCancelHandler { close(fd) }
        source.resume()
        watcher = source
    }

    private var watchingPaused = false

    /// During an import the folder changes hundreds of times; hold off rescanning until it's done.
    func pauseWatching() {
        watchingPaused = true
        rescanTask?.cancel()
    }

    func resumeWatching() async {
        watchingPaused = false
        await rescan()
    }

    private func scheduleRescan() {
        guard !watchingPaused else { return }
        rescanTask?.cancel()
        rescanTask = Task {
            try? await Task.sleep(for: .milliseconds(600))
            guard !Task.isCancelled else { return }
            await rescan()
        }
    }
}
