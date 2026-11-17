#include "core/Tags.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace dss1::tags
{

void TextTags::fillFrom (const TextTags& other)
{
    if (title.empty())
        title = other.title;
    if (artist.empty())
        artist = other.artist;
    if (album.empty())
        album = other.album;
    if (genre.empty())
        genre = other.genre;
}

namespace
{
    void appendUtf8 (std::string& out, std::uint32_t cp)
    {
        if (cp < 0x80)
            out += static_cast<char> (cp);
        else if (cp < 0x800)
        {
            out += static_cast<char> (0xC0 | (cp >> 6));
            out += static_cast<char> (0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char> (0xE0 | (cp >> 12));
            out += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char> (0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char> (0xF0 | (cp >> 18));
            out += static_cast<char> (0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char> (0x80 | (cp & 0x3F));
        }
    }

    std::string latin1 (const std::uint8_t* p, std::size_t n)
    {
        std::string out;
        for (std::size_t i = 0; i < n; ++i)
            appendUtf8 (out, p[i]);
        return out;
    }

    bool isValidUtf8 (const std::uint8_t* p, std::size_t n)
    {
        for (std::size_t i = 0; i < n;)
        {
            const auto c = p[i];
            const std::size_t extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : 99;
            if (extra == 99 || i + extra >= n)
                return false;
            for (std::size_t k = 1; k <= extra; ++k)
                if ((p[i + k] & 0xC0) != 0x80)
                    return false;
            i += extra + 1;
        }
        return true;
    }

    /// Text in an unknown 8-bit encoding: UTF-8 if it is valid UTF-8, otherwise Latin-1.
    std::string guess8Bit (const std::uint8_t* p, std::size_t n)
    {
        return isValidUtf8 (p, n) ? std::string (reinterpret_cast<const char*> (p), n) : latin1 (p, n);
    }

    std::string utf16 (const std::uint8_t* p, std::size_t n, bool bigEndian)
    {
        std::string out;
        for (std::size_t i = 0; i + 1 < n; i += 2)
        {
            std::uint32_t u = bigEndian ? (std::uint32_t (p[i]) << 8 | p[i + 1]) : (std::uint32_t (p[i + 1]) << 8 | p[i]);
            if (u >= 0xD800 && u < 0xDC00 && i + 3 < n)
            {
                const std::uint32_t lo = bigEndian ? (std::uint32_t (p[i + 2]) << 8 | p[i + 3]) : (std::uint32_t (p[i + 3]) << 8 | p[i + 2]);
                if (lo >= 0xDC00 && lo < 0xE000)
                {
                    u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                    i += 2;
                }
            }
            appendUtf8 (out, u);
        }
        return out;
    }

    std::string trim (std::string s)
    {
        const auto notSpace = [] (unsigned char c) { return ! std::isspace (c) && c != 0; };
        s.erase (s.begin(), std::find_if (s.begin(), s.end(), notSpace));
        s.erase (std::find_if (s.rbegin(), s.rend(), notSpace).base(), s.end());
        return s;
    }

    /// Splits ID3 text at its terminators (ID3v2.4 allows several values in one frame) and decodes each.
    std::vector<std::string> decodeId3Text (const std::uint8_t* p, std::size_t n)
    {
        std::vector<std::string> values;
        if (n == 0)
            return values;
        const auto encoding = p[0];
        ++p;
        --n;

        if (encoding == 1 || encoding == 2)
        {
            std::size_t start = 0;
            for (std::size_t i = 0; i <= n; i += 2)
            {
                const bool end = i + 1 >= n || (p[i] == 0 && p[i + 1] == 0);
                if (! end)
                    continue;
                const auto* q = p + start;
                auto len = std::min (i, n) - start;
                bool big = encoding == 2;
                if (encoding == 1 && len >= 2)
                {
                    if (q[0] == 0xFE && q[1] == 0xFF) { big = true; q += 2; len -= 2; }
                    else if (q[0] == 0xFF && q[1] == 0xFE) { big = false; q += 2; len -= 2; }
                }
                values.push_back (trim (utf16 (q, len, big)));
                start = i + 2;
            }
        }
        else
        {
            std::size_t start = 0;
            for (std::size_t i = 0; i <= n; ++i)
            {
                if (i < n && p[i] != 0)
                    continue;
                values.push_back (trim (encoding == 3 ? std::string (reinterpret_cast<const char*> (p + start), i - start)
                                                      : latin1 (p + start, i - start)));
                start = i + 1;
            }
        }
        values.erase (std::remove (values.begin(), values.end(), std::string()), values.end());
        return values;
    }

    std::string join (const std::vector<std::string>& values)
    {
        std::string out;
        for (const auto& v : values)
            out += (out.empty() ? "" : ", ") + v;
        return out;
    }

    /// "(17)", "(17)Rock", "17" or "Rock" → "Rock".
    std::string resolveGenre (const std::string& raw)
    {
        auto s = raw;
        if (s.size() > 2 && s[0] == '(' && std::isdigit (static_cast<unsigned char> (s[1])))
        {
            const auto close = s.find (')');
            if (close != std::string::npos)
            {
                const auto rest = trim (s.substr (close + 1));
                if (! rest.empty())
                    return rest;
                s = s.substr (1, close - 1);
            }
        }
        if (! s.empty() && std::all_of (s.begin(), s.end(), [] (unsigned char c) { return std::isdigit (c); }) && s.size() < 4)
        {
            const auto name = id3v1Genre (std::stoi (s));
            return name.empty() ? raw : name;
        }
        return s;
    }

    std::uint32_t syncsafe (const std::uint8_t* p)
    {
        return (std::uint32_t (p[0] & 0x7F) << 21) | (std::uint32_t (p[1] & 0x7F) << 14) | (std::uint32_t (p[2] & 0x7F) << 7) | (p[3] & 0x7F);
    }

    std::uint32_t be32 (const std::uint8_t* p) { return std::uint32_t (p[0]) << 24 | std::uint32_t (p[1]) << 16 | std::uint32_t (p[2]) << 8 | p[3]; }
    std::uint32_t le32 (const std::uint8_t* p) { return std::uint32_t (p[3]) << 24 | std::uint32_t (p[2]) << 16 | std::uint32_t (p[1]) << 8 | p[0]; }

    /// Undoes ID3 unsynchronisation: every FF 00 becomes FF.
    Bytes resync (const std::uint8_t* p, std::size_t n)
    {
        Bytes out;
        out.reserve (n);
        for (std::size_t i = 0; i < n; ++i)
        {
            out.push_back (p[i]);
            if (p[i] == 0xFF && i + 1 < n && p[i + 1] == 0x00)
                ++i;
        }
        return out;
    }

    void assignFrame (TextTags& t, std::string_view id, const std::vector<std::string>& values)
    {
        if (values.empty())
            return;
        if ((id == "TIT2" || id == "TT2") && t.title.empty())
            t.title = values.front();
        else if ((id == "TPE1" || id == "TP1") && t.artist.empty())
            t.artist = join (values);
        else if ((id == "TALB" || id == "TAL") && t.album.empty())
            t.album = values.front();
        else if ((id == "TCON" || id == "TCO") && t.genre.empty())
        {
            std::vector<std::string> genres;
            for (const auto& v : values)
                genres.push_back (resolveGenre (v));
            t.genre = join (genres);
        }
    }
} // namespace

TextTags parseId3v2 (const Bytes& tag)
{
    TextTags t;
    if (tag.size() < 10 || tag[0] != 'I' || tag[1] != 'D' || tag[2] != '3')
        return t;
    const int major = tag[3];
    const auto flags = tag[5];
    if (major < 2 || major > 4)
        return t;
    if (major == 2 && (flags & 0x40))
        return t;  // compressed ID3v2.2 tag: no defined scheme

    const auto declared = std::min<std::size_t> (tag.size() - 10, syncsafe (&tag[6]));
    Bytes body ((flags & 0x80) && major < 4 ? resync (&tag[10], declared) : Bytes (tag.begin() + 10, tag.begin() + 10 + static_cast<std::ptrdiff_t> (declared)));

    std::size_t p = 0;
    if ((flags & 0x40) && major >= 3 && body.size() >= 4)
    {
        // Extended header: v2.3 size excludes its own 4 bytes, v2.4 includes them and is syncsafe.
        p = major == 3 ? 4 + be32 (&body[0]) : syncsafe (&body[0]);
    }

    const std::size_t idLen = major == 2 ? 3 : 4;
    const std::size_t headerLen = major == 2 ? 6 : 10;
    while (p + headerLen <= body.size() && body[p] != 0)
    {
        const std::string id (body.begin() + static_cast<std::ptrdiff_t> (p), body.begin() + static_cast<std::ptrdiff_t> (p + idLen));
        std::size_t size = 0;
        std::uint8_t formatFlags = 0;
        if (major == 2)
            size = std::size_t (body[p + 3]) << 16 | std::size_t (body[p + 4]) << 8 | body[p + 5];
        else
        {
            size = major == 4 ? syncsafe (&body[p + 4]) : be32 (&body[p + 4]);
            formatFlags = body[p + 9];
        }
        const auto start = p + headerLen;
        if (size == 0 || start + size > body.size())
            break;
        p = start + size;

        if (id[0] != 'T')
            continue;
        // Compressed or encrypted frames are skipped (v2.3: 0x80/0x40, v2.4: 0x08/0x04).
        if ((major == 3 && (formatFlags & 0xC0)) || (major == 4 && (formatFlags & 0x0C)))
            continue;
        const std::uint8_t* data = &body[start];
        std::size_t n = size;
        Bytes resynced;
        if (major == 4 && (formatFlags & 0x01) && n >= 4)
        {
            data += 4;  // data length indicator
            n -= 4;
        }
        if (major == 4 && (formatFlags & 0x02))
        {
            resynced = resync (data, n);
            data = resynced.data();
            n = resynced.size();
        }
        assignFrame (t, id, decodeId3Text (data, n));
    }
    return t;
}

TextTags parseId3v1 (const Bytes& tag)
{
    TextTags t;
    if (tag.size() < 128 || tag[0] != 'T' || tag[1] != 'A' || tag[2] != 'G')
        return t;
    const auto field = [&] (std::size_t offset, std::size_t length) {
        auto end = offset;
        while (end < offset + length && tag[end] != 0)
            ++end;
        return trim (guess8Bit (&tag[offset], end - offset));
    };
    t.title = field (3, 30);
    t.artist = field (33, 30);
    t.album = field (63, 30);
    t.genre = id3v1Genre (tag[127]);
    return t;
}

TextTags parseVorbisComment (const Bytes& block)
{
    TextTags t;
    if (block.size() < 8)
        return t;
    std::size_t p = 4 + std::size_t (le32 (&block[0]));
    if (p + 4 > block.size())
        return t;
    const auto count = le32 (&block[p]);
    p += 4;
    for (std::uint32_t i = 0; i < count && p + 4 <= block.size(); ++i)
    {
        const std::size_t len = le32 (&block[p]);
        p += 4;
        if (p + len > block.size())
            break;
        const std::string comment (block.begin() + static_cast<std::ptrdiff_t> (p), block.begin() + static_cast<std::ptrdiff_t> (p + len));
        p += len;
        const auto eq = comment.find ('=');
        if (eq == std::string::npos)
            continue;
        std::string key = comment.substr (0, eq);
        std::transform (key.begin(), key.end(), key.begin(), [] (unsigned char c) { return static_cast<char> (std::toupper (c)); });
        const auto value = trim (comment.substr (eq + 1));
        if (value.empty())
            continue;
        auto append = [&] (std::string& field) { field += (field.empty() ? "" : ", ") + value; };
        if (key == "TITLE" && t.title.empty())
            t.title = value;
        else if (key == "ARTIST")
            append (t.artist);
        else if (key == "ALBUM" && t.album.empty())
            t.album = value;
        else if (key == "GENRE")
            append (t.genre);
    }
    return t;
}

TextTags parseRiffInfo (const Bytes& listBody)
{
    TextTags t;
    if (listBody.size() < 4 || std::string_view (reinterpret_cast<const char*> (listBody.data()), 4) != "INFO")
        return t;
    std::size_t p = 4;
    while (p + 8 <= listBody.size())
    {
        const std::string_view id (reinterpret_cast<const char*> (&listBody[p]), 4);
        const std::size_t len = le32 (&listBody[p + 4]);
        const auto start = p + 8;
        if (start + len > listBody.size())
            break;
        auto end = start;
        while (end < start + len && listBody[end] != 0)
            ++end;
        const auto value = trim (guess8Bit (&listBody[start], end - start));
        if (id == "INAM")
            t.title = value;
        else if (id == "IART")
            t.artist = value;
        else if (id == "IPRD")
            t.album = value;
        else if (id == "IGNR")
            t.genre = value;
        p = start + len + (len & 1);
    }
    return t;
}

std::string id3v1Genre (int number)
{
    static constexpr std::array<const char*, 126> names {
        "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge", "Hip-Hop", "Jazz", "Metal",
        "New Age", "Oldies", "Other", "Pop", "R&B", "Rap", "Reggae", "Rock", "Techno", "Industrial",
        "Alternative", "Ska", "Death Metal", "Pranks", "Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop", "Vocal", "Jazz+Funk",
        "Fusion", "Trance", "Classical", "Instrumental", "Acid", "House", "Game", "Sound Clip", "Gospel", "Noise",
        "AlternRock", "Bass", "Soul", "Punk", "Space", "Meditative", "Instrumental Pop", "Instrumental Rock", "Ethnic", "Gothic",
        "Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk", "Eurodance", "Dream", "Southern Rock", "Comedy", "Cult", "Gangsta",
        "Top 40", "Christian Rap", "Pop/Funk", "Jungle", "Native American", "Cabaret", "New Wave", "Psychedelic", "Rave", "Showtunes",
        "Trailer", "Lo-Fi", "Tribal", "Acid Punk", "Acid Jazz", "Polka", "Retro", "Musical", "Rock & Roll", "Hard Rock",
        "Folk", "Folk-Rock", "National Folk", "Swing", "Fast Fusion", "Bebop", "Latin", "Revival", "Celtic", "Bluegrass",
        "Avantgarde", "Gothic Rock", "Progressive Rock", "Psychedelic Rock", "Symphonic Rock", "Slow Rock", "Big Band", "Chorus", "Easy Listening", "Acoustic",
        "Humour", "Speech", "Chanson", "Opera", "Chamber Music", "Sonata", "Symphony", "Booty Bass", "Primus", "Porn Groove",
        "Satire", "Slow Jam", "Club", "Tango", "Samba", "Folklore", "Ballad", "Power Ballad", "Rhythmic Soul", "Freestyle",
        "Duet", "Punk Rock", "Drum Solo", "A capella", "Euro-House", "Dance Hall"
    };
    if (number < 0 || number >= static_cast<int> (names.size()))
        return {};
    return names[static_cast<std::size_t> (number)];
}

} // namespace dss1::tags
