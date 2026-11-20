#include "core/Deliver.h"
#include "core/TestExport.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace dss1;

namespace
{
    id3::Bytes readFixture (const char* name)
    {
        std::ifstream in (std::filesystem::path (DSS1_TEST_FIXTURES) / name, std::ios::binary);
        return id3::Bytes (std::istreambuf_iterator<char> (in), {});
    }

    StoredTrack stored (std::int64_t id, const std::string& path)
    {
        StoredTrack t;
        t.id = id;
        t.record = makeTestRecord (path, 44100.0, 44100 * 60);
        t.record.info.title = "Title " + std::to_string (id);
        t.record.info.artist = "Artist";
        t.record.info.camelot = "8A";
        t.record.info.bpm = 120.0;
        return t;
    }
}

TEST_CASE ("A track counts as prepared once it has a cue, loop or grid", "[deliver]")
{
    TrackRecord r;
    r.sampleRate = 44100;
    r.lengthFrames = 44100 * 10;
    CHECK_FALSE (deliver::isPrepared (r));
    r.memoryCues.push_back ({ 100, {}, std::nullopt });
    CHECK (deliver::isPrepared (r));
    r.memoryCues.clear();
    r.savedLoops[3] = SavedLoop { 100, 44100, {}, std::nullopt, false };
    CHECK (deliver::isPrepared (r));
    r.savedLoops[3].reset();
    r.grid = BeatGrid::constant (44100, 120, 0);
    CHECK (deliver::isPrepared (r));
}

TEST_CASE ("The Rekordbox collection points at the original files and keeps every track's details", "[deliver]")
{
    const std::vector tracks { stored (7, "/Music/One.wav"), stored (9, "/Music/Two.mp3") };
    const auto c = deliver::rekordboxCollection (tracks, "DSS1");
    REQUIRE (c.tracks.size() == 2);
    CHECK (c.tracks[0].trackId == "7");
    CHECK (c.tracks[0].location == rekordbox::locationFromPath ("/Music/One.wav"));
    CHECK (c.tracks[0].name == "Title 7");
    CHECK (c.tracks[0].kind == "WAV File");
    CHECK (c.tracks[1].kind == "MP3 File");
    CHECK (c.tracks[0].tonality == "8A");
    CHECK_FALSE (c.tracks[0].marks.empty());
    REQUIRE (c.playlists.children.size() == 1);
    CHECK (c.playlists.children[0].name == "DSS1");
    CHECK (c.playlists.children[0].trackKeys == std::vector<std::string> { "7", "9" });

    // It survives a write and read back.
    CHECK (rekordbox::parse (rekordbox::write (c)).tracks.size() == 2);
}

TEST_CASE ("Serato copies carry the cues, and the grid only when asked", "[deliver]")
{
    const auto t = stored (1, "/Music/clicks.wav");
    const auto original = readFixture ("clicks.wav");
    REQUIRE (! original.empty());

    const auto copy = deliver::seratoTaggedCopy (original, "/Music/clicks.wav", t.record, {});
    const auto frames = id3::readGeobFrames (copy, id3::Container::wav);
    bool markers = false, grid = false;
    for (const auto& f : frames)
    {
        markers |= f.description == serato::markers2Description;
        grid |= f.description == serato::beatGridDescription;
    }
    CHECK (markers);
    CHECK_FALSE (grid);

    serato::ExportOptions withGrid;
    withGrid.includeBeatGrid = true;
    const auto copy2 = deliver::seratoTaggedCopy (original, "/Music/clicks.wav", t.record, withGrid);
    grid = false;
    for (const auto& f : id3::readGeobFrames (copy2, id3::Container::wav))
        grid |= f.description == serato::beatGridDescription;
    CHECK (grid);

    CHECK (deliver::canTagForSerato ("/a/b.MP3"));
    CHECK_FALSE (deliver::canTagForSerato ("/a/b.flac"));
    CHECK_THROWS (deliver::seratoTaggedCopy (original, "/Music/clicks.flac", t.record, {}));
}

TEST_CASE ("Copy names stay unique when two sources share a name", "[deliver]")
{
    const auto names = deliver::copyNames ({ "/A/Intro.mp3", "/B/intro.mp3", "/C/Intro.mp3", "/D/Outro.wav", "/E/noext" });
    CHECK (names == std::vector<std::string> { "Intro.mp3", "intro (2).mp3", "Intro (3).mp3", "Outro.wav", "noext" });
}

TEST_CASE ("Notes go to Rekordbox as the track's comments", "[deliver]")
{
    auto withNotes = stored (3, "/Music/Three.wav");
    withNotes.record.info.notes = "peak time";
    const std::vector tracks { withNotes, stored (4, "/Music/Four.wav") };
    const auto xml = rekordbox::write (deliver::rekordboxCollection (tracks, "DSS1"));
    CHECK (xml.find ("Comments=\"peak time\"") != std::string::npos);
    const auto back = rekordbox::parse (xml);
    REQUIRE (back.tracks.size() == 2);
    const auto& other = back.tracks[0].otherAttributes;
    CHECK (std::find (other.begin(), other.end(), std::pair<std::string, std::string> ("Comments", "peak time")) != other.end());
    CHECK (back.tracks[1].otherAttributes.empty());
}
