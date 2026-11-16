#include "core/RekordboxMapping.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace dss1::rekordbox
{

namespace
{
    /// Rekordbox positions are milliseconds; a marker this close to a beat the grid already has is on it.
    constexpr double sameBeatToleranceSeconds = 0.002;
    constexpr double sameBpmTolerance = 0.005;

    Frame toFrame (double seconds, double sampleRate)
    {
        return static_cast<Frame> (std::llround (seconds * sampleRate));
    }

    double toSeconds (Frame frame, double sampleRate)
    {
        return static_cast<double> (frame) / sampleRate;
    }

    /// "4/4" → 4, "3/4" → 3, "7/8" → 7. Anything unreadable counts as 4.
    int beatsPerBarFromMetro (const std::string& metro)
    {
        try
        {
            const auto n = std::stoi (metro);
            return (n >= 1 && n <= 32) ? n : 4;
        }
        catch (...)
        {
            return 4;
        }
    }

    std::optional<CueColour> toCueColour (const std::optional<Rgb>& rgb)
    {
        if (! rgb)
            return std::nullopt;
        const auto clamp = [] (int v) { return static_cast<std::uint8_t> (std::clamp (v, 0, 255)); };
        return CueColour { clamp (rgb->red), clamp (rgb->green), clamp (rgb->blue) };
    }

    std::optional<Rgb> toRgb (const std::optional<CueColour>& c)
    {
        if (! c)
            return std::nullopt;
        return Rgb { c->red, c->green, c->blue };
    }

    std::string plural (std::size_t n, const char* one, const char* many)
    {
        return std::to_string (n) + " " + (n == 1 ? one : many);
    }
} // namespace

std::optional<BeatGrid> gridFromTempos (const std::vector<Tempo>& tempos, double sampleRate)
{
    if (sampleRate <= 0)
        return std::nullopt;

    std::vector<Tempo> ordered;
    for (const auto& t : tempos)
        if (std::isfinite (t.inizio) && t.bpm >= BeatGrid::minBpm && t.bpm <= BeatGrid::maxBpm)
            ordered.push_back (t);
    std::stable_sort (ordered.begin(), ordered.end(), [] (const Tempo& a, const Tempo& b) { return a.inizio < b.inizio; });

    std::vector<TempoAnchor> kept;
    for (const auto& t : ordered)
    {
        TempoAnchor anchor;
        anchor.frame = toFrame (t.inizio, sampleRate);
        anchor.bpm = t.bpm;
        anchor.beatsPerBar = beatsPerBarFromMetro (t.metro);
        anchor.beatInBar = std::clamp (t.battito, 1, anchor.beatsPerBar);

        if (! kept.empty())
        {
            if (anchor.frame <= kept.back().frame)
                continue;  // two markers on the same frame: the first one wins

            const BeatGrid sofar (sampleRate, kept);
            const auto& last = kept.back();
            const auto beat = std::llround (sofar.beatAt (static_cast<double> (anchor.frame)));
            const auto offBy = std::abs (sofar.frameAt (static_cast<double> (beat)) - static_cast<double> (anchor.frame));

            const bool redundant = std::abs (anchor.bpm - last.bpm) <= sameBpmTolerance
                                && anchor.beatsPerBar == last.beatsPerBar
                                && offBy <= sameBeatToleranceSeconds * sampleRate
                                && sofar.beatInBar (beat) == anchor.beatInBar;
            if (redundant)
                continue;
        }
        kept.push_back (anchor);
    }

    if (kept.empty())
        return std::nullopt;
    return BeatGrid (sampleRate, std::move (kept));
}

std::vector<Tempo> temposFromGrid (const BeatGrid& grid)
{
    std::vector<Tempo> tempos;
    const auto rate = grid.sampleRate();
    const auto& anchors = grid.anchors();

    for (std::size_t i = 0; i < anchors.size(); ++i)
    {
        auto frame = static_cast<double> (anchors[i].frame);
        auto beatInBar = anchors[i].beatInBar;

        if (i == 0)
        {
            // The grid also runs backwards from its first anchor; Rekordbox's doesn't, so start it on the
            // first beat that falls inside the track.
            const auto firstBeat = static_cast<std::int64_t> (std::ceil (grid.beatAt (0.0) - 1.0e-9));
            const auto firstFrame = grid.frameAt (static_cast<double> (firstBeat));
            const bool beforeNextAnchor = anchors.size() == 1 || firstFrame < static_cast<double> (anchors[1].frame);
            if (firstFrame != frame && beforeNextAnchor)
            {
                frame = firstFrame;
                beatInBar = grid.beatInBar (firstBeat);
            }
        }

        Tempo t;
        t.inizio = frame / rate;
        t.bpm = anchors[i].bpm;
        t.metro = std::to_string (anchors[i].beatsPerBar) + "/4";
        t.battito = beatInBar;
        tempos.push_back (t);
    }
    return tempos;
}

ImportedTrack importTrack (const Track& track, double sampleRate, Frame lengthFrames, Frame exportOffsetFrames)
{
    ImportedTrack out;
    auto& r = out.record;
    r.exportOffsetFrames = exportOffsetFrames;
    // Rekordbox's positions count the MP3 encoder delay; DSS1's don't.
    const auto frameOf = [&] (double seconds) { return toFrame (seconds, sampleRate) - exportOffsetFrames; };
    r.identity.path = pathFromLocation (track.location);
    r.sampleRate = sampleRate;
    r.lengthFrames = lengthFrames;
    auto tempos = track.tempos;
    for (auto& t : tempos)
        t.inizio -= static_cast<double> (exportOffsetFrames) / (sampleRate > 0 ? sampleRate : 1.0);
    r.grid = gridFromTempos (tempos, sampleRate);

    std::vector<PositionMark> memoryLoops;
    std::size_t duplicateHotCues = 0, otherMarks = 0, badHotCueNumbers = 0;

    for (const auto& m : track.marks)
    {
        const bool isLoop = m.type == MarkType::loop && m.end && *m.end > m.start;
        if (m.type != MarkType::cue && ! isLoop)
        {
            ++otherMarks;  // fade-in, fade-out and load markers have no DSS1 equivalent
            continue;
        }

        if (m.num >= 0)
        {
            if (m.num >= static_cast<int> (TrackRecord::hotCueSlots))
            {
                ++badHotCueNumbers;
                continue;
            }
            auto& slot = r.hotCues[static_cast<std::size_t> (m.num)];
            if (slot)
            {
                ++duplicateHotCues;
                continue;
            }
            HotCue cue;
            cue.frame = frameOf (m.start);
            if (isLoop)
                cue.loopEnd = frameOf (*m.end);
            cue.name = m.name;
            cue.colour = toCueColour (m.colour);
            slot = cue;
        }
        else if (isLoop)
        {
            memoryLoops.push_back (m);
        }
        else
        {
            r.addMemoryCue ({ frameOf (m.start), m.name, toCueColour (m.colour) });
        }
    }

    // Memory loops fill the saved-loop slots in track order.
    std::stable_sort (memoryLoops.begin(), memoryLoops.end(),
                      [] (const PositionMark& a, const PositionMark& b) { return a.start < b.start; });
    std::size_t slot = 0;
    for (const auto& m : memoryLoops)
    {
        if (slot >= TrackRecord::savedLoopSlots)
            break;
        SavedLoop loop;
        loop.start = frameOf (m.start);
        loop.end = frameOf (*m.end);
        loop.name = m.name;
        loop.colour = toCueColour (m.colour);
        r.savedLoops[slot++] = loop;
    }

    if (memoryLoops.size() > TrackRecord::savedLoopSlots)
        out.notes.push_back (plural (memoryLoops.size() - TrackRecord::savedLoopSlots, "memory loop", "memory loops")
                             + " didn't fit in the " + std::to_string (TrackRecord::savedLoopSlots) + " saved-loop slots");
    if (duplicateHotCues > 0)
        out.notes.push_back (plural (duplicateHotCues, "hot cue was", "hot cues were") + " skipped because another cue already uses that letter");
    if (badHotCueNumbers > 0)
        out.notes.push_back (plural (badHotCueNumbers, "hot cue was", "hot cues were") + " skipped because it isn't one of A–H");
    if (otherMarks > 0)
        out.notes.push_back (plural (otherMarks, "fade or load marker was", "fade or load markers were") + " skipped");

    for (const auto& problem : r.problems())
        out.notes.push_back (problem);
    return out;
}

Track exportTrack (const TrackRecord& r, const TrackDetails& details)
{
    Track t;
    t.trackId = details.trackId;
    t.name = details.name;
    t.artist = details.artist;
    t.album = details.album;
    t.genre = details.genre;
    t.kind = details.kind;
    t.tonality = details.tonality;
    t.location = locationFromPath (r.identity.path);

    const auto rate = r.sampleRate;
    if (rate > 0)
    {
        t.sampleRate = rate;
        if (r.lengthFrames > 0)
            t.totalTime = std::floor (toSeconds (r.lengthFrames, rate));
    }

    // Rekordbox counts an MP3's encoder delay; DSS1 doesn't.
    const auto secondsOf = [&] (Frame f) { return toSeconds (f + r.exportOffsetFrames, rate); };

    if (r.grid && ! r.grid->isEmpty())
    {
        t.tempos = temposFromGrid (*r.grid);
        if (rate > 0)
            for (auto& tempo : t.tempos)
                tempo.inizio += static_cast<double> (r.exportOffsetFrames) / rate;
        if (r.grid->isConstantTempo())
            t.averageBpm = r.grid->anchors().front().bpm;
    }
    if (! t.averageBpm)
        t.averageBpm = details.averageBpm;

    if (rate <= 0)
        return t;  // positions can't be written without a sample rate

    for (std::size_t i = 0; i < r.hotCues.size(); ++i)
    {
        if (! r.hotCues[i])
            continue;
        const auto& c = *r.hotCues[i];
        PositionMark m;
        m.name = c.name;
        m.type = c.loopEnd ? MarkType::loop : MarkType::cue;
        m.start = secondsOf (c.frame);
        if (c.loopEnd)
            m.end = secondsOf (*c.loopEnd);
        m.num = static_cast<int> (i);
        m.colour = toRgb (c.colour);
        t.marks.push_back (m);
    }

    for (const auto& c : r.memoryCues)
        t.marks.push_back ({ c.name, MarkType::cue, secondsOf (c.frame), std::nullopt, -1, toRgb (c.colour) });

    for (const auto& loop : r.savedLoops)
        if (loop)
            t.marks.push_back ({ loop->name, MarkType::loop, secondsOf (loop->start),
                                 secondsOf (loop->end), -1, toRgb (loop->colour) });
    return t;
}

} // namespace dss1::rekordbox
