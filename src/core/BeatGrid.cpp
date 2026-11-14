#include "core/BeatGrid.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace dss1
{

namespace
{
    // Beat numbers are worked out in floating point; anything this close to a whole beat counts as on it.
    constexpr double beatEpsilon = 1.0e-9;

    // A safety stop for beatsBetween(): a 10-hour track at 999 BPM is ~600k beats.
    constexpr std::size_t maxBeatsReturned = 2'000'000;
}

BeatGrid::BeatGrid (double sampleRate, std::vector<TempoAnchor> anchors)
    : sampleRate_ (sampleRate), anchors_ (std::move (anchors))
{
    if (! std::isfinite (sampleRate_) || sampleRate_ <= 0)
        throw std::invalid_argument ("BeatGrid: sample rate must be positive");

    for (std::size_t i = 0; i < anchors_.size(); ++i)
    {
        const auto& a = anchors_[i];
        const auto where = "BeatGrid: anchor " + std::to_string (i) + ": ";

        if (! std::isfinite (a.bpm) || a.bpm < minBpm || a.bpm > maxBpm)
            throw std::invalid_argument (where + "BPM out of range");
        if (a.beatsPerBar < 1 || a.beatsPerBar > 32)
            throw std::invalid_argument (where + "beats per bar out of range");
        if (a.beatInBar < 1 || a.beatInBar > a.beatsPerBar)
            throw std::invalid_argument (where + "beat in bar out of range");
        if (i > 0 && a.frame <= anchors_[i - 1].frame)
            throw std::invalid_argument (where + "anchors must be in increasing frame order");
    }

    anchorBeats_.resize (anchors_.size());
    for (std::size_t i = 1; i < anchors_.size(); ++i)
    {
        const auto span = static_cast<double> (anchors_[i].frame - anchors_[i - 1].frame);
        anchorBeats_[i] = anchorBeats_[i - 1] + span / framesPerBeat (i - 1);
    }
}

bool BeatGrid::operator== (const BeatGrid& other) const noexcept
{
    return sampleRate_ == other.sampleRate_ && anchors_ == other.anchors_;
}

BeatGrid BeatGrid::constant (double sampleRate, double bpm, Frame firstDownbeat, int beatsPerBar)
{
    return BeatGrid (sampleRate, { TempoAnchor { firstDownbeat, bpm, 1, beatsPerBar } });
}

double BeatGrid::framesPerBeat (std::size_t segment) const
{
    return sampleRate_ * 60.0 / anchors_[segment].bpm;
}

std::size_t BeatGrid::segmentForFrame (double frame) const
{
    // The last anchor at or before `frame`; frames before the first anchor belong to segment 0.
    const auto it = std::upper_bound (anchors_.begin(), anchors_.end(), frame,
                                      [] (double f, const TempoAnchor& a) { return f < static_cast<double> (a.frame); });
    return it == anchors_.begin() ? 0 : static_cast<std::size_t> (std::distance (anchors_.begin(), it) - 1);
}

std::size_t BeatGrid::segmentForBeat (double beat) const
{
    const auto it = std::upper_bound (anchorBeats_.begin(), anchorBeats_.end(), beat + beatEpsilon);
    return it == anchorBeats_.begin() ? 0 : static_cast<std::size_t> (std::distance (anchorBeats_.begin(), it) - 1);
}

double BeatGrid::bpmAt (double frame) const
{
    return isEmpty() ? 0.0 : anchors_[segmentForFrame (frame)].bpm;
}

double BeatGrid::framesPerBeatAt (double frame) const
{
    return isEmpty() ? 0.0 : framesPerBeat (segmentForFrame (frame));
}

int BeatGrid::beatsPerBarAt (double frame) const
{
    return isEmpty() ? 4 : anchors_[segmentForFrame (frame)].beatsPerBar;
}

double BeatGrid::barFramesAt (double frame) const
{
    if (isEmpty())
        return 0.0;
    const auto s = segmentForFrame (frame);
    return framesPerBeat (s) * anchors_[s].beatsPerBar;
}

double BeatGrid::beatAt (double frame) const
{
    if (isEmpty())
        return 0.0;
    const auto s = segmentForFrame (frame);
    return anchorBeats_[s] + (frame - static_cast<double> (anchors_[s].frame)) / framesPerBeat (s);
}

double BeatGrid::frameAt (double beat) const
{
    if (isEmpty())
        return 0.0;
    const auto s = segmentForBeat (beat);
    return static_cast<double> (anchors_[s].frame) + (beat - anchorBeats_[s]) * framesPerBeat (s);
}

int BeatGrid::beatInBar (std::int64_t beatIndex) const
{
    if (isEmpty())
        return 1;
    const auto s = segmentForBeat (static_cast<double> (beatIndex));
    const auto& a = anchors_[s];
    const auto offset = beatIndex - std::llround (anchorBeats_[s]);
    const auto bpb = static_cast<std::int64_t> (a.beatsPerBar);
    const auto pos = ((a.beatInBar - 1 + offset) % bpb + bpb) % bpb;
    return static_cast<int> (pos) + 1;
}

GridBeat BeatGrid::nearestBeat (double frame) const
{
    if (isEmpty())
        return {};
    const auto lower = static_cast<std::int64_t> (std::floor (beatAt (frame) + beatEpsilon));
    const auto fLower = frameAt (static_cast<double> (lower));
    const auto fUpper = frameAt (static_cast<double> (lower + 1));
    const bool upper = std::abs (fUpper - frame) < std::abs (fLower - frame);
    const auto index = upper ? lower + 1 : lower;
    return { upper ? fUpper : fLower, index, beatInBar (index) };
}

std::vector<GridBeat> BeatGrid::beatsBetween (double from, double to) const
{
    std::vector<GridBeat> beats;
    if (isEmpty() || ! (to > from))
        return beats;

    auto index = static_cast<std::int64_t> (std::ceil (beatAt (from) - beatEpsilon));
    for (;;)
    {
        const auto f = frameAt (static_cast<double> (index));
        if (f >= to || beats.size() >= maxBeatsReturned)
            break;
        if (f >= from - 1.0e-6)
            beats.push_back ({ f, index, beatInBar (index) });
        ++index;
    }
    return beats;
}

// Editing ------------------------------------------------------------------------------------------

BeatGrid BeatGrid::shifted (Frame frames) const
{
    auto moved = anchors_;
    for (auto& a : moved)
        a.frame += frames;
    return BeatGrid (sampleRate_, std::move (moved));
}

BeatGrid BeatGrid::withDownbeatAt (Frame frame) const
{
    if (isEmpty())
        return *this;

    if (isConstantTempo())
    {
        auto a = anchors_.front();
        a.frame = frame;
        a.beatInBar = 1;
        return BeatGrid (sampleRate_, { a });
    }

    const auto at = static_cast<double> (frame);
    return withAnchor ({ frame, bpmAt (at), 1, beatsPerBarAt (at) });
}

BeatGrid BeatGrid::withAnchor (TempoAnchor anchor) const
{
    auto next = anchors_;
    const auto it = std::lower_bound (next.begin(), next.end(), anchor.frame,
                                      [] (const TempoAnchor& a, Frame f) { return a.frame < f; });
    if (it != next.end() && it->frame == anchor.frame)
        *it = anchor;
    else
        next.insert (it, anchor);
    return BeatGrid (sampleRate_, std::move (next));
}

BeatGrid BeatGrid::withoutAnchor (std::size_t index) const
{
    if (index >= anchors_.size())
        throw std::out_of_range ("BeatGrid: no anchor at that index");
    auto next = anchors_;
    next.erase (next.begin() + static_cast<std::ptrdiff_t> (index));
    return BeatGrid (sampleRate_, std::move (next));
}

BeatGrid BeatGrid::withTempoScaled (double factor) const
{
    if (! std::isfinite (factor) || factor <= 0)
        throw std::invalid_argument ("BeatGrid: tempo factor must be positive");
    auto next = anchors_;
    for (auto& a : next)
        a.bpm *= factor;
    return BeatGrid (sampleRate_, std::move (next));
}

BeatGrid BeatGrid::withBpm (double bpm, double frame) const
{
    if (isEmpty())
        throw std::logic_error ("BeatGrid: an empty grid has no tempo to change");
    auto next = anchors_;
    next[segmentForFrame (frame)].bpm = bpm;
    return BeatGrid (sampleRate_, std::move (next));
}

} // namespace dss1
