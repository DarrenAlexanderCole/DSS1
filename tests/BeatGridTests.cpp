#include "core/BeatGrid.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stdexcept>

using namespace dss1;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double rate = 44100.0;
    constexpr double beat120 = rate * 60.0 / 120.0;  // 22050 frames
    constexpr double beat100 = rate * 60.0 / 100.0;  // 26460 frames
}

TEST_CASE ("An empty grid answers safely", "[grid]")
{
    BeatGrid grid;
    CHECK (grid.isEmpty());
    CHECK (grid.bpmAt (1000) == 0.0);
    CHECK (grid.beatsBetween (0, 1.0e6).empty());
    CHECK (grid.withDownbeatAt (500).isEmpty());
}

TEST_CASE ("Constant grid: beats, bars and positions", "[grid]")
{
    const auto grid = BeatGrid::constant (rate, 120.0, 1000);

    CHECK (grid.isConstantTempo());
    CHECK (grid.bpmAt (0) == 120.0);
    CHECK_THAT (grid.framesPerBeatAt (5.0e6), WithinAbs (beat120, 1e-9));
    CHECK_THAT (grid.barFramesAt (0), WithinAbs (beat120 * 4, 1e-9));

    CHECK_THAT (grid.frameAt (0), WithinAbs (1000, 1e-9));
    CHECK_THAT (grid.frameAt (8), WithinAbs (1000 + 8 * beat120, 1e-6));
    CHECK_THAT (grid.beatAt (1000 + 2.5 * beat120), WithinAbs (2.5, 1e-9));

    // Bars: beat 0 is a downbeat, then 2, 3, 4, 1 …
    CHECK (grid.beatInBar (0) == 1);
    CHECK (grid.beatInBar (1) == 2);
    CHECK (grid.beatInBar (3) == 4);
    CHECK (grid.beatInBar (4) == 1);

    SECTION ("it extends backwards before the first anchor")
    {
        CHECK_THAT (grid.frameAt (-1), WithinAbs (1000 - beat120, 1e-6));
        CHECK (grid.beatInBar (-1) == 4);
        CHECK (grid.beatInBar (-4) == 1);
    }
}

TEST_CASE ("frameAt and beatAt are inverses across tempo changes", "[grid]")
{
    // 120 BPM from frame 0, then 100 BPM from beat 16, then 128 BPM later.
    const auto change1 = static_cast<Frame> (16 * beat120);
    const auto change2 = change1 + static_cast<Frame> (32 * beat100);
    const BeatGrid grid (rate, { { 0, 120.0, 1, 4 }, { change1, 100.0, 1, 4 }, { change2, 128.0, 1, 4 } });

    CHECK_FALSE (grid.isConstantTempo());
    CHECK (grid.bpmAt (change1 - 1) == 120.0);
    CHECK (grid.bpmAt (change1) == 100.0);
    CHECK (grid.bpmAt (change2 + 10) == 128.0);

    CHECK_THAT (grid.beatAt (static_cast<double> (change1)), WithinAbs (16.0, 1e-9));
    CHECK_THAT (grid.beatAt (static_cast<double> (change2)), WithinAbs (48.0, 1e-9));
    CHECK_THAT (grid.frameAt (17), WithinAbs (static_cast<double> (change1) + beat100, 1e-6));

    for (double beat = -10; beat < 200; beat += 0.37)
        CHECK_THAT (grid.beatAt (grid.frameAt (beat)), WithinAbs (beat, 1e-6));

    // The bar count carries straight through the tempo changes.
    CHECK (grid.beatInBar (16) == 1);
    CHECK (grid.beatInBar (17) == 2);
    CHECK (grid.beatInBar (48) == 1);
}

TEST_CASE ("beatsBetween lists every beat line in a range", "[grid]")
{
    const auto grid = BeatGrid::constant (rate, 120.0, 0);
    const auto beats = grid.beatsBetween (0, 4 * beat120);

    REQUIRE (beats.size() == 4);
    CHECK (beats[0].isDownbeat());
    CHECK (beats[0].index == 0);
    CHECK_THAT (beats[3].frame, WithinAbs (3 * beat120, 1e-6));
    CHECK (beats[3].beatInBar == 4);

    SECTION ("a range that starts exactly on a beat includes it")
    {
        const auto from = grid.beatsBetween (beat120, beat120 + 1);
        REQUIRE (from.size() == 1);
        CHECK (from[0].index == 1);
    }

    SECTION ("an empty or reversed range gives nothing")
    {
        CHECK (grid.beatsBetween (100, 100).empty());
        CHECK (grid.beatsBetween (100, 50).empty());
    }
}

TEST_CASE ("nearestBeat quantises to the closest line", "[grid]")
{
    const auto grid = BeatGrid::constant (rate, 120.0, 0);

    CHECK (grid.nearestBeat (beat120 * 0.4).index == 0);
    CHECK (grid.nearestBeat (beat120 * 0.6).index == 1);
    CHECK_THAT (grid.nearestBeat (beat120 * 4.1).frame, WithinAbs (beat120 * 4, 1e-6));
    CHECK (grid.nearestBeat (beat120 * 4.1).isDownbeat());
}

TEST_CASE ("Editing returns new grids and leaves the original alone", "[grid][edit]")
{
    const auto grid = BeatGrid::constant (rate, 120.0, 1000);

    SECTION ("shifted nudges every anchor")
    {
        const auto moved = grid.shifted (-250);
        CHECK (moved.anchors().front().frame == 750);
        CHECK (grid.anchors().front().frame == 1000);
    }

    SECTION ("withDownbeatAt on a constant grid moves the whole grid")
    {
        const auto moved = grid.withDownbeatAt (5000);
        REQUIRE (moved.isConstantTempo());
        CHECK (moved.anchors().front().frame == 5000);
        CHECK (moved.beatInBar (0) == 1);
    }

    SECTION ("withDownbeatAt on a variable grid adds an anchor at the local tempo")
    {
        const BeatGrid variable (rate, { { 0, 120.0, 1, 4 }, { 441000, 100.0, 1, 4 } });
        const auto edited = variable.withDownbeatAt (100000);
        REQUIRE (edited.anchors().size() == 3);
        CHECK (edited.anchors()[1].frame == 100000);
        CHECK (edited.anchors()[1].bpm == 120.0);
        CHECK (edited.anchors()[2].frame == 441000);
    }

    SECTION ("tempo x2 and /2")
    {
        CHECK (grid.withTempoScaled (2.0).bpmAt (0) == 240.0);
        CHECK (grid.withTempoScaled (0.5).bpmAt (0) == 60.0);
        CHECK_THROWS_AS (grid.withTempoScaled (0.0), std::invalid_argument);
    }

    SECTION ("withBpm changes only the segment under the frame")
    {
        const BeatGrid variable (rate, { { 0, 120.0, 1, 4 }, { 441000, 100.0, 1, 4 } });
        const auto edited = variable.withBpm (122.0, 500000);
        CHECK (edited.bpmAt (0) == 120.0);
        CHECK (edited.bpmAt (500000) == 122.0);
    }

    SECTION ("withAnchor replaces an anchor at the same frame; withoutAnchor removes one")
    {
        const auto replaced = grid.withAnchor ({ 1000, 126.0, 1, 4 });
        REQUIRE (replaced.isConstantTempo());
        CHECK (replaced.bpmAt (0) == 126.0);

        const auto two = grid.withAnchor ({ 90000, 100.0, 1, 4 });
        CHECK (two.anchors().size() == 2);
        CHECK (two.withoutAnchor (1) == grid);
        CHECK (grid.withoutAnchor (0).isEmpty());
        CHECK_THROWS_AS (grid.withoutAnchor (5), std::out_of_range);
    }
}

TEST_CASE ("Invalid grids are rejected", "[grid]")
{
    CHECK_THROWS_AS (BeatGrid (0.0, { { 0, 120.0, 1, 4 } }), std::invalid_argument);
    CHECK_THROWS_AS (BeatGrid (rate, { { 0, 0.0, 1, 4 } }), std::invalid_argument);
    CHECK_THROWS_AS (BeatGrid (rate, { { 0, 120.0, 5, 4 } }), std::invalid_argument);
    CHECK_THROWS_AS (BeatGrid (rate, { { 100, 120.0, 1, 4 }, { 100, 100.0, 1, 4 } }), std::invalid_argument);
    CHECK_THROWS_AS (BeatGrid (rate, { { 200, 120.0, 1, 4 }, { 100, 100.0, 1, 4 } }), std::invalid_argument);
}
