#pragma once

#include "core/BeatGrid.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dss1::io
{

// Reads audio files into samples, the same way on every platform, so a cue placed at a frame in DSS1 is at the
// same moment of music on any Mac or PC. DSS1 bundles its own decoders instead of using the operating system's:
// dr_wav (WAV, AIFF), dr_flac (FLAC) and minimp3 (MP3), all public domain.
//
// MP3s carry silence the encoder added at the start ("encoder delay") and end ("padding"). When the file says
// how much (a LAME/Xing header, which almost every MP3 has), DSS1 removes it, so frame 0 is the first real sample
// of music, as the encoder intended. The removed amount is reported, because other DJ apps don't all agree on
// this, and DSS1 may need to shift positions for a particular target.

enum class AudioFormat { wav, aiff, flac, mp3 };

/// "wav", ".AIFF", "aif", "flac", "mp3" → the format. Others (m4a, ogg …) → nullopt for now.
std::optional<AudioFormat> formatForExtension (std::string extension);

struct AudioInfo
{
    AudioFormat format = AudioFormat::wav;
    double sampleRate = 0;
    int channels = 0;
    Frame lengthFrames = 0;
    Frame encoderDelayFrames = 0;    ///< MP3 only: silence removed from the start
};

struct DecodedAudio
{
    AudioInfo info;
    /// One vector per channel, each lengthFrames long, samples in -1 … 1.
    std::vector<std::vector<float>> channels;
};

/// Reads the file's format, rate, channel count and length without decoding all of it (an MP3 without a
/// header is scanned). Throws std::runtime_error with a readable message if the file can't be read.
AudioInfo probe (const std::string& path);

/// Decodes the whole file. Throws std::runtime_error with a readable message if the file can't be read.
DecodedAudio decode (const std::string& path);

/// Writes audio as a 24-bit PCM WAV file. Throws std::runtime_error if it can't.
void writeWav (const std::string& path, const DecodedAudio& audio);

} // namespace dss1::io
