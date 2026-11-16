#include "io/AudioDecoder.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>

using namespace dss1;
using namespace dss1::io;

namespace
{
    std::string fixture (const char* name)
    {
        return (std::filesystem::path (DSS1_TEST_FIXTURES) / name).string();
    }

    constexpr Frame firstClick = 22050;   // 0.5 s
    constexpr Frame secondClick = 55125;  // 1.25 s

    /// The frame with the loudest sample within `radius` of `around`.
    Frame loudestNear (const std::vector<float>& samples, Frame around, Frame radius)
    {
        Frame best = around;
        float bestLevel = -1.0f;
        for (auto f = std::max<Frame> (0, around - radius); f < std::min<Frame> (static_cast<Frame> (samples.size()), around + radius); ++f)
        {
            const auto level = std::abs (samples[static_cast<std::size_t> (f)]);
            if (level > bestLevel)
            {
                bestLevel = level;
                best = f;
            }
        }
        return best;
    }

    /// The first frame within `radius` of `around` where the level jumps above `threshold`.
    Frame onsetNear (const std::vector<float>& samples, Frame around, Frame radius, float threshold)
    {
        for (auto f = std::max<Frame> (0, around - radius); f < std::min<Frame> (static_cast<Frame> (samples.size()), around + radius); ++f)
            if (std::abs (samples[static_cast<std::size_t> (f)]) > threshold)
                return f;
        return -1;
    }
}

TEST_CASE ("Formats are recognised by extension", "[decoder]")
{
    CHECK (formatForExtension (".WAV") == AudioFormat::wav);
    CHECK (formatForExtension ("aif") == AudioFormat::aiff);
    CHECK (formatForExtension ("flac") == AudioFormat::flac);
    CHECK (formatForExtension ("mp3") == AudioFormat::mp3);
    CHECK_FALSE (formatForExtension ("m4a"));
}

TEST_CASE ("WAV, AIFF and FLAC decode sample-exactly", "[decoder]")
{
    for (const auto* name : { "clicks.wav", "clicks.aiff", "clicks.flac" })
    {
        INFO (name);
        const auto audio = decode (fixture (name));
        CHECK (audio.info.sampleRate == 44100.0);
        CHECK (audio.info.lengthFrames == 88200);
        CHECK (audio.info.encoderDelayFrames == 0);
        REQUIRE (! audio.channels.empty());
        CHECK (audio.channels[0].size() == 88200);

        // Lossless: the clicks start on exactly the right frame, and nothing comes before them.
        CHECK (onsetNear (audio.channels[0], firstClick, 2000, 0.5f) == firstClick);
        CHECK (onsetNear (audio.channels[0], secondClick, 2000, 0.5f) == secondClick);
        CHECK (onsetNear (audio.channels[0], firstClick - 1000, 999, 0.001f) == -1);

        const auto info = probe (fixture (name));
        CHECK (info.lengthFrames == audio.info.lengthFrames);
        CHECK (info.channels == audio.info.channels);
    }
    CHECK (decode (fixture ("clicks.wav")).info.channels == 2);
    CHECK (decode (fixture ("clicks.aiff")).info.channels == 1);
}

TEST_CASE ("MP3 encoder delay is removed so the music starts at frame 0", "[decoder]")
{
    const auto audio = decode (fixture ("clicks.mp3"));
    CHECK (audio.info.sampleRate == 44100.0);
    CHECK (audio.info.channels == 2);

    // The LAME header says how much silence the encoder added; with it removed the file is as long as the
    // original and the clicks are where they were in the source (MP3 smears a click, so allow a few frames).
    CHECK (audio.info.encoderDelayFrames > 0);
    CHECK (audio.info.lengthFrames == 88200);
    CHECK (std::abs (loudestNear (audio.channels[0], firstClick, 3000) - firstClick) <= 4);
    CHECK (std::abs (loudestNear (audio.channels[0], secondClick, 3000) - secondClick) <= 4);

    CHECK (probe (fixture ("clicks.mp3")).lengthFrames == 88200);
}

TEST_CASE ("Unreadable files give a readable error", "[decoder]")
{
    CHECK_THROWS_AS (decode (fixture ("README.md")), std::runtime_error);
    CHECK_THROWS_AS (probe (fixture ("missing.wav")), std::runtime_error);

    try
    {
        decode (fixture ("missing.flac"));
        FAIL ("expected an error");
    }
    catch (const std::runtime_error& e)
    {
        CHECK (std::string (e.what()).find ("missing.flac") != std::string::npos);
    }
}
