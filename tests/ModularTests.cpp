#include "engine/Modular.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace dss1::engine;

namespace
{
    constexpr double rate = 48000.0;

    struct Stereo
    {
        std::vector<float> l, r;
        explicit Stereo (std::size_t n) : l (n, 0.0f), r (n, 0.0f) {}
    };

    // A rising ramp: every sample different, so a repeat is easy to spot.
    Stereo ramp (std::size_t n)
    {
        Stereo s (n);
        for (std::size_t i = 0; i < n; ++i)
            s.l[i] = s.r[i] = static_cast<float> (i % 100000) / 100000.0f;
        return s;
    }

    void run (Modular& m, Stereo& s, int block = 256)
    {
        for (std::size_t start = 0; start < s.l.size(); start += static_cast<std::size_t> (block))
        {
            const auto n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (block), s.l.size() - start));
            float* ch[2] { s.l.data() + start, s.r.data() + start };
            m.process (ch, 2, n, 0.0, false);
        }
    }

    std::size_t beatsToFrames (double beats, double bpm) { return static_cast<std::size_t> (beats * 60.0 / bpm * rate); }
}

TEST_CASE ("Deluxe rack: with every module off the sound passes untouched", "[modular]")
{
    Modular m;
    m.prepare (rate, 256);
    auto s = ramp (48000);
    const auto before = s.l;
    run (m, s);
    REQUIRE (s.l == before);
}

TEST_CASE ("Deluxe rack: STUTTER repeats a slice that starts on the beat", "[modular]")
{
    Modular m;
    m.prepare (rate, 256);
    m.bpm = 120.0f;
    auto& c = m.controls[Modular::stutter];
    c.a = 1.0f;  // 1/16 note = a quarter of a beat
    c.b = 1.0f;
    c.mode = true;  // HOLD
    c.on = true;

    auto s = ramp (beatsToFrames (8, 120));
    const auto dry = s.l;
    run (m, s);

    // Once the mix has come up, the output is the same slice over and over: one slice length apart, it's equal
    // (away from the 2 ms fades at the slice's edges).
    const auto slice = beatsToFrames (0.25, 120);
    const auto at = beatsToFrames (6, 120) + slice / 2;
    REQUIRE (s.l[at] == Catch::Approx (s.l[at + slice]).margin (1.0e-4));
    REQUIRE (s.l[at] == Catch::Approx (s.l[at + 3 * slice]).margin (1.0e-4));
    // ...and it isn't the live sound any more.
    REQUIRE (std::abs (s.l[at] - dry[at]) > 0.01f);
}

TEST_CASE ("Deluxe rack: SLICER gates the sound to its pattern", "[modular]")
{
    Modular m;
    m.prepare (rate, 256);
    m.bpm = 120.0f;
    auto& c = m.controls[Modular::slicer];
    c.a = 4.0f;  // quarter stabs: open on the first sixteenth of each beat
    c.b = 1.0f;
    c.on = true;

    Stereo s (beatsToFrames (8, 120));
    std::fill (s.l.begin(), s.l.end(), 0.5f);
    std::fill (s.r.begin(), s.r.end(), 0.5f);
    run (m, s);

    const auto beat = beatsToFrames (1, 120);
    const auto bar5 = 4 * beat;
    // Open in the middle of the first sixteenth, closed in the middle of the third.
    REQUIRE (s.l[bar5 + beat / 8] > 0.45f);
    REQUIRE (s.l[bar5 + beat / 2 + beat / 8] < 0.02f);
}

TEST_CASE ("Deluxe rack: the slot order decides which module runs first", "[modular]")
{
    // SLICER fully closed in front of STUTTER: STUTTER repeats silence. Behind it: STUTTER's repeats are gated.
    // Either way nothing should blow up and the sound stays in range.
    Modular m;
    m.prepare (rate, 256);
    m.controls[Modular::stutter].on = true;
    m.controls[Modular::stutter].b = 1.0f;
    m.controls[Modular::slicer].on = true;
    m.controls[Modular::slicer].b = 1.0f;
    m.order[0] = Modular::slicer;
    m.order[1] = Modular::stutter;
    auto s = ramp (48000);
    run (m, s);
    for (auto v : s.l)
        REQUIRE (std::abs (v) <= 1.0f);
}

namespace
{
    Stereo sine (float hz, std::size_t n, float level = 0.5f)
    {
        Stereo s (n);
        for (std::size_t i = 0; i < n; ++i)
            s.l[i] = s.r[i] = level * std::sin (6.2831853f * hz * static_cast<float> (i) / static_cast<float> (rate));
        return s;
    }

    float rms (const std::vector<float>& v, std::size_t from)
    {
        double sum = 0;
        for (auto i = from; i < v.size(); ++i)
            sum += static_cast<double> (v[i]) * v[i];
        return static_cast<float> (std::sqrt (sum / static_cast<double> (v.size() - from)));
    }
}

TEST_CASE ("Deluxe rack: LADDER filters, and steps aside when it's open", "[modular]")
{
    SECTION ("open with no resonance: untouched")
    {
        Modular m;
        m.prepare (rate, 256);
        m.controls[Modular::ladder].on = true;
        m.controls[Modular::ladder].a = 1.0f;
        auto s = sine (5000.0f, 24000);
        const auto before = s.l;
        run (m, s);
        REQUIRE (s.l == before);
    }
    SECTION ("closed down low: highs gone, lows through")
    {
        Modular m;
        m.prepare (rate, 256);
        m.controls[Modular::ladder].on = true;
        m.controls[Modular::ladder].a = 0.4f;  // about 320 Hz
        auto high = sine (6000.0f, 48000);
        run (m, high);
        REQUIRE (rms (high.l, 24000) < 0.01f);

        Modular m2;
        m2.prepare (rate, 256);
        m2.controls[Modular::ladder].on = true;
        m2.controls[Modular::ladder].a = 0.4f;
        auto low = sine (60.0f, 48000);
        run (m2, low);
        REQUIRE (rms (low.l, 24000) > 0.25f);
    }
}

TEST_CASE ("Deluxe rack: MULTI-TAP echoes on the beat", "[modular]")
{
    Modular m;
    m.prepare (rate, 256);
    m.bpm = 120.0f;
    auto& c = m.controls[Modular::multitap];
    c.a = 1.0f;  // half a beat
    c.b = 1.0f;
    c.on = true;
    // Let the amount come up, then a click.
    Stereo s (beatsToFrames (6, 120));
    const auto click = beatsToFrames (1, 120);
    s.l[click] = s.r[click] = 1.0f;
    run (m, s);
    const auto half = beatsToFrames (0.5, 120);
    for (std::size_t t = 1; t <= 4; ++t)
        REQUIRE (std::abs (s.l[click + t * half]) + std::abs (s.l[click + t * half + 1]) > 0.1f);
    // And quiet between the taps.
    REQUIRE (std::abs (s.l[click + half + half / 2]) < 0.01f);
}

TEST_CASE ("Deluxe rack: DRIFT bends the pitch, and stays in range", "[modular]")
{
    Modular m;
    m.prepare (rate, 256);
    auto& c = m.controls[Modular::drift];
    c.a = 1.0f;
    c.b = 1.0f;
    c.mode = true;
    c.on = true;
    auto s = sine (440.0f, 96000);
    const auto dry = s.l;
    run (m, s);
    float diff = 0;
    for (std::size_t i = 48000; i < s.l.size(); ++i)
    {
        REQUIRE (std::abs (s.l[i]) <= 1.0f);
        diff = std::max (diff, std::abs (s.l[i] - dry[i]));
    }
    REQUIRE (diff > 0.1f);
}
