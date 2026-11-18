#include "engine/Effects.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace dss1::engine;
using Catch::Approx;

namespace
{
    constexpr double rate = 44100.0;
    constexpr float tau = 6.2831853f;

    struct Stereo
    {
        std::vector<float> l, r;
        explicit Stereo (std::size_t n) : l (n, 0.0f), r (n, 0.0f) {}
    };

    Stereo sine (float hz, double seconds, float level = 0.5f)
    {
        Stereo s (static_cast<std::size_t> (seconds * rate));
        for (std::size_t i = 0; i < s.l.size(); ++i)
            s.l[i] = s.r[i] = level * std::sin (tau * hz * static_cast<float> (i) / static_cast<float> (rate));
        return s;
    }

    void run (Effects& fx, Stereo& s, int block = 256)
    {
        for (std::size_t start = 0; start < s.l.size(); start += static_cast<std::size_t> (block))
        {
            const auto n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (block), s.l.size() - start));
            float* ch[2] { s.l.data() + start, s.r.data() + start };
            fx.process (ch, 2, n);
        }
    }

    float rms (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double sum = 0;
        for (auto i = from; i < to; ++i)
            sum += x[i] * x[i];
        return static_cast<float> (std::sqrt (sum / static_cast<double> (to - from)));
    }
}

TEST_CASE ("With every effect off, audio passes through untouched", "[effects]")
{
    Effects fx;
    fx.prepare (rate, 256);
    auto s = sine (440.0f, 1.0);
    const auto original = s.l;
    run (fx, s);
    for (std::size_t i = 0; i < original.size(); ++i)
        REQUIRE (s.l[i] == original[i]);
}

TEST_CASE ("The filter: low-pass to the left, high-pass to the right, open in the middle", "[effects]")
{
    const auto level = [] (float hz, float amount) {
        Effects fx;
        fx.prepare (rate, 256);
        fx.filterOn = true;
        fx.filter = amount;
        auto s = sine (hz, 1.0);
        run (fx, s);
        return rms (s.l, s.l.size() / 2, s.l.size());
    };
    const auto open = 0.5f / std::sqrt (2.0f);
    CHECK (level (1000.0f, 0.0f) == Approx (open).epsilon (0.05));
    CHECK (level (5000.0f, -0.8f) < open * 0.1f);   // low-pass closes on highs
    CHECK (level (100.0f, -0.8f) > open * 0.7f);    // lows pass
    CHECK (level (100.0f, 0.8f) < open * 0.1f);     // high-pass closes on lows
    CHECK (level (8000.0f, 0.6f) > open * 0.7f);    // highs pass
}

TEST_CASE ("Echo repeats the sound a synced number of beats later", "[effects]")
{
    Effects fx;
    fx.prepare (rate, 256);
    fx.echoOn = true;
    fx.bpm = 120.0f;
    fx.echoBeats = 0.5f;   // 0.25 s
    fx.echoMix = 1.0f;
    fx.echoFeedback = 0.0f;
    Stereo s (static_cast<std::size_t> (rate));
    // Let the delay time settle, then send a click.
    run (fx, s);
    Stereo click (static_cast<std::size_t> (rate));
    click.l[100] = click.r[100] = 1.0f;
    run (fx, click);
    std::size_t peak = 200;
    for (std::size_t i = 200; i < click.l.size(); ++i)
        if (std::abs (click.l[i]) > std::abs (click.l[peak]))
            peak = i;
    CHECK (static_cast<double> (peak - 100) == Approx (0.25 * rate).margin (8));
}

TEST_CASE ("Reverb leaves a tail, crusher quantises", "[effects]")
{
    Effects fx;
    fx.prepare (rate, 256);
    fx.reverbOn = true;
    fx.reverbMix = 1.0f;
    Stereo s (static_cast<std::size_t> (rate));
    for (std::size_t i = 0; i < 2000; ++i)
        s.l[i] = s.r[i] = 0.5f * std::sin (static_cast<float> (i) * 0.1f);
    run (fx, s);
    CHECK (rms (s.l, 10000, 20000) > 0.001f);

    Effects crush;
    crush.prepare (rate, 256);
    crush.crushOn = true;
    crush.crushBits = 3.0f;
    crush.crushRate = 1.0f;
    crush.crushMix = 1.0f;
    auto t = sine (220.0f, 0.5);
    run (crush, t);
    // After the mix has faded in, every sample is a multiple of 1/4 (3 bits: 4 levels per side).
    for (std::size_t i = t.l.size() / 2; i < t.l.size(); ++i)
        REQUIRE (std::abs (t.l[i] * 4.0f - std::round (t.l[i] * 4.0f)) < 1e-3f);
}
