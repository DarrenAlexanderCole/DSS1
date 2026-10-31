import Foundation

enum Phase: Equatable {
    case idle
    case extracting(name: String, done: Int, total: Int)
    case copying(done: Int, total: Int, name: String)
    case done(Int)

    var isBusy: Bool {
        switch self {
        case .extracting, .copying: return true
        default: return false
        }
    }

    var progress: Double {
        switch self {
        case .idle: return 0
        case let .extracting(_, done, total): return total == 0 ? 0 : Double(done) / Double(total)
        case let .copying(done, total, _): return total == 0 ? 0 : Double(done) / Double(total)
        case .done: return 1
        }
    }
}

enum Screen {
    case home, player, chop, radio, perform
}

/// Title and artist pulled from a record-pool filename like
/// "07 Anderson .Paak & Cordae - Crown (Dirty).wav".
struct TrackInfo: Equatable {
    let title: String
    let artist: String
    let format: String

    init(url: URL) {
        format = url.pathExtension.uppercased()
        var name = url.deletingPathExtension().lastPathComponent
        // Leading track numbers ("07 ", "12 ", "3. ") but not names like "50 Cent".
        if let r = name.range(of: #"^([01]\d\s+|\d{1,3}\.\s*)"#, options: .regularExpression) {
            name.removeSubrange(r)
        }
        if let dash = name.range(of: " - ") {
            artist = String(name[..<dash.lowerBound])
            title = String(name[dash.upperBound...])
        } else {
            artist = ""
            title = name
        }
    }
}
