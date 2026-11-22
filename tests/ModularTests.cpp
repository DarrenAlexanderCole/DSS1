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

TEST_CASE ("Deluxe rack: NOISE adds noise only when it's turned up", "[modular]")
{
    Modular m;
    m.prepare (rate, 256);
    m.controls[Modular::noise].on = true;  // on, but LEVEL at 0: passive
    Stereo silent (24000);
    run (m, silent);
    REQUIRE (rms (silent.l, 0) == 0.0f);

    m.controls[Modular::noise].b = 1.0f;
    m.controls[Modular::noise].mode = true;  // VINYL too
    for (float colour : { 0.0f, 0.5f, 1.0f })
    {
        m.controls[Modular::noise].a = colour;
        Stereo s (48000);
        run (m, s);
        const auto level = rms (s.l, 24000);
        REQUIRE (level > 0.005f);
        REQUIRE (level < 0.5f);
    }
}

TEST_CASE ("Deluxe rack: WAVETABLE ring-modulates, and FM bends the phase", "[modular]")
{
    for (bool fm : { false, true })
    {
        Modular m;
        m.prepare (rate, 256);
        m.rootHz = 220.0f;
        auto& c = m.controls[Modular::wavetable];
        c.a = 0.5f;
        c.b = 1.0f;
        c.mode = fm;
        c.on = true;
        auto s = sine (330.0f, 48000);
        const auto dry = s.l;
        run (m, s);
        float diff = 0;
        for (std::size_t i = 24000; i < s.l.size(); ++i)
        {
            REQUIRE (std::abs (s.l[i]) <= 1.0f);
            diff = std::max (diff, std::abs (s.l[i] - dry[i]));
        }
        REQUIRE (diff > 0.1f);
    }
}

TEST_CASE ("Deluxe rack: MOD moves another module's knob in time", "[modular]")
{
    // MOD sweeping LADDER's cutoff (set half way) a full range, once a beat: a steady bright tone gets louder
    // and quieter with it.
    Modular m;
    m.prepare (rate, 256);
    m.bpm = 120.0f;
    m.controls[Modular::ladder].on = true;
    m.controls[Modular::ladder].a = 0.55f;
    m.controls[Modular::mod].on = true;
    m.controls[Modular::mod].a = 3.0f;  // 1/4 note: one cycle a beat
    m.controls[Modular::mod].b = 1.0f;
    m.modTarget = Modular::ladder * 2;
    auto s = sine (2000.0f, beatsToFrames (4, 120));
    run (m, s);

    // The level, beat-sixteenth by beat-sixteenth through the last beat: it swings a long way.
    const auto beat = beatsToFrames (1, 120);
    float lo = 1, hi = 0;
    for (std::size_t q = 0; q < 16; ++q)
    {
        const auto from = 3 * beat + q * beat / 16;
        double sum = 0;
        for (auto i = from; i < from + beat / 16; ++i)
            sum += static_cast<double> (s.l[i]) * s.l[i];
        const auto level = static_cast<float> (std::sqrt (sum / static_cast<double> (beat / 16)));
        lo = std::min (lo, level);
        hi = std::max (hi, level);
    }
    REQUIRE (hi > 4.0f * lo);

    // Depth 0: nothing moves.
    m.controls[Modular::mod].b = 0.0f;
    Modular still;
    still.prepare (rate, 256);
    still.controls[Modular::mod].on = true;
    still.controls[Modular::mod].b = 0.0f;
    auto quiet = ramp (24000);
    const auto before = quiet.l;
    run (still, quiet);
    REQUIRE (quiet.l == before);
}

TEST_CASE ("Deluxe rack: STEP grabs a beat on the beat and plays its slices in 16 steps", "[modular]")
{
    Modular m;
    m.prepare (rate, 256);
    m.bpm = 120.0f;
    auto& st = m.step;
    st.beats = 1;
    st.grab = true;
    st.level = 1.0f;

    // A ramp; GRAB waits for the next beat, then takes one beat of it.
    auto s = ramp (beatsToFrames (3, 120));
    const auto dry = s.l;
    run (m, s);
    REQUIRE (st.state.load() == Modular::Step::ready);
    REQUIRE (st.slices.load() == 4);
    const auto beat = beatsToFrames (1, 120);
    REQUIRE (st.length.load() == static_cast<int> (beat));
    // It began on beat 2 (the first beat line after GRAB): the sample is the music from there.
    REQUIRE (m.stepSample (0)[100] == Catch::Approx (dry[beat + 100]).margin (1.0e-4));

    // RUN, REPLACE: silence in, the steps out. Step 2 (the second sixteenth) plays slice 2.
    st.running = true;
    st.replace = true;
    Stereo out (beatsToFrames (4, 120));
    run (m, out);
    const auto sixteenth = beat / 4;
    const auto at = 3 * beat + sixteenth + sixteenth / 2;  // the middle of a second sixteenth, once faded in
    REQUIRE (out.l[at] == Catch::Approx (m.stepSample (0)[sixteenth + sixteenth / 2]).margin (2.0e-3));

    // A step switched off stays quiet.
    st.on[1] = false;
    Stereo off (beatsToFrames (2, 120));
    run (m, off);
    REQUIRE (std::abs (off.l[beat + sixteenth + sixteenth / 2]) < 1.0e-4f);

    // CLEAR: empty again.
    st.clear = true;
    Stereo after (512);
    run (m, after);
    REQUIRE (st.state.load() == Modular::Step::empty);
}

TEST_CASE ("Deluxe rack: STEP's gate, repeat, reverse and space echo", "[modular]")
{
    const auto grabbed = [] (Modular& m) {
        m.prepare (rate, 256);
        m.bpm = 120.0f;
        m.step.beats = 1;
        m.step.grab = true;
        m.step.level = 1.0f;
        auto s = ramp (beatsToFrames (3, 120));
        run (m, s);
        REQUIRE (m.step.state.load() == Modular::Step::ready);
        m.step.running = true;
        m.step.replace = true;
    };
    const auto beat = beatsToFrames (1, 120);
    const auto sixteenth = beat / 4;

    SECTION ("GATE LENGTH: half the slice plays, then quiet until the next step")
    {
        Modular m;
        grabbed (m);
        m.step.gateLength = 0.5f;
        Stereo out (beatsToFrames (4, 120));
        run (m, out);
        const auto stepStart = 3 * beat + sixteenth;
        REQUIRE (std::abs (out.l[stepStart + sixteenth / 4]) > 0.001f);
        REQUIRE (std::abs (out.l[stepStart + sixteenth * 3 / 4]) < 1.0e-4f);
    }
    SECTION ("REVERSE: the slice backwards")
    {
        Modular m;
        grabbed (m);
        m.step.reverse = true;
        Stereo out (beatsToFrames (4, 120));
        run (m, out);
        const auto stepStart = 3 * beat + sixteenth;  // step 2: slice 2
        const auto x = sixteenth / 3;
        REQUIRE (out.l[stepStart + x] == Catch::Approx (m.stepSample (0)[sixteenth + sixteenth - 1 - x]).margin (2.0e-3));
    }
    SECTION ("NOTE REPEAT: the start of the slice again and again")
    {
        Modular m;
        grabbed (m);
        m.step.repeat = 3;  // a quarter of a step
        Stereo out (beatsToFrames (4, 120));
        run (m, out);
        const auto stepStart = 3 * beat + sixteenth;
        const auto quarter = sixteenth / 4;
        REQUIRE (out.l[stepStart + quarter / 2] == Catch::Approx (out.l[stepStart + quarter + quarter / 2]).margin (2.0e-3));
    }
    SECTION ("SPACE ECHO: the steps come back after they stop, in range")
    {
        Modular m;
        grabbed (m);
        m.step.echoAmount = 1.0f;
        Stereo out (beatsToFrames (4, 120));
        run (m, out);
        m.step.running = false;
        m.step.replace = false;
        Stereo tail (beatsToFrames (1, 120));
        run (m, tail);
        REQUIRE (rms (tail.l, beatsToFrames (0.25, 120)) > 0.005f);
        // Full feedback on a full-level sample: it builds, but stays bounded (the tape saturates).
        for (auto v : out.l)
            REQUIRE (std::abs (v) <= 2.0f);
    }
}
