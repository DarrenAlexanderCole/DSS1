#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dss1::id3
{

// Puts ID3 GEOB frames into audio files without touching the audio: WAV and AIFF carry an ID3 tag in a
// chunk ("id3 " / "ID3 "), MP3 at the start of the file. Every other frame and chunk is kept. Works on
// bytes in memory; reading and writing files is the caller's job.

using Bytes = std::vector<std::uint8_t>;

enum class Container { wav, aiff, mp3 };

/// "wav" → wav, "aif"/"aiff" → aiff, "mp3" → mp3 (any case, with or without the dot). Others: nullopt.
std::optional<Container> containerForExtension (std::string extension);

struct GeobFrame
{
    std::string description;                       ///< e.g. "Serato Markers2"
    Bytes content;
    std::string mimeType = "application/octet-stream";

    bool operator== (const GeobFrame&) const = default;
};

/// Returns `file` with an ID3 tag holding `frames`. GEOB frames with the same description are replaced; all
/// other frames are kept. An ID3 tag DSS1 can't safely edit (ID3v2.2, unsynchronised or with an extended
/// header) is replaced by a new one. Throws std::runtime_error if the bytes aren't that kind of file.
Bytes writeGeobFrames (const Bytes& file, Container container, const std::vector<GeobFrame>& frames);

/// The GEOB frames in a file's ID3 tag (empty if it has none).
std::vector<GeobFrame> readGeobFrames (const Bytes& file, Container container);

} // namespace dss1::id3
