#include "core/RekordboxMapping.h"
#include "core/RekordboxXml.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace dss1;
using namespace dss1::rekordbox;
using Catch::Approx;

namespace
{
    // Shaped like a Rekordbox 6/7 "Export Collection in xml format" file.
    const char* const sampleXml = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<DJ_PLAYLISTS Version="1.0.0">
  <PRODUCT Name="rekordbox" Version="7.1.3" Company="AlphaTheta"/>
  <COLLECTION Entries="2">
    <TRACK TrackID="90889481" Name="Vysoko (Original Mix)" Artist="Enigmatic" Composer="" Album="Night"
           Genre="Techno" Kind="WAV File" Size="52918372" TotalTime="300" DiscNumber="0" TrackNumber="1"
           Year="2024" AverageBpm="121.00" DateAdded="2026-11-01" BitRate="1411" SampleRate="44100"
           Comments="" PlayCount="3" Rating="0" Location="file://localhost/Users/dj/Music/Crate/Vysoko%20(Original%20Mix).wav"
           Remixer="" Tonality="8A" Label="" Mix="">
      <TEMPO Inizio="0.025" Bpm="121.00" Metro="4/4" Battito="1"/>
      <TEMPO Inizio="2.008" Bpm="121.00" Metro="4/4" Battito="1"/>
      <POSITION_MARK Name="" Type="0" Start="0.025" Num="-1"/>
      <POSITION_MARK Name="Drop" Type="0" Start="31.761" Num="0" Red="255" Green="55" Blue="111"/>
      <POSITION_MARK Name="" Type="0" Start="95.232" Num="1" Red="69" Green="172" Blue="219"/>
      <POSITION_MARK Name="Loop" Type="4" Start="63.497" End="71.432" Num="2" Red="125" Green="193" Blue="61"/>
      <POSITION_MARK Name="Outro loop" Type="4" Start="270.000" End="277.934" Num="-1"/>
      <POSITION_MARK Name="" Type="1" Start="0.000" Num="-1"/>
    </TRACK>
    <TRACK TrackID="2" Name="Café" Artist="Ünïcode" Kind="MP3 File" Location="file://localhost/Users/dj/Music/Caf%C3%A9.mp3"/>
  </COLLECTION>
  <PLAYLISTS>
    <NODE Type="0" Name="ROOT" Count="1">
      <NODE Type="1" Name="Friday" KeyType="0" Entries="2">
        <TRACK Key="90889481"/>
        <TRACK Key="2"/>
      </NODE>
    </NODE>
  </PLAYLISTS>
</DJ_PLAYLISTS>
)XML";

    constexpr double rate = 44100.0;
}

TEST_CASE ("A Rekordbox collection is read with tracks, grids, marks and playlists", "[rekordbox]")
{
    const auto c = parse (sampleXml);

    CHECK (c.productName == "rekordbox");
    REQUIRE (c.tracks.size() == 2);

    const auto& t = c.tracks[0];
    CHECK (t.trackId == "90889481");
    CHECK (t.name == "Vysoko (Original Mix)");
    CHECK (t.averageBpm == Approx (121.0));
    CHECK (t.sampleRate == Approx (44100.0));
    CHECK (t.tonality == "8A");
    REQUIRE (t.tempos.size() == 2);
    CHECK (t.tempos[0].inizio == Approx (0.025));
    REQUIRE (t.marks.size() == 6);
    CHECK (t.marks[1].num == 0);
    CHECK (t.marks[1].colour == Rgb { 255, 55, 111 });
    CHECK (t.marks[3].type == MarkType::loop);
    CHECK (*t.marks[3].end == Approx (71.432));

    CHECK (c.tracks[1].name == "Café");

    REQUIRE (c.playlists.children.size() == 1);
    CHECK (c.playlists.children[0].name == "Friday");
    CHECK (c.playlists.children[0].trackKeys == std::vector<std::string> { "90889481", "2" });
}

TEST_CASE ("Unknown TRACK attributes survive a read and write", "[rekordbox]")
{
    const auto c = parse (sampleXml);
    const auto again = parse (write (c));
    CHECK (again == c);

    const auto& extra = again.tracks[0].otherAttributes;
    const auto has = [&] (const std::string& key) {
        return std::any_of (extra.begin(), extra.end(), [&] (const auto& kv) { return kv.first == key; });
    };
    CHECK (has ("PlayCount"));
    CHECK (has ("DateAdded"));
    CHECK (has ("Label"));
}

TEST_CASE ("Files that aren't Rekordbox collections are refused with a readable message", "[rekordbox]")
{
    CHECK_THROWS_AS (parse ("this is not xml <"), std::runtime_error);
    CHECK_THROWS_AS (parse ("<?xml version=\"1.0\"?><playlist/>"), std::runtime_error);
}

TEST_CASE ("Locations convert to and from file paths", "[rekordbox]")
{
    CHECK (locationFromPath ("/Users/dj/My Music/A&B #1.mp3")
           == "file://localhost/Users/dj/My%20Music/A%26B%20%231.mp3");
    CHECK (pathFromLocation ("file://localhost/Users/dj/My%20Music/A%26B%20%231.mp3") == "/Users/dj/My Music/A&B #1.mp3");

    // Non-ASCII names are percent-encoded as UTF-8 bytes.
    CHECK (locationFromPath ("/Users/dj/Café.mp3") == "file://localhost/Users/dj/Caf%C3%A9.mp3");
    CHECK (pathFromLocation ("file://localhost/Users/dj/Caf%C3%A9.mp3") == "/Users/dj/Café.mp3");

    // Windows.
    CHECK (locationFromPath ("C:\\Music\\a b.mp3") == "file://localhost/C:/Music/a%20b.mp3");
    CHECK (pathFromLocation ("file://localhost/C:/Music/a%20b.mp3") == "C:/Music/a b.mp3");

    CHECK (pathFromLocation ("file:///Users/dj/x.wav") == "/Users/dj/x.wav");
    CHECK (pathFromLocation ("http://example.com/x.wav").empty());
}

TEST_CASE ("Importing a Rekordbox track fills hot cues, memory cues, saved loops and the grid", "[rekordbox]")
{
    const auto c = parse (sampleXml);
    const auto imported = importTrack (c.tracks[0], rate, static_cast<Frame> (300 * rate));
    const auto& r = imported.record;

    CHECK (r.identity.path == "/Users/dj/Music/Crate/Vysoko (Original Mix).wav");

    REQUIRE (r.hotCues[0]);
    CHECK (r.hotCues[0]->frame == std::llround (31.761 * rate));
    CHECK (r.hotCues[0]->name == "Drop");
    CHECK (r.hotCues[0]->colour == CueColour { 255, 55, 111 });
    REQUIRE (r.hotCues[1]);
    REQUIRE (r.hotCues[2]);
    REQUIRE (r.hotCues[2]->loopEnd);
    CHECK (*r.hotCues[2]->loopEnd == std::llround (71.432 * rate));
    CHECK_FALSE (r.hotCues[3]);

    REQUIRE (r.memoryCues.size() == 1);
    CHECK (r.memoryCues[0].frame == std::llround (0.025 * rate));

    REQUIRE (r.savedLoops[0]);
    CHECK (r.savedLoops[0]->name == "Outro loop");
    CHECK (r.savedLoops[0]->start == std::llround (270.0 * rate));

    // The second TEMPO is on a beat the first already implies, so the grid is one tempo.
    REQUIRE (r.grid);
    CHECK (r.grid->isConstantTempo());
    CHECK (r.grid->anchors()[0].frame == std::llround (0.025 * rate));
    CHECK (r.grid->bpmAt (0) == Approx (121.0));

    // The fade-in marker has no DSS1 equivalent and is reported.
    REQUIRE (imported.notes.size() == 1);
    CHECK (imported.notes[0].find ("fade or load") != std::string::npos);
}

TEST_CASE ("A tempo change in Rekordbox becomes a variable-tempo grid", "[rekordbox]")
{
    const std::vector<Tempo> tempos {
        { 0.100, 120.0, "4/4", 1 },
        { 2.100, 120.0, "4/4", 1 },   // one bar later: redundant
        { 4.100, 128.0, "4/4", 1 },   // tempo change
        { 5.975, 128.0, "4/4", 1 },   // one bar at 128: redundant
        { 6.500, 128.0, "4/4", 3 },   // off the beat: a real re-anchor
    };
    const auto grid = gridFromTempos (tempos, rate);
    REQUIRE (grid);
    REQUIRE (grid->anchors().size() == 3);
    CHECK (grid->anchors()[1].bpm == Approx (128.0));
    CHECK (grid->anchors()[2].beatInBar == 3);
}

TEST_CASE ("More than eight memory loops are reported, not lost silently", "[rekordbox]")
{
    Track t;
    t.location = locationFromPath ("/x.wav");
    for (int i = 0; i < 10; ++i)
        t.marks.push_back ({ "", MarkType::loop, 10.0 * i, 10.0 * i + 4.0, -1, std::nullopt });

    const auto imported = importTrack (t, rate, static_cast<Frame> (200 * rate));
    for (const auto& slot : imported.record.savedLoops)
        CHECK (slot);
    REQUIRE (imported.notes.size() == 1);
    CHECK (imported.notes[0].find ("2 memory loops") != std::string::npos);
}

TEST_CASE ("DSS1 track data goes out to Rekordbox and comes back to the millisecond", "[rekordbox]")
{
    TrackRecord r;
    r.identity.path = "/Users/dj/Music/Crate/Artist - Track.wav";
    r.sampleRate = rate;
    r.lengthFrames = static_cast<Frame> (240 * rate);
    r.grid = BeatGrid (rate, { { 22050, 124.0, 1, 4 }, { 4'410'000, 126.0, 1, 4 } });
    r.hotCues[0] = HotCue { 22050, std::nullopt, "Intro", CueColour { 230, 50, 50 } };
    r.hotCues[5] = HotCue { 882'011, Frame { 970'217 }, "Drop loop", std::nullopt };
    r.addMemoryCue ({ 2'000'003, "Break", std::nullopt });
    r.savedLoops[0] = SavedLoop { 1'323'000, 1'408'500, "4 bars", std::nullopt, false };

    TrackDetails details;
    details.trackId = "1";
    details.name = "Track";
    details.artist = "Artist";
    details.kind = "WAV File";

    Collection out;
    out.tracks.push_back (exportTrack (r, details));
    out.playlists.children.push_back ({ Node::Kind::playlist, "DSS1", {}, { "1" }, 0 });

    const auto back = parse (write (out));
    REQUIRE (back.tracks.size() == 1);
    CHECK (back.tracks[0].totalTime == Approx (240.0));
    CHECK_FALSE (back.tracks[0].averageBpm);  // two tempos: no single average written

    const auto imported = importTrack (back.tracks[0], rate, r.lengthFrames);
    const auto& r2 = imported.record;
    const auto halfMs = static_cast<Frame> (std::ceil (rate * 0.0005));
    const auto near = [halfMs] (Frame a, Frame b) { return std::abs (a - b) <= halfMs; };

    CHECK (imported.notes.empty());
    CHECK (r2.identity.path == r.identity.path);
    REQUIRE (r2.hotCues[0]);
    CHECK (near (r2.hotCues[0]->frame, 22050));
    CHECK (r2.hotCues[0]->colour == CueColour { 230, 50, 50 });
    REQUIRE (r2.hotCues[5]);
    CHECK (near (r2.hotCues[5]->frame, 882'011));
    CHECK (near (*r2.hotCues[5]->loopEnd, 970'217));
    REQUIRE (r2.memoryCues.size() == 1);
    CHECK (near (r2.memoryCues[0].frame, 2'000'003));
    REQUIRE (r2.savedLoops[0]);
    CHECK (r2.savedLoops[0]->name == "4 bars");

    REQUIRE (r2.grid);
    REQUIRE (r2.grid->anchors().size() == 2);
    CHECK (r2.grid->bpmAt (0) == Approx (124.0));
    CHECK (r2.grid->bpmAt (5'000'000) == Approx (126.0));
}

TEST_CASE ("An exported grid starts on the first beat inside the track", "[rekordbox]")
{
    // Downbeat 10 s in at 120 BPM: the grid runs backwards, so the first beat in the track is at 0.0 s
    // (10 s = 20 beats, so beat -20 lands exactly at 0) and it is a downbeat (20 is a multiple of 4).
    const auto grid = BeatGrid::constant (rate, 120.0, static_cast<Frame> (10 * rate));
    const auto tempos = temposFromGrid (grid);
    REQUIRE (tempos.size() == 1);
    CHECK (tempos[0].inizio == Approx (0.0).margin (1e-6));
    CHECK (tempos[0].battito == 1);

    // Shift by a quarter beat: the first beat in the track is then 0.125 s in, and it's beat 1 of a bar.
    const auto shifted = BeatGrid::constant (rate, 120.0, static_cast<Frame> (10.125 * rate));
    const auto t2 = temposFromGrid (shifted);
    CHECK (t2[0].inizio == Approx (0.125).margin (1e-4));
    CHECK (t2[0].battito == 1);
}

TEST_CASE ("MP3 positions are shifted by the encoder delay Rekordbox counts", "[rekordbox]")
{
    TrackRecord r;
    r.identity.path = "/Music/a.mp3";
    r.sampleRate = rate;
    r.lengthFrames = static_cast<Frame> (60 * rate);
    r.exportOffsetFrames = 1105;
    r.grid = BeatGrid::constant (rate, 120.0, 0);
    r.hotCues[0] = HotCue { 44100, std::nullopt, "", std::nullopt };

    const auto t = exportTrack (r, {});
    REQUIRE (t.marks.size() == 1);
    CHECK (t.marks[0].start == Approx (1.0 + 1105.0 / rate));
    REQUIRE (! t.tempos.empty());
    CHECK (t.tempos[0].inizio == Approx (1105.0 / rate));

    const auto back = importTrack (t, rate, r.lengthFrames, 1105);
    REQUIRE (back.record.hotCues[0]);
    CHECK (back.record.hotCues[0]->frame == 44100);
    REQUIRE (back.record.grid);
    CHECK (back.record.grid->anchors()[0].frame == 0);
    CHECK (back.record.exportOffsetFrames == 1105);
}
