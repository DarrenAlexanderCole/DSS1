#include "core/Id3.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string_view>

namespace dss1::id3
{

namespace
{
    struct Frame
    {
        std::string id;           ///< four characters
        std::uint8_t flags[2] {};
        Bytes body;
    };

    struct Tag
    {
        int major = 4;            ///< 3 or 4
        std::vector<Frame> frames;
    };

    std::uint32_t readSyncsafe (const Bytes& b, std::size_t p)
    {
        return (std::uint32_t (b[p] & 0x7F) << 21) | (std::uint32_t (b[p + 1] & 0x7F) << 14)
             | (std::uint32_t (b[p + 2] & 0x7F) << 7) | std::uint32_t (b[p + 3] & 0x7F);
    }

    std::uint32_t readBE32 (const Bytes& b, std::size_t p)
    {
        return (std::uint32_t (b[p]) << 24) | (std::uint32_t (b[p + 1]) << 16) | (std::uint32_t (b[p + 2]) << 8) | std::uint32_t (b[p + 3]);
    }

    std::uint32_t readLE32 (const Bytes& b, std::size_t p)
    {
        return (std::uint32_t (b[p + 3]) << 24) | (std::uint32_t (b[p + 2]) << 16) | (std::uint32_t (b[p + 1]) << 8) | std::uint32_t (b[p]);
    }

    void putSyncsafe (Bytes& out, std::size_t v)
    {
        if (v >= (1u << 28))
            throw std::runtime_error ("ID3 tag would be too large");
        out.push_back (static_cast<std::uint8_t> ((v >> 21) & 0x7F));
        out.push_back (static_cast<std::uint8_t> ((v >> 14) & 0x7F));
        out.push_back (static_cast<std::uint8_t> ((v >> 7) & 0x7F));
        out.push_back (static_cast<std::uint8_t> (v & 0x7F));
    }

    void putBE32 (Bytes& out, std::size_t v)
    {
        out.push_back (static_cast<std::uint8_t> ((v >> 24) & 0xFF));
        out.push_back (static_cast<std::uint8_t> ((v >> 16) & 0xFF));
        out.push_back (static_cast<std::uint8_t> ((v >> 8) & 0xFF));
        out.push_back (static_cast<std::uint8_t> (v & 0xFF));
    }

    void putLE32 (Bytes& out, std::size_t v)
    {
        out.push_back (static_cast<std::uint8_t> (v & 0xFF));
        out.push_back (static_cast<std::uint8_t> ((v >> 8) & 0xFF));
        out.push_back (static_cast<std::uint8_t> ((v >> 16) & 0xFF));
        out.push_back (static_cast<std::uint8_t> ((v >> 24) & 0xFF));
    }

    bool startsWith (const Bytes& b, std::size_t p, const char* text)
    {
        const std::string_view s (text);
        if (p + s.size() > b.size())
            return false;
        return std::equal (s.begin(), s.end(), b.begin() + static_cast<std::ptrdiff_t> (p),
                           [] (char c, std::uint8_t u) { return static_cast<std::uint8_t> (c) == u; });
    }

    /// Parses an ID3v2.3/2.4 tag. Returns nullopt for tags DSS1 won't edit (other versions, unsynchronised,
    /// extended header) or that are malformed; the caller then starts a fresh tag.
    std::optional<Tag> parseTag (const Bytes& tag)
    {
        if (tag.size() < 10 || ! startsWith (tag, 0, "ID3"))
            return std::nullopt;
        const int major = tag[3];
        const auto flags = tag[5];
        if ((major != 3 && major != 4) || (flags & 0xC0) != 0)
            return std::nullopt;

        Tag t;
        t.major = major;
        const auto end = std::min<std::size_t> (tag.size(), 10 + readSyncsafe (tag, 6));
        std::size_t p = 10;
        while (p + 10 <= end)
        {
            if (tag[p] == 0)
                break;  // padding
            Frame f;
            f.id.assign (tag.begin() + static_cast<std::ptrdiff_t> (p), tag.begin() + static_cast<std::ptrdiff_t> (p + 4));
            const auto size = major == 4 ? readSyncsafe (tag, p + 4) : readBE32 (tag, p + 4);
            f.flags[0] = tag[p + 8];
            f.flags[1] = tag[p + 9];
            const auto next = p + 10 + size;
            if (size == 0 || next > end)
                break;
            f.body.assign (tag.begin() + static_cast<std::ptrdiff_t> (p + 10), tag.begin() + static_cast<std::ptrdiff_t> (next));
            t.frames.push_back (std::move (f));
            p = next;
        }
        return t;
    }

    Bytes buildTag (const Tag& t)
    {
        Bytes frames;
        for (const auto& f : t.frames)
        {
            frames.insert (frames.end(), f.id.begin(), f.id.end());
            if (t.major == 4)
                putSyncsafe (frames, f.body.size());
            else
                putBE32 (frames, f.body.size());
            frames.push_back (f.flags[0]);
            frames.push_back (f.flags[1]);
            frames.insert (frames.end(), f.body.begin(), f.body.end());
        }

        Bytes out { 'I', 'D', '3', static_cast<std::uint8_t> (t.major), 0x00, 0x00 };
        putSyncsafe (out, frames.size());
        out.insert (out.end(), frames.begin(), frames.end());
        return out;
    }

    /// A GEOB body in Latin-1 or UTF-8 (encodings 0 and 3, which is what Serato writes):
    /// encoding, MIME type\0, file name\0, description\0, content.
    std::optional<GeobFrame> parseGeob (const Bytes& body)
    {
        if (body.empty() || (body[0] != 0 && body[0] != 3))
            return std::nullopt;
        std::size_t p = 1;
        std::string parts[3];
        for (auto& part : parts)
        {
            const auto zero = std::find (body.begin() + static_cast<std::ptrdiff_t> (p), body.end(), std::uint8_t { 0 });
            if (zero == body.end())
                return std::nullopt;
            part.assign (body.begin() + static_cast<std::ptrdiff_t> (p), zero);
            p = static_cast<std::size_t> (zero - body.begin()) + 1;
        }
        GeobFrame g;
        g.mimeType = parts[0];
        g.description = parts[2];
        g.content.assign (body.begin() + static_cast<std::ptrdiff_t> (p), body.end());
        return g;
    }

    Bytes geobBody (const GeobFrame& g)
    {
        Bytes b { 0x00 };
        b.insert (b.end(), g.mimeType.begin(), g.mimeType.end());
        b.push_back (0);
        b.push_back (0);  // no file name
        b.insert (b.end(), g.description.begin(), g.description.end());
        b.push_back (0);
        b.insert (b.end(), g.content.begin(), g.content.end());
        return b;
    }

    Bytes retag (const std::optional<Bytes>& existing, const std::vector<GeobFrame>& frames, const std::vector<std::string>& remove)
    {
        Tag t;
        if (existing)
            if (auto parsed = parseTag (*existing))
                t = std::move (*parsed);

        // Drop GEOB frames we're replacing or were asked to remove; keep everything else.
        t.frames.erase (std::remove_if (t.frames.begin(), t.frames.end(), [&] (const Frame& f) {
            if (f.id != "GEOB")
                return false;
            const auto g = parseGeob (f.body);
            if (! g)
                return false;
            return std::any_of (frames.begin(), frames.end(), [&] (const GeobFrame& n) { return n.description == g->description; })
                || std::find (remove.begin(), remove.end(), g->description) != remove.end();
        }), t.frames.end());

        for (const auto& g : frames)
            t.frames.push_back ({ "GEOB", { 0, 0 }, geobBody (g) });
        return buildTag (t);
    }

    // --- Chunked files (WAV is RIFF, little-endian; AIFF is FORM, big-endian) ----------------------------

    struct Chunk
    {
        std::string id;
        Bytes body;
    };

    struct ChunkFile
    {
        Bytes header;  ///< the first 12 bytes: RIFF/FORM, size, WAVE/AIFF/AIFC
        std::vector<Chunk> chunks;
    };

    ChunkFile readChunks (const Bytes& file, bool bigEndian)
    {
        const bool ok = file.size() >= 12
                     && (bigEndian ? startsWith (file, 0, "FORM") && (startsWith (file, 8, "AIFF") || startsWith (file, 8, "AIFC"))
                                   : startsWith (file, 0, "RIFF") && startsWith (file, 8, "WAVE"));
        if (! ok)
            throw std::runtime_error (bigEndian ? "Not an AIFF file" : "Not a WAV file");

        ChunkFile cf;
        cf.header.assign (file.begin(), file.begin() + 12);
        std::size_t p = 12;
        while (p + 8 <= file.size())
        {
            Chunk c;
            c.id.assign (file.begin() + static_cast<std::ptrdiff_t> (p), file.begin() + static_cast<std::ptrdiff_t> (p + 4));
            const std::size_t length = bigEndian ? readBE32 (file, p + 4) : readLE32 (file, p + 4);
            const auto end = std::min (file.size(), p + 8 + length);
            c.body.assign (file.begin() + static_cast<std::ptrdiff_t> (p + 8), file.begin() + static_cast<std::ptrdiff_t> (end));
            cf.chunks.push_back (std::move (c));
            p += 8 + length + (length & 1);
        }
        return cf;
    }

    Bytes writeChunks (const ChunkFile& cf, bool bigEndian)
    {
        Bytes out = cf.header;
        for (const auto& c : cf.chunks)
        {
            out.insert (out.end(), c.id.begin(), c.id.end());
            if (bigEndian)
                putBE32 (out, c.body.size());
            else
                putLE32 (out, c.body.size());
            out.insert (out.end(), c.body.begin(), c.body.end());
            if (c.body.size() & 1)
                out.push_back (0);
        }
        Bytes size;
        if (bigEndian)
            putBE32 (size, out.size() - 8);
        else
            putLE32 (size, out.size() - 8);
        std::copy (size.begin(), size.end(), out.begin() + 4);
        return out;
    }

    bool isId3Chunk (const std::string& id)
    {
        return id == "id3 " || id == "ID3 ";
    }

    /// Where the audio starts in an MP3: after the ID3v2 tag (and its footer), if there is one.
    std::size_t mp3TagLength (const Bytes& file)
    {
        if (file.size() < 10 || ! startsWith (file, 0, "ID3"))
            return 0;
        const bool footer = (file[5] & 0x10) != 0;
        return std::min<std::size_t> (file.size(), 10 + readSyncsafe (file, 6) + (footer ? 10 : 0));
    }

    std::optional<Bytes> existingTag (const Bytes& file, Container container)
    {
        if (container == Container::mp3)
        {
            const auto length = mp3TagLength (file);
            if (length == 0)
                return std::nullopt;
            return Bytes (file.begin(), file.begin() + static_cast<std::ptrdiff_t> (length));
        }
        const auto cf = readChunks (file, container == Container::aiff);
        for (const auto& c : cf.chunks)
            if (isId3Chunk (c.id))
                return c.body;
        return std::nullopt;
    }
} // namespace

std::optional<Container> containerForExtension (std::string extension)
{
    if (! extension.empty() && extension.front() == '.')
        extension.erase (0, 1);
    std::transform (extension.begin(), extension.end(), extension.begin(),
                    [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    if (extension == "wav" || extension == "wave")
        return Container::wav;
    if (extension == "aif" || extension == "aiff")
        return Container::aiff;
    if (extension == "mp3")
        return Container::mp3;
    return std::nullopt;
}

Bytes writeGeobFrames (const Bytes& file, Container container, const std::vector<GeobFrame>& frames,
                       const std::vector<std::string>& remove)
{
    if (container == Container::mp3)
    {
        const auto length = mp3TagLength (file);
        std::optional<Bytes> existing;
        if (length > 0)
            existing = Bytes (file.begin(), file.begin() + static_cast<std::ptrdiff_t> (length));
        auto out = retag (existing, frames, remove);
        out.insert (out.end(), file.begin() + static_cast<std::ptrdiff_t> (length), file.end());
        return out;
    }

    const bool bigEndian = container == Container::aiff;
    auto cf = readChunks (file, bigEndian);
    std::optional<Bytes> existing;
    for (const auto& c : cf.chunks)
        if (isId3Chunk (c.id))
            existing = c.body;
    cf.chunks.erase (std::remove_if (cf.chunks.begin(), cf.chunks.end(), [] (const Chunk& c) { return isId3Chunk (c.id); }),
                     cf.chunks.end());
    cf.chunks.push_back ({ bigEndian ? "ID3 " : "id3 ", retag (existing, frames, remove) });
    return writeChunks (cf, bigEndian);
}

std::vector<GeobFrame> readGeobFrames (const Bytes& file, Container container)
{
    std::vector<GeobFrame> out;
    const auto tag = existingTag (file, container);
    if (! tag)
        return out;
    const auto parsed = parseTag (*tag);
    if (! parsed)
        return out;
    for (const auto& f : parsed->frames)
        if (f.id == "GEOB")
            if (auto g = parseGeob (f.body))
                out.push_back (std::move (*g));
    return out;
}

} // namespace dss1::id3
