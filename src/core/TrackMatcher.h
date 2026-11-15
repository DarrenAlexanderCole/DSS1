#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dss1
{

// Finds which library track a track from somewhere else (a Rekordbox XML, a Serato crate) refers to.
// Paths rarely match exactly: the same file name can be spelled with composed or decomposed accents
// (macOS file names vs. other apps), differ in case, live on another drive, or have been converted from
// MP3 to WAV. The matcher tries strict rules first and only falls back to looser ones when nothing
// stricter matched. It never guesses between two candidates: that's reported as ambiguous.

struct MatchCandidate
{
    std::int64_t id = 0;
    std::string path;
    std::optional<double> lengthSeconds;
};

struct MatchQuery
{
    std::string path;
    std::optional<double> lengthSeconds;
};

enum class MatchKind
{
    none,           ///< nothing plausible
    ambiguous,      ///< several equally good candidates; `ids` lists them
    exactPath,      ///< the same path, byte for byte
    samePath,       ///< the same path once accents and letter case are ignored
    sameFileName,   ///< the same file name in another folder, and the same length
    sameName        ///< the same name with another extension (e.g. converted MP3 → WAV), and the same length
};

struct MatchResult
{
    MatchKind kind = MatchKind::none;
    std::vector<std::int64_t> ids;   ///< one id for a match, several when ambiguous

    bool isMatch() const noexcept { return ids.size() == 1 && kind != MatchKind::ambiguous && kind != MatchKind::none; }
};

/// Lengths within this many seconds count as the same (Rekordbox rounds to whole seconds; decoders differ).
inline constexpr double lengthToleranceSeconds = 2.0;

MatchResult matchTrack (const MatchQuery& query, const std::vector<MatchCandidate>& candidates);

/// The comparison key used for the looser rules: lower-case, accents removed (composed or decomposed),
/// backslashes as slashes. "Café/É.MP3" and "cafe\\e.mp3" give the same key.
std::string looseKey (const std::string& utf8);

} // namespace dss1
