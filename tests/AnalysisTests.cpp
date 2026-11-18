#include "core/Analysis.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <random>
#include <tuple>

using namespace dss1::analysis;
using Catch::Approx;

namespace
{
    constexpr double rate = 44100.0;
    constexpr double tau = 2.0 * std::numbers::pi;

    /// A drum pattern: kick on every beat, snare on 2 and 4, closed hats on eighths, a bass note, some noise.
    std::vector<float> drumLoop (double bpm, double seconds, unsigned seed = 7)
    {
        const auto n = static_cast<std::size_t> (seconds * rate);
        std::vector<float> x (n, 0.0f);
        std::mt19937 rng (seed);
        std::normal_distribution<float> noise (0.0f, 1.0f);
        for (std::size_t i = 0; i < n; ++i)
            x[i] = 0.01f * noise (rng) + 0.08f * static_cast<float> (std::sin (tau * 55.0 * static_cast<double> (i) / rate));

        const auto beat = 60.0 / bpm;
        for (int b = 0; b * beat < seconds - 0.5; ++b)
        {
            const auto add = [&] (double at, double length, auto&& sample) {
                const auto start = static_cast<std::size_t> (at * rate);
                for (std::size_t k = 0; k < static_cast<std::size_t> (length * rate) && start + k < n; ++k)
                    x[start + k] += sample (static_cast<double> (k) / rate);
            };
            add (b * beat, 0.2, [] (double t) {
                return static_cast<float> (0.8 * std::sin (tau * (50.0 + 100.0 * std::exp (-t * 40.0)) * t) * std::exp (-t * 15.0)); });
            if (b % 2 == 1)
                add (b * beat, 0.15, [&] (double t) { return 0.3f * noise (rng) * static_cast<float> (std::exp (-t * 25.0)); });
            for (int h = 0; h < 2; ++h)
                add (b * beat + h * beat / 2, 0.04, [&] (double t) { return 0.08f * noise (rng) * static_cast<float> (std::exp (-t * 120.0)); });
        }
        return x;
    }

    /// Chords with a few harmonics each, two seconds per chord. Notes as MIDI numbers.
    std::vector<float> chords (const std::vector<std::vector<int>>& progression, int repeats)
    {
        std::vector<float> x;
        for (int r = 0; r < repeats; ++r)
            for (const auto& chord : progression)
            {
                const auto start = x.size();
                x.resize (start + static_cast<std::size_t> (2 * rate), 0.0f);
                for (const auto note : chord)
                {
                    const auto hz = 440.0 * std::pow (2.0, (note - 69) / 12.0);
                    for (int h = 1; h <= 4; ++h)
                        for (std::size_t i = 0; i < static_cast<std::size_t> (2 * rate); ++i)
                            x[start + i] += static_cast<float> (0.08 / h * std::sin (tau * hz * h * static_cast<double> (i) / rate));
                }
            }
        return x;
    }
}

TEST_CASE ("FFT puts a sine in its bin", "[analysis]")
{
    std::vector<double> re (64), im (64, 0.0);
    for (std::size_t i = 0; i < 64; ++i)
        re[i] = std::cos (tau * 5.0 * static_cast<double> (i) / 64.0);
    fft (re, im);
    CHECK (std::hypot (re[5], im[5]) == Approx (32.0).margin (1e-9));
    CHECK (std::hypot (re[6], im[6]) == Approx (0.0).margin (1e-9));
}

TEST_CASE ("Tempo is found on drum loops", "[analysis]")
{
    for (const double bpm : { 90.0, 100.0, 122.0, 128.0, 140.0, 174.0 })
    {
        INFO (bpm << " BPM");
        const auto found = detectBpm (drumLoop (bpm, 40.0), rate);
        REQUIRE (found);
        // Folding into 70–180 keeps every test tempo as it is.
        CHECK (*found == Approx (bpm).margin (0.15));
    }
}

TEST_CASE ("No tempo for silence or a steady tone", "[analysis]")
{
    CHECK_FALSE (detectBpm (std::vector<float> (static_cast<std::size_t> (20 * rate), 0.0f), rate));
    std::vector<float> tone (static_cast<std::size_t> (20 * rate));
    for (std::size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.3f * static_cast<float> (std::sin (tau * 220.0 * static_cast<double> (i) / rate));
    CHECK_FALSE (detectBpm (tone, rate));
}

TEST_CASE ("Key is found from chord progressions", "[analysis]")
{
    // A minor: Am – Dm – E – Am
    auto key = detectKey (chords ({ { 57, 60, 64 }, { 62, 65, 69 }, { 64, 68, 71 }, { 57, 60, 64 } }, 2), rate);
    REQUIRE (key);
    CHECK (key->name() == "A minor");
    CHECK (key->camelot() == "8A");

    // C major: C – F – G – C
    key = detectKey (chords ({ { 60, 64, 67 }, { 65, 69, 72 }, { 67, 71, 74 }, { 60, 64, 67 } }, 2), rate);
    REQUIRE (key);
    CHECK (key->name() == "C major");
    CHECK (key->camelot() == "8B");

    // F minor: Fm – Bbm – C – Fm (Rekordbox "Fm", Serato "4A")
    key = detectKey (chords ({ { 53, 56, 60 }, { 58, 61, 65 }, { 60, 64, 67 }, { 53, 56, 60 } }, 2), rate);
    REQUIRE (key);
    CHECK (key->name() == "F minor");
    CHECK (key->camelot() == "4A");

    CHECK_FALSE (detectKey (std::vector<float> (static_cast<std::size_t> (10 * rate), 0.0f), rate));
}

TEST_CASE ("Loudness of a 1 kHz tone at -23 dBFS is -23 LUFS (EBU Tech 3341)", "[analysis]")
{
    const auto amplitude = static_cast<float> (std::pow (10.0, -23.0 / 20.0));
    for (const double r : { 44100.0, 48000.0 })
    {
        INFO (r << " Hz");
        std::vector<float> tone (static_cast<std::size_t> (20 * r));
        for (std::size_t i = 0; i < tone.size(); ++i)
            tone[i] = amplitude * static_cast<float> (std::sin (tau * 1000.0 * static_cast<double> (i) / r));
        const auto lufs = integratedLoudness ({ tone, tone }, r);
        REQUIRE (lufs);
        CHECK (*lufs == Approx (-23.0).margin (0.1));
    }
    CHECK_FALSE (integratedLoudness ({ std::vector<float> (48000 * 5, 0.0f) }, 48000.0));
}

namespace
{
    /// Four-on-the-floor house with a bar structure: kick on every beat, a bass note and a new chord at the start of
    /// every bar, hats on the off-beats, a clap on 2 and 4. The first downbeat is at `offset` seconds.
    std::vector<float> barLoop (double bpm, double seconds, double offset, unsigned seed = 3)
    {
        const auto n = static_cast<std::size_t> (seconds * rate);
        std::vector<float> x (n, 0.0f);
        std::mt19937 rng (seed);
        std::normal_distribution<float> noise (0.0f, 1.0f);
        const auto beat = 60.0 / bpm;
        const auto add = [&] (double at, double length, auto&& sample) {
            if (at < 0)
                return;
            const auto start = static_cast<std::size_t> (at * rate);
            for (std::size_t k = 0; k < static_cast<std::size_t> (length * rate) && start + k < n; ++k)
                x[start + k] += sample (static_cast<double> (k) / rate);
        };
        const double roots[4] = { 45, 41, 48, 43 };  // A, F, C, G
        for (int b = -4; offset + b * beat < seconds; ++b)
        {
            const auto t0 = offset + b * beat;
            add (t0, 0.2, [] (double t) {
                return static_cast<float> (0.6 * std::sin (tau * (50.0 + 100.0 * std::exp (-t * 40.0)) * t) * std::exp (-t * 15.0)); });
            add (t0 + beat / 2, 0.04, [&] (double t) { return 0.08f * noise (rng) * static_cast<float> (std::exp (-t * 120.0)); });
            if ((b & 1) != 0)
                add (t0, 0.12, [&] (double t) { return 0.25f * noise (rng) * static_cast<float> (std::exp (-t * 30.0)); });
            if (b % 4 == 0)
            {
                const auto root = roots[static_cast<std::size_t> (((b / 4) % 4 + 4) % 4)];
                const auto hz = [] (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); };
                add (t0, beat * 1.5, [&] (double t) { return static_cast<float> (0.35 * std::sin (tau * hz (root - 12) * t) * std::exp (-t * 2.0)); });
                add (t0, beat * 4, [&] (double t) {
                    double v = 0;
                    for (const double m : { root + 12, root + 16, root + 19 })
                        v += std::sin (tau * hz (m) * t);
                    return static_cast<float> (0.06 * v);
                });
            }
        }
        return x;
    }

    /// Distance between two frames, allowing either to be a whole number of `period`s away.
    double phaseError (double found, double expected, double period)
    {
        auto d = std::fmod (found - expected, period);
        if (d < 0)
            d += period;
        return std::min (d, period - d);
    }
}

TEST_CASE ("Beats and downbeats are placed on bar-structured loops", "[analysis]")
{
    for (const auto& [bpm, offset] : { std::pair { 124.0, 0.37 }, std::pair { 128.0, 1.05 }, std::pair { 100.0, 0.0 }, std::pair { 140.0, 2.2 } })
    {
        CAPTURE (bpm, offset);
        const auto audio = barLoop (bpm, 40.0, offset);
        const auto placed = placeBeats (audio, rate, bpm);
        REQUIRE (placed);
        const auto beatFrames = 60.0 / bpm * rate;
        CHECK (phaseError (placed->firstBeat, offset * rate, beatFrames) < 0.004 * rate);
        CHECK (phaseError (placed->firstDownbeat, offset * rate, 4 * beatFrames) < 0.004 * rate);
        CHECK (placed->firstBeat >= 0);
        CHECK (placed->firstBeat < beatFrames);
        CHECK (placed->firstDownbeat < 4 * beatFrames);
        CHECK (placed->downbeatConfidence > 0.1);
    }
}

TEST_CASE ("Beats are placed on a plain drum loop, and not at all in silence", "[analysis]")
{
    const auto placed = placeBeats (drumLoop (122.0, 30.0), rate, 122.0);
    REQUIRE (placed);
    CHECK (phaseError (placed->firstBeat, 0.0, 60.0 / 122.0 * rate) < 0.004 * rate);
    CHECK_FALSE (placeBeats (std::vector<float> (static_cast<std::size_t> (10 * rate), 0.0f), rate, 120.0));
}

TEST_CASE ("Beat placement corrects a slightly wrong tempo, so the grid holds to the end", "[analysis]")
{
    // Three minutes: a 0.03 BPM error would put the last beats ~20 ms out.
    for (const auto& [actual, given, whole] : { std::tuple { 124.0, 123.97, true }, std::tuple { 123.45, 123.48, false } })
    {
        CAPTURE (actual, given);
        const auto offset = 0.62;
        const auto audio = barLoop (actual, 180.0, offset);
        const auto placed = placeBeats (audio, rate, given);
        REQUIRE (placed);
        if (whole)
            CHECK (placed->bpm == Approx (actual).margin (1e-9));
        else
            CHECK (placed->bpm == Approx (actual).margin (0.005));

        // A beat near the end lands within 3 ms of the real one.
        const auto period = 60.0 / placed->bpm * rate;
        const auto lateBeat = placed->firstDownbeat + period * std::floor ((175.0 * rate - placed->firstDownbeat) / period);
        CHECK (phaseError (lateBeat, offset * rate, 60.0 / actual * rate) < 0.003 * rate);
        CHECK (phaseError (placed->firstDownbeat, offset * rate, 4 * 60.0 / actual * rate) < 0.004 * rate);
    }
}
