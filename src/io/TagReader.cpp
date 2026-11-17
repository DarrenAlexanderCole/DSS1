#include "io/TagReader.h"

#include "io/AudioDecoder.h"

#include <array>
#include <fstream>

namespace dss1::io
{

namespace
{
    using tags::Bytes;

    constexpr std::size_t maxTagBytes = 64u << 20;  // ignore absurd sizes from broken files

    bool readAt (std::ifstream& in, std::uint64_t offset, std::size_t length, Bytes& out)
    {
        if (length > maxTagBytes)
            return false;
        out.resize (length);
        in.clear();
        in.seekg (static_cast<std::streamoff> (offset));
        in.read (reinterpret_cast<char*> (out.data()), static_cast<std::streamsize> (length));
        return in.gcount() == static_cast<std::streamsize> (length);
    }

    std::uint32_t syncsafe (const std::uint8_t* p)
    {
        return (std::uint32_t (p[0] & 0x7F) << 21) | (std::uint32_t (p[1] & 0x7F) << 14) | (std::uint32_t (p[2] & 0x7F) << 7) | (p[3] & 0x7F);
    }

    /// The ID3v2 tag at `offset`, if there is one. `length` gets the tag's full size (header included).
    bool readId3v2At (std::ifstream& in, std::uint64_t offset, Bytes& tag, std::size_t& length)
    {
        Bytes header;
        if (! readAt (in, offset, 10, header) || header[0] != 'I' || header[1] != 'D' || header[2] != '3')
            return false;
        length = 10 + syncsafe (&header[6]) + ((header[5] & 0x10) ? 10 : 0);
        return readAt (in, offset, length, tag);
    }

    std::uint64_t fileSize (std::ifstream& in)
    {
        in.clear();
        in.seekg (0, std::ios::end);
        return static_cast<std::uint64_t> (in.tellg());
    }

    tags::TextTags readMp3 (std::ifstream& in)
    {
        tags::TextTags t;
        Bytes tag;
        std::size_t length = 0;
        if (readId3v2At (in, 0, tag, length))
            t = tags::parseId3v2 (tag);
        const auto size = fileSize (in);
        if (size >= 128 && readAt (in, size - 128, 128, tag))
            t.fillFrom (tags::parseId3v1 (tag));
        return t;
    }

    tags::TextTags readChunked (std::ifstream& in, bool bigEndian)
    {
        tags::TextTags id3, info;
        const auto size = fileSize (in);
        std::uint64_t p = 12;
        Bytes header, body;
        while (p + 8 <= size && readAt (in, p, 8, header))
        {
            const std::string id (header.begin(), header.begin() + 4);
            const std::uint64_t length = bigEndian
                ? (std::uint32_t (header[4]) << 24 | std::uint32_t (header[5]) << 16 | std::uint32_t (header[6]) << 8 | header[7])
                : (std::uint32_t (header[7]) << 24 | std::uint32_t (header[6]) << 16 | std::uint32_t (header[5]) << 8 | header[4]);
            if ((id == "id3 " || id == "ID3 ") && readAt (in, p + 8, static_cast<std::size_t> (length), body))
                id3 = tags::parseId3v2 (body);
            else if (! bigEndian && id == "LIST" && length >= 4 && readAt (in, p + 8, static_cast<std::size_t> (length), body))
                info.fillFrom (tags::parseRiffInfo (body));
            p += 8 + length + (length & 1);
        }
        id3.fillFrom (info);
        return id3;
    }

    tags::TextTags readFlac (std::ifstream& in)
    {
        tags::TextTags t;
        std::uint64_t p = 0;
        Bytes bytes;
        std::size_t id3Length = 0;
        if (readId3v2At (in, 0, bytes, id3Length))
        {
            t = tags::parseId3v2 (bytes);
            p = id3Length;
        }
        if (! readAt (in, p, 4, bytes) || bytes != Bytes { 'f', 'L', 'a', 'C' })
            return t;
        p += 4;
        for (bool last = false; ! last && readAt (in, p, 4, bytes);)
        {
            last = (bytes[0] & 0x80) != 0;
            const auto type = bytes[0] & 0x7F;
            const std::size_t length = std::size_t (bytes[1]) << 16 | std::size_t (bytes[2]) << 8 | bytes[3];
            if (type == 4)
            {
                Bytes block;
                if (readAt (in, p + 4, length, block))
                {
                    auto vorbis = tags::parseVorbisComment (block);
                    vorbis.fillFrom (t);
                    return vorbis;
                }
            }
            p += 4 + length;
        }
        return t;
    }
} // namespace

tags::TextTags readTags (const std::string& path)
{
    const auto dot = path.find_last_of ('.');
    const auto format = formatForExtension (dot == std::string::npos ? std::string() : path.substr (dot));
    std::ifstream in (path, std::ios::binary);
    if (! format || ! in)
        return {};
    switch (*format)
    {
        case AudioFormat::mp3:  return readMp3 (in);
        case AudioFormat::wav:  return readChunked (in, false);
        case AudioFormat::aiff: return readChunked (in, true);
        case AudioFormat::flac: return readFlac (in);
    }
    return {};
}

} // namespace dss1::io
