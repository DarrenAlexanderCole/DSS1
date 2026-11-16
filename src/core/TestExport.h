#pragma once

#include "core/TrackRecord.h"

#include <string>
#include <vector>

namespace dss1
{

// A reference set of cues, loops and grid with positions that are easy to check by eye in Serato and
// Rekordbox. Used by the app's "test export" to prove that positions land exactly where DSS1 put them:
//
//   Grid          120 BPM from 0:00.000, so every bar is exactly 2 seconds and lines fall on whole seconds
//   Hot cues A–H  0:08, 0:16, 0:24 … 1:04 (every 4 bars), each on a bar line
//   Memory cue    0:04
//   Saved loop 1  0:32 – 0:40 (4 bars)
//   Saved loop 2  0:48 – 0:50 (1 bar)
//
// Anything that would fall within a second of the end of the track is left out.

struct TestPosition
{
    std::string what;     ///< "Hot cue A", "Saved loop 1" …
    double seconds = 0;
    double endSeconds = 0;  ///< loops only
};

TrackRecord makeTestRecord (const std::string& path, double sampleRate, Frame lengthFrames);

/// The timing test: hot cues A–H on the given frames (hits found in the music), named with their times.
/// No grid, memory cues or loops.
TrackRecord makeTimingRecord (const std::string& path, double sampleRate, Frame lengthFrames, const std::vector<Frame>& hits);

/// The positions makeTestRecord used, for showing the user what to check.
std::vector<TestPosition> testPositions (const TrackRecord& record);

/// "1:04.000"
std::string formatTime (double seconds);

} // namespace dss1
