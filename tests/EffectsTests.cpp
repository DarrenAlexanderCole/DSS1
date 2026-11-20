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
    auto t = sine (220.0f, 0.5);
    run (crush, t);
    // After it has faded in: 12-bit steps, each held for 3 samples.
    for (std::size_t i = t.l.size() / 2; i < t.l.size(); ++i)
        REQUIRE (std::abs (t.l[i] * 2048.0f - std::round (t.l[i] * 2048.0f)) < 1e-2f);
    int changes = 0;
    for (std::size_t i = t.l.size() / 2; i + 1 < t.l.size(); ++i)
        changes += std::abs (t.l[i] - t.l[i + 1]) > 1e-4f;
    CHECK (changes <= static_cast<int> (t.l.size() / 2 / 3) + 1);
}

TEST_CASE ("Compressor: each type squeezes loud sound, leaves quiet sound alone", "[effects]")
{
    for (const auto type : { Effects::CompType::opto, Effects::CompType::vca, Effects::CompType::tube, Effects::CompType::fet })
    {
        Effects fx;
        fx.prepare (rate, 256);
        fx.compOn = true;
        fx.compType = static_cast<int> (type);
        fx.compAmount = 0.75f;  // threshold -27 dB
        fx.compMix = 1.0f;

        auto loud = sine (200.0f, 1.0, 0.9f);
        run (fx, loud);
        CHECK (fx.gainReduction.load() < -6.0f);
        // Squeezed: louder input doesn't come out proportionally louder (make-up is only half the reduction).
        CHECK (rms (loud.l, 30000, 44000) < 0.9f / 1.414f);

        Effects calm;
        calm.prepare (rate, 256);
        calm.compOn = true;
        calm.compType = static_cast<int> (type);
        calm.compAmount = 0.25f;  // threshold -9 dB
        auto quiet = sine (200.0f, 1.0, 0.02f);  // -34 dB
        run (calm, quiet);
        CHECK (calm.gainReduction.load() > -0.5f);
    }
}

TEST_CASE ("Compressor switches: high ratio squeezes harder, mix brings back the dry sound", "[effects]")
{
    const auto reduction = [] (bool highRatio) {
        Effects fx;
        fx.prepare (rate, 256);
        fx.compOn = true;
        fx.compType = static_cast<int> (Effects::CompType::vca);
        fx.compAmount = 0.75f;
        fx.compHighRatio = highRatio;
        auto s = sine (200.0f, 1.0, 0.9f);
        run (fx, s);
        return fx.gainReduction.load();
    };
    CHECK (reduction (true) < reduction (false) - 2.0f);

    Effects off;
    off.prepare (rate, 256);
    auto dry = sine (200.0f, 1.0, 0.9f);
    run (off, dry);
    Effects parallel;
    parallel.prepare (rate, 256);
    parallel.compOn = true;
    parallel.compAmount = 0.75f;
    parallel.compMix = 0.0f;
    auto wet = sine (200.0f, 1.0, 0.9f);
    run (parallel, wet);
    CHECK (rms (wet.l, 30000, 44000) == Approx (rms (dry.l, 30000, 44000)).epsilon (0.01));
}

TEST_CASE ("The COMP module's EQ: low shelf, mid bell, high shelf", "[effects]")
{
    const auto gainAt = [] (float freq, float low, float mid, float high) {
        Effects fx;
        fx.prepare (rate, 256);
        fx.compOn = true;
        fx.compAmount = 0.0f;  // threshold 0 dB: no squeezing at this level, no make-up
        fx.compType = static_cast<int> (Effects::CompType::opto);
        fx.eqLowDb = low;
        fx.eqMidDb = mid;
        fx.eqHighDb = high;
        auto s = sine (freq, 1.0, 0.1f);
        run (fx, s);
        return 20.0f * std::log10 (rms (s.l, 30000, 44000) / (0.1f / 1.414f));
    };
    CHECK (gainAt (60.0f, 12.0f, 0, 0) > 10.0f);
    CHECK (std::abs (gainAt (5000.0f, 12.0f, 0, 0)) < 1.0f);
    CHECK (gainAt (1000.0f, 0, -12.0f, 0) < -10.0f);
    CHECK (gainAt (12000.0f, 0, 0, 12.0f) > 10.0f);
    CHECK (std::abs (gainAt (1000.0f, 0, 0, 0)) < 0.2f);
}
