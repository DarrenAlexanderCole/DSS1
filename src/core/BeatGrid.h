#pragma once

#include <cstdint>
#include <vector>

namespace dss1
{

/// A position in a track, counted in sample frames at the file's own sample rate.
/// Frames are exact; seconds are only worked out at the edges (UI, Rekordbox, Serato).
using Frame = std::int64_t;

/// The start of a stretch of constant tempo. A grid is one or more anchors in time order;
/// each tempo runs until the next anchor (the first also runs backwards to the start of the track).
struct TempoAnchor
{
    Frame frame = 0;      ///< where this tempo starts; normally on a beat
    double bpm = 120.0;
    int beatInBar = 1;    ///< which beat of the bar the anchor sits on, 1 … beatsPerBar
    int beatsPerBar = 4;  ///< 4 for 4/4, 3 for 3/4 …

    bool operator== (const TempoAnchor&) const = default;
};

/// One beat line, for drawing or quantising.
struct GridBeat
{
    double frame = 0;        ///< exact (fractional) frame of the beat
    std::int64_t index = 0;  ///< beat number counted from the first anchor (negative before it)
    int beatInBar = 1;       ///< 1 = downbeat

    bool isDownbeat() const noexcept { return beatInBar == 1; }
};

/// A beat grid with one tempo (constant) or several (variable tempo).
///
/// Beats are numbered from the first anchor: beat 0 sits on it, beat 1 is the next one, and so on,
/// continuing through every tempo change. All lookups are O(log anchors).
///
/// The class is an immutable value: editing operations return a new grid.
class BeatGrid
{
public:
    /// An empty grid (no tempo known). Lookups on an empty grid return 0 / empty results.
    BeatGrid() = default;

    /// Throws std::invalid_argument if the sample rate or any anchor is out of range,
    /// or the anchors are not in strictly increasing frame order.
    BeatGrid (double sampleRate, std::vector<TempoAnchor> anchors);

    /// One tempo everywhere, with a downbeat on `firstDownbeat`.
    static BeatGrid constant (double sampleRate, double bpm, Frame firstDownbeat, int beatsPerBar = 4);

    static constexpr double minBpm = 1.0;
    static constexpr double maxBpm = 999.0;

    double sampleRate() const noexcept                   { return sampleRate_; }
    const std::vector<TempoAnchor>& anchors() const noexcept { return anchors_; }
    bool isEmpty() const noexcept                        { return anchors_.empty(); }
    bool isConstantTempo() const noexcept                { return anchors_.size() == 1; }

    double bpmAt (double frame) const;
    double framesPerBeatAt (double frame) const;
    int beatsPerBarAt (double frame) const;
    double barFramesAt (double frame) const;

    /// Fractional beat number at a frame (0 = first anchor, 1.5 = halfway between beats 1 and 2).
    double beatAt (double frame) const;
    /// The frame where a (fractional) beat number falls. Inverse of beatAt.
    double frameAt (double beat) const;
    /// Position in the bar (1 = downbeat) of a whole beat number.
    int beatInBar (std::int64_t beatIndex) const;

    /// The beat line closest to `frame`.
    GridBeat nearestBeat (double frame) const;
    /// Every beat line with from <= frame < to, in order.
    std::vector<GridBeat> beatsBetween (double from, double to) const;

    // Editing ----------------------------------------------------------------------------------

    /// Moves the whole grid by `frames` (a nudge). Positive moves it later.
    BeatGrid shifted (Frame frames) const;

    /// Makes `frame` a downbeat. On a constant grid the whole grid moves; on a variable grid a new
    /// anchor (same tempo as the music there) starts at `frame`, leaving earlier tempo changes alone.
    BeatGrid withDownbeatAt (Frame frame) const;

    /// Adds an anchor, or replaces the one already at the same frame.
    BeatGrid withAnchor (TempoAnchor anchor) const;

    /// Removes the anchor at `index`. Removing the last remaining anchor gives an empty grid.
    BeatGrid withoutAnchor (std::size_t index) const;

    /// Multiplies every tempo by `factor`, e.g. 2 for "×2" or 0.5 for "÷2". Anchor frames stay put.
    BeatGrid withTempoScaled (double factor) const;

    /// Sets one tempo for a constant grid, or for the segment containing `frame` on a variable grid.
    BeatGrid withBpm (double bpm, double frame = 0) const;

    /// Exact equality (same sample rate and identical anchors).
    bool operator== (const BeatGrid& other) const noexcept;

private:
    std::size_t segmentForFrame (double frame) const;
    std::size_t segmentForBeat (double beat) const;
    double framesPerBeat (std::size_t segment) const;

    double sampleRate_ = 0;
    std::vector<TempoAnchor> anchors_;
    std::vector<double> anchorBeats_;  ///< beat number at each anchor, accumulated through the segments
};

} // namespace dss1
