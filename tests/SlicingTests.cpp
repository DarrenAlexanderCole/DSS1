#include "engine/Slicing.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace dss1;

namespace
{
    // Four "hits" (decaying 200 Hz bursts) on silence, at 0.25 s, 0.75 s, 1.25 s and 1.75 s.
    io::DecodedAudio hits (double rate = 44100.0)
    {
        io::DecodedAudio a;
        a.info.sampleRate = rate;
        const auto n = static_cast<std::size_t> (rate * 2);
        a.channels.assign (2, std::vector<float> (n, 0.0f));
        for (const auto t : { 0.25, 0.75, 1.25, 1.75 })
        {
            const auto start = static_cast<std::size_t> (t * rate);
            for (std::size_t i = 0; i < static_cast<std::size_t> (rate * 0.2); ++i)
            {
                const auto v = static_cast<float> (std::sin (2.0 * 3.14159265 * 200.0 * static_cast<double> (i) / rate) * std::exp (-static_cast<double> (i) / (rate * 0.04)));
                a.channels[0][start + i] = a.channels[1][start + i] = v * 0.8f;
            }
        }
        return a;
    }
}

TEST_CASE ("Transients are found just before each hit, in order", "[slicing]")
{
    const auto a = hits();
    const auto found = engine::findTransients (a, 0, static_cast<std::int64_t> (a.channels[0].size()), 16);
    REQUIRE (found.size() == 4);
    const double expected[] { 0.25, 0.75, 1.25, 1.75 };
    for (std::size_t i = 0; i < 4; ++i)
    {
        const auto seconds = static_cast<double> (found[i]) / a.info.sampleRate;
        CHECK (seconds <= expected[i] + 0.001);  // never after the hit starts …
        CHECK (seconds > expected[i] - 0.012);   // … and only just before it
    }
    // Asking for fewer keeps the strongest; a range keeps to it.
    CHECK (engine::findTransients (a, 0, static_cast<std::int64_t> (a.channels[0].size()), 2).size() == 2);
    CHECK (engine::findTransients (a, static_cast<std::int64_t> (0.5 * a.info.sampleRate), static_cast<std::int64_t> (1.5 * a.info.sampleRate)).size() == 2);
}

TEST_CASE ("Zero crossings are found nearby", "[slicing]")
{
    io::DecodedAudio a;
    a.info.sampleRate = 1000.0;
    a.channels.assign (1, std::vector<float> (100));
    for (std::size_t i = 0; i < 100; ++i)
        a.channels[0][i] = i < 40 ? 0.5f : -0.5f;  // crosses at 40
    CHECK (engine::nearestZeroCrossing (a, 35, 10) == 40);
    CHECK (engine::nearestZeroCrossing (a, 46, 10) == 40);
    CHECK (engine::nearestZeroCrossing (a, 20, 5) == 20);  // too far: unchanged
}
