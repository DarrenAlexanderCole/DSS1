#pragma once

#include "core/TrackRecord.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dss1::serato
{

// Serato DJ keeps hot cues, saved loops and the beat grid inside the audio file, in two ID3 GEOB frames:
// "Serato Markers2" (cues, loops, track colour) and "Serato BeatGrid". This file builds and reads those
// payloads, following the community-documented layout (github.com/Holzhaus/serato-tags), and converts
// them to and from DSS1 track data. Putting the frames into a file is Id3.h's job.

using Bytes = std::vector<std::uint8_t>;

struct Cue
{
    int index = 0;                 ///< 0–7 = hot cue 1–8
    std::uint32_t positionMs = 0;
    CueColour colour;
    std::string name;

    bool operator== (const Cue&) const = default;
};

struct Loop
{
    int index = 0;                 ///< 0–7 = saved loop 1–8
    std::uint32_t startMs = 0;
    std::uint32_t endMs = 0;
    CueColour colour { 0x27, 0xAA, 0xE1 };  ///< Serato's loop blue
    bool locked = false;
    std::string name;

    bool operator== (const Loop&) const = default;
};

struct Markers
{
    CueColour trackColour { 0xFF, 0xFF, 0xFF };
    std::vector<Cue> cues;
    std::vector<Loop> loops;
    bool bpmLocked = false;

    bool operator== (const Markers&) const = default;
};

/// Serato's grid: markers on beats. Each marker but the last says how many beats follow until the next
/// marker; the last one carries the tempo from there on.
struct Grid
{
    struct Marker
    {
        float positionSeconds = 0;
        std::uint32_t beatsToNext = 0;
        bool operator== (const Marker&) const = default;
    };

    std::vector<Marker> markers;   ///< all but the last
    float lastPositionSeconds = 0;
    float lastBpm = 120;

    bool operator== (const Grid&) const = default;
};

/// The ID3 GEOB descriptions DSS1 writes, and the old one it removes. For MP3s Serato also keeps cues in an
/// older binary "Serato Markers_" frame; a stale copy of it (from a file Serato had seen before) could disagree
/// with the new Markers2 data, so DSS1 drops it whenever it writes Markers2.
inline constexpr const char* markers2Description = "Serato Markers2";
inline constexpr const char* beatGridDescription = "Serato BeatGrid";
inline constexpr const char* legacyMarkersDescription = "Serato Markers_";

/// Serato's default hot-cue colours, by cue number, used when a DSS1 cue has no colour of its own.
CueColour defaultCueColour (int index);

// --- Payloads ---------------------------------------------------------------------------------------------

/// The decoded "Serato Markers2" data: version, entries, terminator.
Bytes encodeMarkers (const Markers& markers);
/// Reads decoded "Serato Markers2" data. Unknown entries are skipped. Throws std::runtime_error if malformed.
Markers decodeMarkers (const Bytes& payload);

/// What goes inside the GEOB frame for Markers2: `01 01`, then the payload in Serato's base64 (a line feed
/// every 72 characters, '=' written as 'A'), padded with zeros to at least 470 bytes.
Bytes wrapMarkersForGeob (const Bytes& payload);
/// The reverse of wrapMarkersForGeob.
Bytes unwrapMarkersFromGeob (const Bytes& geobContent);

Bytes encodeGrid (const Grid& grid);
Grid decodeGrid (const Bytes& geobContent);

// --- DSS1 track data --------------------------------------------------------------------------------------

struct Export
{
    Markers markers;
    std::optional<Grid> grid;
    /// What Serato can't hold, in plain words ("2 memory cues aren't exported: Serato has no memory cues").
    std::vector<std::string> notes;
};

struct ExportOptions
{
    /// Many Serato DJs don't use beat grids, so DSS1 only writes one when the user asks for it.
    bool includeBeatGrid = false;
};

/// Hot cues A–H → cues 1–8, saved loops → loops 1–8, and the beat grid → Serato's grid if asked for.
/// Positions are shifted by the record's exportOffsetFrames (an MP3's encoder delay, which Serato counts).
Export exportTrack (const TrackRecord& record, const ExportOptions& options = {});

/// Serato cues, loops and grid → DSS1 track data, using the record's sample rate (the audio file's own).
/// Replaces the record's hot cues and saved loops, and its grid when Serato has one. Set the record's
/// exportOffsetFrames first: Serato's positions count an MP3's encoder delay and DSS1's don't.
void importInto (TrackRecord& record, const Markers& markers, const std::optional<Grid>& grid);

/// Serato grid ↔ DSS1 grid.
std::optional<Grid> gridFromBeatGrid (const BeatGrid& grid);
std::optional<BeatGrid> beatGridFromGrid (const Grid& grid, double sampleRate);

} // namespace dss1::serato
