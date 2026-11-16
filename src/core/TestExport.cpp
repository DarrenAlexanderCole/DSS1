#include "core/TestExport.h"

#include <cmath>
#include <cstdio>

namespace dss1
{

namespace
{
    constexpr double testBpm = 120.0;
    constexpr double endMarginSeconds = 1.0;
}

std::string formatTime (double seconds)
{
    const auto totalMs = static_cast<long long> (std::llround (seconds * 1000.0));
    char text[32];
    std::snprintf (text, sizeof text, "%lld:%02lld.%03lld", totalMs / 60000, (totalMs / 1000) % 60, totalMs % 1000);
    return text;
}

TrackRecord makeTestRecord (const std::string& path, double sampleRate, Frame lengthFrames)
{
    TrackRecord r;
    r.identity.path = path;
    r.sampleRate = sampleRate;
    r.lengthFrames = lengthFrames;
    if (sampleRate <= 0)
        return r;

    const auto lengthSeconds = static_cast<double> (lengthFrames) / sampleRate;
    const auto fits = [&] (double seconds) { return seconds <= lengthSeconds - endMarginSeconds; };
    const auto frame = [&] (double seconds) { return static_cast<Frame> (std::llround (seconds * sampleRate)); };

    r.grid = BeatGrid::constant (sampleRate, testBpm, 0);

    for (std::size_t i = 0; i < TrackRecord::hotCueSlots; ++i)
    {
        const auto at = 8.0 * static_cast<double> (i + 1);
        if (fits (at))
            r.hotCues[i] = HotCue { frame (at), std::nullopt,
                                    std::string (1, TrackRecord::hotCueLetter (i)) + " " + formatTime (at), std::nullopt };
    }

    if (fits (4.0))
        r.addMemoryCue ({ frame (4.0), "Memory " + formatTime (4.0), std::nullopt });

    if (fits (40.0))
        r.savedLoops[0] = SavedLoop { frame (32.0), frame (40.0), "Loop " + formatTime (32.0) + " - " + formatTime (40.0), std::nullopt, false };
    if (fits (50.0))
        r.savedLoops[1] = SavedLoop { frame (48.0), frame (50.0), "Loop " + formatTime (48.0) + " - " + formatTime (50.0), std::nullopt, false };

    return r;
}

TrackRecord makeTimingRecord (const std::string& path, double sampleRate, Frame lengthFrames, const std::vector<Frame>& hits)
{
    TrackRecord r;
    r.identity.path = path;
    r.sampleRate = sampleRate;
    r.lengthFrames = lengthFrames;
    if (sampleRate <= 0)
        return r;
    for (std::size_t i = 0; i < hits.size() && i < TrackRecord::hotCueSlots; ++i)
        r.hotCues[i] = HotCue { hits[i], std::nullopt,
                                std::string (1, TrackRecord::hotCueLetter (i)) + " " + formatTime (static_cast<double> (hits[i]) / sampleRate),
                                std::nullopt };
    return r;
}

std::vector<TestPosition> testPositions (const TrackRecord& r)
{
    std::vector<TestPosition> out;
    if (r.sampleRate <= 0)
        return out;
    const auto seconds = [&] (Frame f) { return static_cast<double> (f) / r.sampleRate; };

    for (std::size_t i = 0; i < r.hotCues.size(); ++i)
        if (r.hotCues[i])
            out.push_back ({ std::string ("Hot cue ") + TrackRecord::hotCueLetter (i), seconds (r.hotCues[i]->frame), 0 });
    for (const auto& c : r.memoryCues)
        out.push_back ({ "Memory cue", seconds (c.frame), 0 });
    for (std::size_t i = 0; i < r.savedLoops.size(); ++i)
        if (r.savedLoops[i])
            out.push_back ({ "Saved loop " + std::to_string (i + 1), seconds (r.savedLoops[i]->start), seconds (r.savedLoops[i]->end) });
    return out;
}

} // namespace dss1
