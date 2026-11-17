#pragma once

#include "core/Tags.h"

#include <string>

namespace dss1::io
{

/// The title, artist, album and genre a file is tagged with: ID3v2 (MP3, WAV, AIFF, and FLACs that carry one),
/// ID3v1 (MP3), Vorbis comments (FLAC) and LIST/INFO (WAV). Only the tag parts of the file are read, never the
/// audio. Returns empty tags (never throws) if the file has none or can't be read.
tags::TextTags readTags (const std::string& path);

} // namespace dss1::io
