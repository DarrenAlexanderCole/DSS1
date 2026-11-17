#include "core/Tags.h"
#include "io/TagReader.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace dss1;
using tags::Bytes;

namespace
{
    void put (Bytes& b, const std::string& s) { b.insert (b.end(), s.begin(), s.end()); }

    void putBE32 (Bytes& b, std::size_t v)
    {
        for (int shift = 24; shift >= 0; shift -= 8)
            b.push_back (static_cast<std::uint8_t> ((v >> shift) & 0xFF));
    }

    void putLE32 (Bytes& b, std::size_t v)
    {
        for (int shift = 0; shift <= 24; shift += 8)
            b.push_back (static_cast<std::uint8_t> ((v >> shift) & 0xFF));
    }

    void putSyncsafe (Bytes& b, std::size_t v)
    {
        for (int shift = 21; shift >= 0; shift -= 7)
            b.push_back (static_cast<std::uint8_t> ((v >> shift) & 0x7F));
    }

    /// An ID3v2.3 or 2.4 tag from (id, body) frames.
    Bytes id3Tag (int major, const std::vector<std::pair<std::string, Bytes>>& frames)
    {
        Bytes body;
        for (const auto& [id, data] : frames)
        {
            put (body, id);
            if (major == 4)
                putSyncsafe (body, data.size());
            else
                putBE32 (body, data.size());
            body.push_back (0);
            body.push_back (0);
            body.insert (body.end(), data.begin(), data.end());
        }
        Bytes tag { 'I', 'D', '3', static_cast<std::uint8_t> (major), 0, 0 };
        putSyncsafe (tag, body.size());
        tag.insert (tag.end(), body.begin(), body.end());
        return tag;
    }

    Bytes latin1Text (const std::string& s)
    {
        Bytes b { 0 };
        put (b, s);
        return b;
    }

    Bytes utf8Text (const std::string& s)
    {
        Bytes b { 3 };
        put (b, s);
        return b;
    }

    /// UTF-16 with a little-endian BOM, for plain ASCII input plus any extra code units given.
    Bytes utf16Text (const std::string& ascii, std::vector<std::uint16_t> extra = {})
    {
        Bytes b { 1, 0xFF, 0xFE };
        std::vector<std::uint16_t> units (ascii.begin(), ascii.end());
        units.insert (units.end(), extra.begin(), extra.end());
        for (auto u : units)
        {
            b.push_back (static_cast<std::uint8_t> (u & 0xFF));
            b.push_back (static_cast<std::uint8_t> (u >> 8));
        }
        b.push_back (0);
        b.push_back (0);
        return b;
    }

    Bytes readFile (const std::filesystem::path& p)
    {
        std::ifstream in (p, std::ios::binary);
        return Bytes (std::istreambuf_iterator<char> (in), {});
    }

    void writeFile (const std::filesystem::path& p, const Bytes& b)
    {
        std::ofstream out (p, std::ios::binary);
        out.write (reinterpret_cast<const char*> (b.data()), static_cast<std::streamsize> (b.size()));
    }

    std::filesystem::path fixture (const char* name) { return std::filesystem::path (DSS1_TEST_FIXTURES) / name; }

    std::filesystem::path tempFile (const char* name) { return std::filesystem::temp_directory_path() / name; }
} // namespace

TEST_CASE ("ID3v2.3 text frames in Latin-1 and UTF-16", "[tags]")
{
    const auto tag = id3Tag (3, { { "TIT2", utf16Text ("Caf", { 0x00E9 }) },
                                  { "TPE1", latin1Text ("Bj\xF6rk") },
                                  { "TALB", latin1Text ("Debut") },
                                  { "TCON", latin1Text ("(17)") } });
    const auto t = tags::parseId3v2 (tag);
    CHECK (t.title == "Caf\xC3\xA9");
    CHECK (t.artist == "Bj\xC3\xB6rk");
    CHECK (t.album == "Debut");
    CHECK (t.genre == "Rock");
}

TEST_CASE ("ID3v2.4 joins several artists and resolves numeric genres", "[tags]")
{
    Bytes artists { 3 };
    put (artists, std::string ("Artist One\0Artist Two", 21));
    const auto tag = id3Tag (4, { { "TPE1", artists }, { "TIT2", utf8Text ("Song") }, { "TCON", utf8Text ("35") } });
    const auto t = tags::parseId3v2 (tag);
    CHECK (t.artist == "Artist One, Artist Two");
    CHECK (t.title == "Song");
    CHECK (t.genre == "House");
}

TEST_CASE ("ID3v2.2 uses three-letter frame ids", "[tags]")
{
    Bytes body;
    const auto frame = [&] (const std::string& id, const Bytes& data) {
        put (body, id);
        body.push_back (0);
        body.push_back (static_cast<std::uint8_t> (data.size() >> 8));
        body.push_back (static_cast<std::uint8_t> (data.size() & 0xFF));
        body.insert (body.end(), data.begin(), data.end());
    };
    frame ("TT2", latin1Text ("Old Tag"));
    frame ("TP1", latin1Text ("Someone"));
    Bytes tag { 'I', 'D', '3', 2, 0, 0 };
    putSyncsafe (tag, body.size());
    tag.insert (tag.end(), body.begin(), body.end());

    const auto t = tags::parseId3v2 (tag);
    CHECK (t.title == "Old Tag");
    CHECK (t.artist == "Someone");
}

TEST_CASE ("Broken ID3 tags give what they can and never throw", "[tags]")
{
    auto tag = id3Tag (3, { { "TIT2", latin1Text ("Fine") }, { "TPE1", latin1Text ("Cut off here") } });
    tag.resize (tag.size() - 5);
    const auto t = tags::parseId3v2 (tag);
    CHECK (t.title == "Fine");
    CHECK (tags::parseId3v2 (Bytes { 'I', 'D' }).empty());
    CHECK (tags::parseId3v2 (Bytes (40, 0xFF)).empty());
}

TEST_CASE ("ID3v1", "[tags]")
{
    Bytes tag (128, 0);
    const auto set = [&] (std::size_t at, const std::string& s) { std::copy (s.begin(), s.end(), tag.begin() + static_cast<std::ptrdiff_t> (at)); };
    set (0, "TAG");
    set (3, "Title v1");
    set (33, "Artist v1");
    set (63, "Album v1");
    tag[127] = 7;
    const auto t = tags::parseId3v1 (tag);
    CHECK (t == tags::TextTags { "Title v1", "Artist v1", "Album v1", "Hip-Hop" });
}

TEST_CASE ("FLAC Vorbis comments", "[tags]")
{
    Bytes block;
    const std::string vendor = "Lavf";
    putLE32 (block, vendor.size());
    put (block, vendor);
    const std::vector<std::string> comments { "title=Night Drive", "ARTIST=A", "Artist=B", "GENRE=Techno", "ALBUM=", "junk" };
    putLE32 (block, comments.size());
    for (const auto& c : comments)
    {
        putLE32 (block, c.size());
        put (block, c);
    }
    const auto t = tags::parseVorbisComment (block);
    CHECK (t.title == "Night Drive");
    CHECK (t.artist == "A, B");
    CHECK (t.genre == "Techno");
    CHECK (t.album.empty());
}

TEST_CASE ("WAV LIST/INFO", "[tags]")
{
    Bytes list;
    put (list, "INFO");
    const auto sub = [&] (const std::string& id, const std::string& value) {
        put (list, id);
        putLE32 (list, value.size() + 1);
        put (list, value);
        list.push_back (0);
        if ((value.size() + 1) & 1)
            list.push_back (0);
    };
    sub ("INAM", "Wav Title");
    sub ("IART", "Wav Artist");
    sub ("IGNR", "Garage");
    const auto t = tags::parseRiffInfo (list);
    CHECK (t.title == "Wav Title");
    CHECK (t.artist == "Wav Artist");
    CHECK (t.genre == "Garage");
}

TEST_CASE ("readTags finds tags in real files without reading the audio", "[tags][io]")
{
    SECTION ("MP3 with an ID3v2 tag in front")
    {
        auto file = readFile (fixture ("clicks.mp3"));
        REQUIRE (file.size() > 10);
        const std::size_t oldTag = 10 + ((std::size_t (file[6]) << 21) | (std::size_t (file[7]) << 14) | (std::size_t (file[8]) << 7) | file[9]);
        auto tagged = id3Tag (3, { { "TIT2", latin1Text ("Clicks") }, { "TPE1", latin1Text ("DSS1") } });
        tagged.insert (tagged.end(), file.begin() + static_cast<std::ptrdiff_t> (oldTag), file.end());
        const auto path = tempFile ("dss1-tags.mp3");
        writeFile (path, tagged);
        const auto t = io::readTags (path.string());
        CHECK (t.title == "Clicks");
        CHECK (t.artist == "DSS1");
        std::filesystem::remove (path);
    }

    SECTION ("WAV with a LIST/INFO chunk at the end")
    {
        auto file = readFile (fixture ("clicks.wav"));
        Bytes list;
        put (list, "INFO");
        put (list, "INAM");
        putLE32 (list, 6);
        put (list, std::string ("Click\0", 6));
        put (file, "LIST");
        putLE32 (file, list.size());
        file.insert (file.end(), list.begin(), list.end());
        Bytes size;
        putLE32 (size, file.size() - 8);
        std::copy (size.begin(), size.end(), file.begin() + 4);
        const auto path = tempFile ("dss1-tags.wav");
        writeFile (path, file);
        CHECK (io::readTags (path.string()).title == "Click");
        std::filesystem::remove (path);
    }

    SECTION ("Untagged files and missing files")
    {
        CHECK (io::readTags (fixture ("clicks.flac").string()).title.empty());
        CHECK (io::readTags (fixture ("clicks.aiff").string()).empty());
        CHECK (io::readTags ("/no/such/file.mp3").empty());
    }
}
