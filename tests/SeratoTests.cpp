#include "core/Id3.h"
#include "core/SeratoTags.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace dss1;
using namespace dss1::serato;
using Catch::Approx;

namespace
{
    constexpr double rate = 44100.0;

    Bytes bytesOf (const std::string& s)
    {
        return Bytes (s.begin(), s.end());
    }

    void putLE32 (Bytes& b, std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            b.push_back (static_cast<std::uint8_t> ((v >> (8 * i)) & 0xFF));
    }

    void putBE32 (Bytes& b, std::uint32_t v)
    {
        for (int i = 3; i >= 0; --i)
            b.push_back (static_cast<std::uint8_t> ((v >> (8 * i)) & 0xFF));
    }

    /// A tiny but valid WAV: fmt chunk, a LIST chunk with odd length, 6 bytes of audio.
    Bytes makeWav()
    {
        Bytes fmt { 1, 0, 2, 0 };            // PCM, stereo
        putLE32 (fmt, 44100);
        putLE32 (fmt, 44100 * 4);
        fmt.insert (fmt.end(), { 4, 0, 16, 0 });

        Bytes body = bytesOf ("WAVE");
        auto chunk = [&] (const char* id, const Bytes& data) {
            body.insert (body.end(), id, id + 4);
            putLE32 (body, static_cast<std::uint32_t> (data.size()));
            body.insert (body.end(), data.begin(), data.end());
            if (data.size() & 1)
                body.push_back (0);
        };
        chunk ("fmt ", fmt);
        chunk ("LIST", bytesOf ("INFOabc"));
        chunk ("data", Bytes { 1, 2, 3, 4, 5, 6 });

        Bytes file = bytesOf ("RIFF");
        putLE32 (file, static_cast<std::uint32_t> (body.size()));
        file.insert (file.end(), body.begin(), body.end());
        return file;
    }

    Bytes makeAiff()
    {
        Bytes body = bytesOf ("AIFF");
        auto chunk = [&] (const char* id, const Bytes& data) {
            body.insert (body.end(), id, id + 4);
            putBE32 (body, static_cast<std::uint32_t> (data.size()));
            body.insert (body.end(), data.begin(), data.end());
            if (data.size() & 1)
                body.push_back (0);
        };
        chunk ("COMM", Bytes (18, 0));
        chunk ("SSND", Bytes { 0, 0, 0, 0, 0, 0, 0, 0, 9, 9 });

        Bytes file = bytesOf ("FORM");
        putBE32 (file, static_cast<std::uint32_t> (body.size()));
        file.insert (file.end(), body.begin(), body.end());
        return file;
    }

    /// An ID3v2.3 tag with one title frame, followed by fake MP3 frames.
    Bytes makeMp3WithTitle()
    {
        const auto title = bytesOf (std::string (1, '\0') + "My Title");
        Bytes frame = bytesOf ("TIT2");
        putBE32 (frame, static_cast<std::uint32_t> (title.size()));
        frame.insert (frame.end(), { 0, 0 });
        frame.insert (frame.end(), title.begin(), title.end());

        Bytes file = bytesOf ("ID3");
        file.insert (file.end(), { 3, 0, 0, 0, 0, 0, static_cast<std::uint8_t> (frame.size()) });
        file.insert (file.end(), frame.begin(), frame.end());
        file.insert (file.end(), { 0xFF, 0xFB, 0x90, 0x64, 0x11, 0x22 });
        return file;
    }

    std::uint32_t le32At (const Bytes& b, std::size_t p)
    {
        return std::uint32_t (b[p]) | (std::uint32_t (b[p + 1]) << 8) | (std::uint32_t (b[p + 2]) << 16) | (std::uint32_t (b[p + 3]) << 24);
    }

    bool contains (const Bytes& haystack, const Bytes& needle)
    {
        return std::search (haystack.begin(), haystack.end(), needle.begin(), needle.end()) != haystack.end();
    }

    Markers sampleMarkers()
    {
        Markers m;
        m.cues.push_back ({ 0, 31761, defaultCueColour (0), "Drop" });
        m.cues.push_back ({ 3, 95232, CueColour { 1, 2, 3 }, "" });
        Loop l;
        l.index = 0;
        l.startMs = 63497;
        l.endMs = 71432;
        l.locked = true;
        l.name = "4 bars";
        m.loops.push_back (l);
        return m;
    }
}

TEST_CASE ("A Serato cue entry has the documented layout", "[serato]")
{
    Markers m;
    m.cues.push_back ({ 2, 0x01020304, CueColour { 0xCC, 0x88, 0x00 }, "" });
    const auto payload = encodeMarkers (m);

    const Bytes expected { 'C', 'U', 'E', 0, 0, 0, 0, 0x0D,   // name, length 13
                           0x00, 0x02, 0x01, 0x02, 0x03, 0x04, 0x00, 0xCC, 0x88, 0x00, 0x00, 0x00, 0x00 };
    CHECK (contains (payload, expected));
    CHECK (payload[0] == 0x01);
    CHECK (payload[1] == 0x01);
    CHECK (payload.back() == 0x00);
}

TEST_CASE ("Serato markers read back exactly as written", "[serato]")
{
    const auto m = sampleMarkers();
    CHECK (decodeMarkers (encodeMarkers (m)) == m);
}

TEST_CASE ("The Markers2 frame uses Serato's base64 and padding", "[serato]")
{
    const auto payload = encodeMarkers (sampleMarkers());
    const auto content = wrapMarkersForGeob (payload);

    CHECK (content[0] == 0x01);
    CHECK (content[1] == 0x01);
    CHECK (content.size() >= 470);
    CHECK (std::find (content.begin(), content.end(), std::uint8_t ('=')) == content.end());

    // Line feeds every 72 characters.
    const auto end = std::find (content.begin() + 2, content.end(), std::uint8_t { 0 });
    std::size_t run = 0;
    for (auto it = content.begin() + 2; it != end; ++it)
    {
        if (*it == '\n')
        {
            CHECK (run == 72);
            run = 0;
        }
        else
        {
            ++run;
        }
    }
    CHECK (run <= 72);

    CHECK (decodeMarkers (unwrapMarkersFromGeob (content)) == sampleMarkers());
}

TEST_CASE ("Malformed Serato data is refused, not misread", "[serato]")
{
    CHECK_THROWS_AS (decodeMarkers (Bytes { 0x02, 0x01, 0x00 }), std::runtime_error);
    auto cut = encodeMarkers (sampleMarkers());
    cut.resize (20);
    CHECK_THROWS_AS (decodeMarkers (cut), std::runtime_error);
    CHECK_THROWS_AS (decodeGrid (Bytes { 0x01, 0x00, 0, 0, 0, 5, 1, 2 }), std::runtime_error);
}

TEST_CASE ("Serato grids encode markers, beats and the final tempo", "[serato]")
{
    Grid g;
    g.markers.push_back ({ 0.025f, 64 });
    g.lastPositionSeconds = 31.761f;
    g.lastBpm = 121.0f;
    const auto bytes = encodeGrid (g);

    REQUIRE (bytes.size() == 2 + 4 + 8 + 8 + 1);
    CHECK (bytes[5] == 2);  // two markers
    CHECK (decodeGrid (bytes) == g);
}

TEST_CASE ("DSS1 cues, loops and grid export to Serato with notes for what doesn't fit", "[serato]")
{
    TrackRecord r;
    r.sampleRate = rate;
    r.lengthFrames = static_cast<Frame> (300 * rate);
    r.grid = BeatGrid::constant (rate, 120.0, static_cast<Frame> (10.125 * rate));
    r.hotCues[0] = HotCue { static_cast<Frame> (31.761 * rate), std::nullopt, "Drop", std::nullopt };
    r.hotCues[2] = HotCue { 441000, Frame { 529200 }, "Loop A", CueColour { 9, 9, 9 } };
    r.addMemoryCue ({ 1000, "", std::nullopt });
    r.savedLoops[4] = SavedLoop { 882000, 970200, "2 bars", std::nullopt, true };

    // No grid unless asked for.
    CHECK_FALSE (exportTrack (r).grid);

    const auto e = exportTrack (r, ExportOptions { true });
    REQUIRE (e.markers.cues.size() == 2);
    CHECK (e.markers.cues[0].positionMs == 31761);
    CHECK (e.markers.cues[0].colour == defaultCueColour (0));
    CHECK (e.markers.cues[1].colour == CueColour { 9, 9, 9 });
    REQUIRE (e.markers.loops.size() == 1);
    CHECK (e.markers.loops[0].index == 4);
    CHECK (e.markers.loops[0].startMs == 20000);
    CHECK (e.markers.loops[0].locked);

    // The grid starts on the first downbeat in the track: 10.125 s is a downbeat, 2 s per bar → 0.125 s.
    REQUIRE (e.grid);
    CHECK (e.grid->markers.empty());
    CHECK (e.grid->lastPositionSeconds == Approx (0.125).margin (1e-4));
    CHECK (e.grid->lastBpm == Approx (120.0));

    REQUIRE (e.notes.size() == 2);  // hot loop, memory cue
}

TEST_CASE ("A variable-tempo grid survives a trip through Serato", "[serato]")
{
    // 124 BPM from 0.5 s, then 126 BPM exactly 64 beats later.
    const auto start = static_cast<Frame> (0.5 * rate);
    const auto change = start + static_cast<Frame> (std::llround (64 * rate * 60.0 / 124.0));
    const BeatGrid grid (rate, { { start, 124.0, 1, 4 }, { change, 126.0, 1, 4 } });

    const auto serato = gridFromBeatGrid (grid);
    REQUIRE (serato);
    REQUIRE (serato->markers.size() == 1);
    CHECK (serato->markers[0].beatsToNext == 64);

    const auto back = beatGridFromGrid (*serato, rate);
    REQUIRE (back);
    REQUIRE (back->anchors().size() == 2);
    CHECK (back->bpmAt (static_cast<double> (start + 1000)) == Approx (124.0).epsilon (1e-4));
    CHECK (back->bpmAt (static_cast<double> (change + 1000)) == Approx (126.0));
    CHECK (std::abs (back->anchors()[1].frame - change) <= 2);
    CHECK (back->anchors()[1].beatInBar == 1);
}

TEST_CASE ("Serato markers import into DSS1 track data", "[serato]")
{
    TrackRecord r;
    r.sampleRate = rate;
    importInto (r, sampleMarkers(), std::nullopt);

    REQUIRE (r.hotCues[0]);
    CHECK (r.hotCues[0]->frame == std::llround (31.761 * rate));
    CHECK (r.hotCues[0]->name == "Drop");
    REQUIRE (r.hotCues[3]);
    CHECK (r.hotCues[3]->colour == CueColour { 1, 2, 3 });
    REQUIRE (r.savedLoops[0]);
    CHECK (r.savedLoops[0]->locked);
    CHECK (r.savedLoops[0]->name == "4 bars");
}

TEST_CASE ("GEOB frames go into a WAV's id3 chunk without touching the audio", "[id3]")
{
    const auto wav = makeWav();
    const id3::GeobFrame markers { "Serato Markers2", wrapMarkersForGeob (encodeMarkers (sampleMarkers())) };

    auto tagged = id3::writeGeobFrames (wav, id3::Container::wav, { markers });
    CHECK (le32At (tagged, 4) == tagged.size() - 8);  // RIFF size updated
    CHECK (contains (tagged, Bytes { 'd', 'a', 't', 'a', 6, 0, 0, 0, 1, 2, 3, 4, 5, 6 }));
    CHECK (contains (tagged, bytesOf ("INFOabc")));

    auto frames = id3::readGeobFrames (tagged, id3::Container::wav);
    REQUIRE (frames.size() == 1);
    CHECK (frames[0].description == "Serato Markers2");
    CHECK (decodeMarkers (unwrapMarkersFromGeob (frames[0].content)) == sampleMarkers());

    // Writing again replaces the frame rather than adding a second one, and adds the grid beside it.
    Grid g;
    g.lastPositionSeconds = 0.5f;
    g.lastBpm = 128.0f;
    tagged = id3::writeGeobFrames (tagged, id3::Container::wav, { markers, { "Serato BeatGrid", encodeGrid (g) } });
    frames = id3::readGeobFrames (tagged, id3::Container::wav);
    REQUIRE (frames.size() == 2);
    CHECK (decodeGrid (frames[1].content) == g);
    CHECK (le32At (tagged, 4) == tagged.size() - 8);
}

TEST_CASE ("GEOB frames go into an AIFF's ID3 chunk", "[id3]")
{
    const auto aiff = makeAiff();
    const id3::GeobFrame frame { "Serato Markers2", wrapMarkersForGeob (encodeMarkers (sampleMarkers())) };
    const auto tagged = id3::writeGeobFrames (aiff, id3::Container::aiff, { frame });

    CHECK (contains (tagged, bytesOf ("ID3 ")));
    CHECK (contains (tagged, Bytes { 0, 0, 0, 0, 0, 0, 0, 0, 9, 9 }));
    const auto frames = id3::readGeobFrames (tagged, id3::Container::aiff);
    REQUIRE (frames.size() == 1);
    CHECK (frames[0] == frame);
}

TEST_CASE ("An MP3's existing ID3 frames are kept when Serato frames are added", "[id3]")
{
    const auto mp3 = makeMp3WithTitle();
    const id3::GeobFrame frame { "Serato Markers2", wrapMarkersForGeob (encodeMarkers (sampleMarkers())) };
    const auto tagged = id3::writeGeobFrames (mp3, id3::Container::mp3, { frame });

    CHECK (tagged[3] == 3);  // stays ID3v2.3
    CHECK (contains (tagged, bytesOf ("My Title")));
    const Bytes audio { 0xFF, 0xFB, 0x90, 0x64, 0x11, 0x22 };
    CHECK (std::equal (audio.rbegin(), audio.rend(), tagged.rbegin()));
    REQUIRE (id3::readGeobFrames (tagged, id3::Container::mp3).size() == 1);

    // An MP3 with no tag at all gets one.
    const Bytes bare { 0xFF, 0xFB, 0x90, 0x64 };
    const auto taggedBare = id3::writeGeobFrames (bare, id3::Container::mp3, { frame });
    CHECK (taggedBare[0] == 'I');
    CHECK (id3::readGeobFrames (taggedBare, id3::Container::mp3).size() == 1);
}

TEST_CASE ("Containers are recognised by extension and checked by content", "[id3]")
{
    CHECK (id3::containerForExtension (".WAV") == id3::Container::wav);
    CHECK (id3::containerForExtension ("aif") == id3::Container::aiff);
    CHECK (id3::containerForExtension ("mp3") == id3::Container::mp3);
    CHECK_FALSE (id3::containerForExtension ("flac"));

    CHECK_THROWS_AS (id3::writeGeobFrames (makeAiff(), id3::Container::wav, {}), std::runtime_error);
    CHECK_THROWS_AS (id3::writeGeobFrames (makeWav(), id3::Container::aiff, {}), std::runtime_error);
}

TEST_CASE ("MP3 positions are shifted by the encoder delay Serato counts", "[serato]")
{
    TrackRecord r;
    r.sampleRate = rate;
    r.exportOffsetFrames = 1105;  // a typical LAME MP3: 576 encoder + 529 decoder delay
    r.hotCues[0] = HotCue { 44100, std::nullopt, "", std::nullopt };
    r.savedLoops[0] = SavedLoop { 88200, 176400, "", std::nullopt, false };

    const auto e = exportTrack (r);
    CHECK (e.markers.cues[0].positionMs == 1025);   // 1 s + 25.06 ms
    CHECK (e.markers.loops[0].startMs == 2025);

    TrackRecord back;
    back.sampleRate = rate;
    back.exportOffsetFrames = 1105;
    importInto (back, e.markers, std::nullopt);
    REQUIRE (back.hotCues[0]);
    CHECK (std::abs (back.hotCues[0]->frame - 44100) <= 23);  // within Serato's 1 ms resolution
    CHECK (std::abs (back.savedLoops[0]->end - 176400) <= 23);
}
