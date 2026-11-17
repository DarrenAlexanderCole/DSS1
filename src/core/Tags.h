#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dss1::tags
{

// Reads the title, artist, album and genre a file was tagged with. These parsers work on bytes already pulled
// out of a file (an ID3 tag, a FLAC Vorbis comment block, a WAV LIST/INFO chunk); io::readTags finds those
// pieces in a file without reading the audio. Everything comes back as UTF-8.

using Bytes = std::vector<std::uint8_t>;

struct TextTags
{
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;

    bool empty() const { return title.empty() && artist.empty() && album.empty() && genre.empty(); }
    /// Fills in whatever this is missing from `other`.
    void fillFrom (const TextTags& other);
    bool operator== (const TextTags&) const = default;
};

/// An ID3v2.2, 2.3 or 2.4 tag, starting with "ID3". Unreadable parts are skipped, never thrown.
TextTags parseId3v2 (const Bytes& tag);

/// The 128-byte ID3v1 tag at the end of some MP3s, starting with "TAG".
TextTags parseId3v1 (const Bytes& tag);

/// A FLAC VORBIS_COMMENT metadata block body (vendor string, then KEY=value comments).
TextTags parseVorbisComment (const Bytes& block);

/// A WAV "LIST" chunk body of type INFO (INAM, IART, IPRD, IGNR).
TextTags parseRiffInfo (const Bytes& listBody);

/// ID3v1 genre number → name (the original 80, plus Winamp's extensions up to 125). Empty if unknown.
std::string id3v1Genre (int number);

} // namespace dss1::tags
