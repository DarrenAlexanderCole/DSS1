#include "core/TrackRecord.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <stdexcept>

using namespace dss1;

namespace
{
    TrackRecord makeTrack()
    {
        TrackRecord t;
        t.identity = { "/Users/dj/Music/Crate/Artist - Track.wav", "abc123" };
        t.sampleRate = 44100.0;
        t.lengthFrames = 44100 * 240;
        t.grid = BeatGrid (44100.0, { { 1000, 124.0, 1, 4 }, { 4410000, 126.0, 1, 4 } });
        t.hotCues[0] = HotCue { 1000, std::nullopt, "Intro", CueColour { 0xE6, 0x32, 0x32 } };
        t.hotCues[3] = HotCue { 882000, Frame { 970200 }, "Drop loop", std::nullopt };
        t.hotCues[7] = HotCue { 5000000, std::nullopt, "", std::nullopt };
        t.addMemoryCue ({ 2000000, "Break", std::nullopt });
        t.addMemoryCue ({ 500000, "", CueColour { 0, 0x80, 0xFF } });
        t.savedLoops[0] = SavedLoop { 1000, 85000, "1 bar", std::nullopt, false };
        t.savedLoops[4] = SavedLoop { 3000000, 3340000, "", CueColour { 1, 2, 3 }, true };
        return t;
    }
}

TEST_CASE ("Hot cue letters run A to H", "[track]")
{
    CHECK (TrackRecord::hotCueLetter (0) == 'A');
    CHECK (TrackRecord::hotCueLetter (7) == 'H');
    CHECK_THROWS_AS (TrackRecord::hotCueLetter (8), std::out_of_range);
}

TEST_CASE ("Memory cues stay in order and replace duplicates", "[track]")
{
    TrackRecord t;
    t.addMemoryCue ({ 300, "c", std::nullopt });
    t.addMemoryCue ({ 100, "a", std::nullopt });
    t.addMemoryCue ({ 200, "b", std::nullopt });
    t.addMemoryCue ({ 200, "b2", std::nullopt });

    REQUIRE (t.memoryCues.size() == 3);
    CHECK (t.memoryCues[0].name == "a");
    CHECK (t.memoryCues[1].name == "b2");
    CHECK (t.memoryCues[2].name == "c");
}

TEST_CASE ("A track record survives a JSON round trip unchanged", "[track][json]")
{
    const auto original = makeTrack();
    const auto json = toJson (original);

    CHECK (json["schema"] == TrackRecord::schemaVersion);
    CHECK (json["hotCues"].size() == 3);
    CHECK (json["hotCues"][0]["slot"] == "A");
    CHECK (json["hotCues"][0]["colour"] == "#E63232");
    CHECK (json["savedLoops"][1]["slot"] == 5);

    const auto restored = trackRecordFromJson (nlohmann::json::parse (json.dump()));
    CHECK (restored == original);
    CHECK (restored.problems().empty());
}

TEST_CASE ("Reading JSON from a newer DSS1 is refused rather than misread", "[track][json]")
{
    auto json = toJson (makeTrack());
    json["schema"] = TrackRecord::schemaVersion + 1;
    CHECK_THROWS_AS (trackRecordFromJson (json), std::runtime_error);
}

TEST_CASE ("Malformed slots and colours are rejected", "[track][json]")
{
    auto badSlot = toJson (makeTrack());
    badSlot["hotCues"][0]["slot"] = "J";
    CHECK_THROWS (trackRecordFromJson (badSlot));

    auto badLoop = toJson (makeTrack());
    badLoop["savedLoops"][0]["slot"] = 9;
    CHECK_THROWS (trackRecordFromJson (badLoop));

    auto badColour = toJson (makeTrack());
    badColour["hotCues"][0]["colour"] = "red";
    CHECK_THROWS (trackRecordFromJson (badColour));
}

TEST_CASE ("problems() reports cues and loops that don't make sense", "[track]")
{
    auto t = makeTrack();
    t.hotCues[1] = HotCue { t.lengthFrames + 10, std::nullopt, "", std::nullopt };
    t.savedLoops[2] = SavedLoop { 5000, 4000, "", std::nullopt, false };

    const auto found = t.problems();
    REQUIRE (found.size() == 2);
    CHECK (found[0] == "hot cue B is outside the track");
    CHECK (found[1] == "saved loop 3 ends before it starts");
}

#include "core/TestExport.h"

TEST_CASE ("The test export puts cues on bar lines of a 120 BPM grid", "[testexport]")
{
    const auto r = makeTestRecord ("/x.wav", 48000.0, 48000 * 180);
    REQUIRE (r.grid);
    CHECK (r.problems().empty());

    for (std::size_t i = 0; i < 8; ++i)
    {
        REQUIRE (r.hotCues[i]);
        const auto frame = static_cast<double> (r.hotCues[i]->frame);
        CHECK (frame == 48000.0 * 8.0 * static_cast<double> (i + 1));
        CHECK (r.grid->nearestBeat (frame).beatInBar == 1);
    }
    CHECK (r.hotCues[7]->name == "H 1:04.000");
    REQUIRE (r.savedLoops[0]);
    CHECK (r.savedLoops[0]->end == 48000 * 40);
    CHECK (testPositions (r).size() == 11);

    // A 30-second track only gets what fits.
    const auto shortTrack = makeTestRecord ("/x.wav", 44100.0, 44100 * 30);
    CHECK (shortTrack.hotCues[2]);
    CHECK_FALSE (shortTrack.hotCues[3]);
    CHECK_FALSE (shortTrack.savedLoops[0]);
    CHECK (formatTime (64.0) == "1:04.000");
}
