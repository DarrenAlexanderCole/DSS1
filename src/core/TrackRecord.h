#pragma once

#include "core/BeatGrid.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace dss1
{

struct CueColour
{
    std::uint8_t red = 0, green = 0, blue = 0;
    bool operator== (const CueColour&) const = default;
};

/// One of the eight performance cues, A–H. With `loopEnd` set it is a hot loop.
struct HotCue
{
    Frame frame = 0;
    std::optional<Frame> loopEnd;
    std::string name;
    std::optional<CueColour> colour;
    bool operator== (const HotCue&) const = default;
};

/// A navigation marker; a track can have any number of them.
struct MemoryCue
{
    Frame frame = 0;
    std::string name;
    std::optional<CueColour> colour;
    bool operator== (const MemoryCue&) const = default;
};

/// A saved loop: [start, end) in frames.
struct SavedLoop
{
    Frame start = 0;
    Frame end = 0;
    std::string name;
    std::optional<CueColour> colour;
    bool locked = false;
    bool operator== (const SavedLoop&) const = default;
};

/// How a track is recognised, even after it has been moved or renamed.
struct TrackIdentity
{
    std::string path;         ///< absolute path when last seen
    std::string fingerprint;  ///< hash of the decoded audio; empty until worked out
    bool operator== (const TrackIdentity&) const = default;
};

/// What the crate shows about a track: from its tags or file name, and from DSS1's analysis.
struct TrackInfo
{
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    std::optional<double> bpm;
    std::string key;            ///< e.g. "F minor"
    std::string camelot;        ///< e.g. "4A"
    std::optional<double> loudnessLufs;
    std::string notes;          ///< the DJ's own notes, typed in Catalog (exported to Rekordbox as Comments)
    bool analysed = false;
    bool operator== (const TrackInfo&) const = default;
};

/// Everything DSS1 knows about one track that the user has prepared: its grid, cues and loops.
/// Positions are frames at `sampleRate`, the file's own rate.
struct TrackRecord
{
    static constexpr int schemaVersion = 1;
    static constexpr std::size_t hotCueSlots = 8;
    static constexpr std::size_t savedLoopSlots = 8;

    TrackIdentity identity;
    TrackInfo info;
    double sampleRate = 0;
    Frame lengthFrames = 0;

    /// Frames other DJ apps count before DSS1's frame 0. DSS1 removes an MP3's encoder delay so frame 0 is the
    /// first real sample; Serato and Rekordbox keep it (checked by hand: cues landed ~25 ms early until this
    /// was added back). Exports add this to every position and imports subtract it. 0 for WAV, AIFF and FLAC.
    Frame exportOffsetFrames = 0;

    std::optional<BeatGrid> grid;
    std::array<std::optional<HotCue>, hotCueSlots> hotCues {};
    std::vector<MemoryCue> memoryCues;  ///< kept in frame order
    std::array<std::optional<SavedLoop>, savedLoopSlots> savedLoops {};

    /// 'A' … 'H'.
    static char hotCueLetter (std::size_t slot);

    /// Adds a memory cue in frame order. A cue already at the same frame is replaced.
    void addMemoryCue (MemoryCue cue);

    /// Human-readable problems (cues past the end, loops with end <= start …); empty when valid.
    std::vector<std::string> problems() const;

    bool operator== (const TrackRecord&) const = default;
};

/// JSON form, with a "schema" number. Reading throws std::runtime_error for a newer schema
/// than this build understands, and nlohmann::json::exception for malformed input.
nlohmann::json toJson (const TrackRecord& track);
TrackRecord trackRecordFromJson (const nlohmann::json& json);

} // namespace dss1
