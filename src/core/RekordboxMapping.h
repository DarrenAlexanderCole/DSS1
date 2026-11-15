#pragma once

#include "core/RekordboxXml.h"
#include "core/TrackRecord.h"

#include <optional>
#include <string>
#include <vector>

namespace dss1::rekordbox
{

// Converting between Rekordbox's XML values (seconds, marks numbered 0–7 / -1) and DSS1's track data
// (frames, hot cue slots A–H, memory cues, saved loops).
//
// Precision: Rekordbox writes positions to the millisecond, so a DSS1 position that goes out to Rekordbox
// and back lands within half a millisecond (about 22 frames at 44.1 kHz) of where it started.

/// What DSS1 writes into a TRACK besides the cues and grid. Anything left empty is left out.
struct TrackDetails
{
    std::string trackId;
    std::string name;
    std::string artist;
    std::string album;
    std::string genre;
    std::string kind;       ///< "WAV File", "MP3 File" …
    std::string tonality;   ///< key, e.g. "8A" or "Am"
    std::optional<double> averageBpm;  ///< used when the grid isn't a single tempo
};

struct ImportedTrack
{
    TrackRecord record;
    /// Things that didn't carry over, in plain words ("2 memory loops didn't fit in the 8 saved-loop slots").
    std::vector<std::string> notes;
};

/// Builds DSS1 track data from a Rekordbox TRACK. `sampleRate` and `lengthFrames` come from the audio file
/// itself (the XML's values are only hints), so positions land on the right frames.
ImportedTrack importTrack (const Track& track, double sampleRate, Frame lengthFrames);

/// Builds a Rekordbox TRACK from DSS1 track data.
Track exportTrack (const TrackRecord& record, const TrackDetails& details);

/// Rekordbox TEMPO markers → a DSS1 grid. Markers that only repeat what the grid already says (same tempo,
/// on a beat it already has) are dropped, so a "one marker per bar" grid becomes a single anchor.
/// Returns nullopt when there are no usable markers.
std::optional<BeatGrid> gridFromTempos (const std::vector<Tempo>& tempos, double sampleRate);

/// A DSS1 grid → Rekordbox TEMPO markers, one per anchor. The first marker is moved back to the first beat
/// at or after the start of the track, because Rekordbox doesn't extend a grid backwards.
std::vector<Tempo> temposFromGrid (const BeatGrid& grid);

} // namespace dss1::rekordbox
