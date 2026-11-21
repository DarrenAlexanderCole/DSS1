#include "Modular.h"

#include <algorithm>
#include <cmath>

namespace dss1::engine
{

void Modular::setSync (double anchorFrame, double beatFrames, int beatInBar, int beatsPerBar) noexcept
{
    // Written to the slot the audio thread isn't reading, then made current: never torn.
    const auto next = 1 - syncSlot.load (std::memory_order_relaxed);
    sync[static_cast<std::size_t> (next)] = { anchorFrame, beatFrames, beatInBar, std::max (1, beatsPerBar) };
    syncSlot.store (next, std::memory_order_release);
}

void Modular::prepare (double sampleRate, int maxBlock)
{
    rate = sampleRate;
    for (auto& h : history)
        h.assign (static_cast<std::size_t> (sampleRate * 6.0), 0.0f);  // a 1/2-note slice at 40 BPM, and room
    for (auto& l : driftLine)
        l.assign (static_cast<std::size_t> (sampleRate * 0.1), 0.0f);
    for (auto& l : fmLine)
        l.assign (static_cast<std::size_t> (sampleRate * 0.02), 0.0f);
    if (waves.empty())
    {
        // The table: sine → triangle → saw → square → thin pulse, each band-limited (40 harmonics), with the
        // waves between them crossfaded.
        const auto shape = [] (int kind, double x) {
            constexpr double tau = 6.283185307179586;
            double v = 0;
            for (int n = 1; n <= 40; ++n)
            {
                const auto h = std::sin (tau * n * x);
                switch (kind)
                {
                    case 0: v = std::sin (tau * x); return v;
                    case 1: if (n % 2 == 1) v += h * (((n - 1) / 2) % 2 == 0 ? 1.0 : -1.0) / (n * n); break;
                    case 2: v += h / n * 0.6; break;
                    case 3: if (n % 2 == 1) v += h / n * 1.0; break;
                    default: v += std::sin (tau * n * x) * std::cos (tau * n * 0.12) / 12.0; break;  // a narrow pulse
                }
            }
            return v;
        };
        waves.assign (static_cast<std::size_t> (waveCount * waveSize), 0.0f);
        for (int w = 0; w < waveCount; ++w)
        {
            const auto pos = static_cast<double> (w) / (waveCount - 1) * 4.0;
            const auto a = std::min (3, static_cast<int> (pos));
            const auto t = pos - a;
            float peak = 0;
            for (int i = 0; i < waveSize; ++i)
            {
                const auto x = static_cast<double> (i) / waveSize;
                const auto v = static_cast<float> (shape (a, x) * (1.0 - t) + shape (a + 1, x) * t);
                waves[static_cast<std::size_t> (w * waveSize + i)] = v;
                peak = std::max (peak, std::abs (v));
            }
            for (int i = 0; i < waveSize; ++i)
                waves[static_cast<std::size_t> (w * waveSize + i)] /= std::max (peak, 1.0e-3f);
        }
    }
    for (auto& l : tapLine)
        l.assign (static_cast<std::size_t> (sampleRate * 9.0), 0.0f);  // four taps 1 1/2 beats apart at 40 BPM
    beatBuffer.assign (static_cast<std::size_t> (std::max (maxBlock, 1) * 2), 0.0);
    reset();
}

void Modular::reset() noexcept
{
    for (auto& h : history)
        std::fill (h.begin(), h.end(), 0.0f);
    historyWrite = 0;
    sliceStart = sliceLength = slicePos = 0;
    repeating = haveSlice = false;
    lastSliceIndex = lastPeriodIndex = -1;
    stutterMix = 0;
    gate = 1;
    slicerDepth = 0;
    for (auto& l : driftLine)
        std::fill (l.begin(), l.end(), 0.0f);
    for (auto& l : tapLine)
        std::fill (l.begin(), l.end(), 0.0f);
    driftWrite = tapWrite = 0;
    driftTone = tapTone = {};
    driftAmount = tapAmount = ladderAmount = 0;
    for (auto& c : ladderStage)
        c = {};
}

float Modular::random() noexcept
{
    noiseSeed = noiseSeed * 1664525u + 1013904223u;
    return static_cast<float> (noiseSeed >> 8) / static_cast<float> (1u << 24) * 2.0f - 1.0f;
}

void Modular::process (float* const* channels, int numChannels, int numFrames, double trackFrame, bool trackPlaying) noexcept
{
    if (numFrames <= 0 || history[0].empty())
        return;
    numChannels = std::min (numChannels, 2);

    // Where the beat is, sample by sample, in beats from the start of the bar.
    if (static_cast<std::size_t> (numFrames) > beatBuffer.size())
        numFrames = static_cast<int> (beatBuffer.size());
    const auto& s = sync[static_cast<std::size_t> (syncSlot.load (std::memory_order_acquire))];
    const auto perSample = std::max (20.0f, bpm.load (std::memory_order_relaxed)) / 60.0 / rate;
    const auto perBar = static_cast<double> (s.beatsPerBar);
    double beat;
    if (trackPlaying && s.beatFrames > 0)
    {
        beat = static_cast<double> (s.beatInBar) + (trackFrame - s.anchorFrame) / s.beatFrames;
        ownBeat = beat;
    }
    else
        beat = ownBeat;
    for (int i = 0; i < numFrames; ++i)
    {
        auto b = std::fmod (beat + perSample * i, perBar);
        beatBuffer[static_cast<std::size_t> (i)] = b < 0 ? b + perBar : b;
    }
    ownBeat = std::fmod (beat + perSample * numFrames, perBar);

    // The modules, in slot order.
    for (const auto& slot : order)
    {
        switch (slot.load (std::memory_order_relaxed))
        {
            case stutter: runStutter (channels, numChannels, numFrames, beatBuffer.data()); break;
            case slicer:  runSlicer (channels, numChannels, numFrames, beatBuffer.data(), perBar); break;
            case noise:     runNoise (channels, numChannels, numFrames); break;
            case wavetable: runWavetable (channels, numChannels, numFrames); break;
            case drift:    runDrift (channels, numChannels, numFrames); break;
            case multitap: runMultiTap (channels, numChannels, numFrames); break;
            case ladder:   runLadder (channels, numChannels, numFrames); break;
            default: break;  // not built yet: the sound passes through
        }
    }
}

void Modular::runStutter (float* const* ch, int numChannels, int numFrames, const double* beats) noexcept
{
    auto& c = controls[stutter];
    const auto on = c.on.load (std::memory_order_relaxed);
    const auto mixTarget = on ? std::clamp (c.b.load (std::memory_order_relaxed), 0.0f, 1.0f) : 0.0f;
    const auto hold = c.mode.load (std::memory_order_relaxed);
    const auto lengthIndex = std::clamp (static_cast<int> (std::lround (c.a.load (std::memory_order_relaxed))), 0, 4);
    const auto lengthBeats = stutterBeats[static_cast<std::size_t> (lengthIndex)];
    const auto periodBeats = std::max (1.0, lengthBeats);  // a fresh slice every beat (or every slice, if longer)
    const auto size = history[0].size();
    const auto perSample = std::max (20.0f, bpm.load (std::memory_order_relaxed)) / 60.0 / rate;
    const auto length = std::min (size / 2, static_cast<std::size_t> (lengthBeats / perSample));
    const auto fade = std::max<std::size_t> (1, std::min<std::size_t> (static_cast<std::size_t> (rate * 0.002), length / 4));
    const bool idle = mixTarget <= 0.0f && stutterMix < 1.0e-4f;

    for (int i = 0; i < numFrames; ++i)
    {
        const auto beat = beats[i];
        // Remember everything that's heard.
        for (int k = 0; k < numChannels; ++k)
            history[static_cast<std::size_t> (k)][historyWrite] = ch[k][i];
        if (numChannels == 1)
            history[1][historyWrite] = ch[0][i];

        if (! idle)
        {
            // A new slice starts on the beat: every beat (or every slice length, if longer), unless HOLD keeps
            // the one it has. Turning on waits for the next slice line.
            const auto sliceIndex = static_cast<long long> (std::floor (beat / lengthBeats));
            const auto periodIndex = static_cast<long long> (std::floor (beat / periodBeats));
            const bool onSliceLine = sliceIndex != lastSliceIndex && lastSliceIndex >= 0;
            const bool newPeriod = periodIndex != lastPeriodIndex && lastPeriodIndex >= 0;
            if (on && onSliceLine && (! haveSlice || (newPeriod && ! hold)))
            {
                sliceStart = historyWrite;
                sliceLength = std::max<std::size_t> (length, 16);
                slicePos = 0;
                repeating = false;
                haveSlice = true;
            }
            lastSliceIndex = sliceIndex;
            lastPeriodIndex = periodIndex;

            stutterMix += (mixTarget - stutterMix) * 0.0015f;
            if (haveSlice && repeating)
            {
                // Each repeat fades in and out over 2 ms so it never clicks.
                const auto edge = std::min ({ slicePos, sliceLength - slicePos, fade });
                const auto window = static_cast<float> (edge) / static_cast<float> (fade);
                const auto read = (sliceStart + slicePos) % size;
                for (int k = 0; k < numChannels; ++k)
                {
                    const auto wet = history[static_cast<std::size_t> (k)][read] * window;
                    ch[k][i] += (wet - ch[k][i]) * stutterMix;
                }
            }
            if (haveSlice && ++slicePos >= sliceLength)
            {
                slicePos = 0;
                repeating = true;
            }
            if (! on && stutterMix < 1.0e-4f)
                haveSlice = repeating = false;
        }
        else
        {
            lastSliceIndex = static_cast<long long> (std::floor (beat / lengthBeats));
            lastPeriodIndex = static_cast<long long> (std::floor (beat / periodBeats));
            haveSlice = repeating = false;
        }
        historyWrite = (historyWrite + 1) % size;
    }
}

void Modular::runSlicer (float* const* ch, int numChannels, int numFrames, const double* beats, double beatsPerBar) noexcept
{
    auto& c = controls[slicer];
    const auto depthTarget = c.on.load (std::memory_order_relaxed) ? std::clamp (c.b.load (std::memory_order_relaxed), 0.0f, 1.0f) : 0.0f;
    if (depthTarget <= 0.0f && slicerDepth < 1.0e-4f)
    {
        slicerDepth = 0;
        gate = 1;
        return;
    }
    const auto pattern = slicerPatterns[static_cast<std::size_t> (std::clamp (static_cast<int> (std::lround (c.a.load (std::memory_order_relaxed))), 0, 7))];
    const auto triplet = c.mode.load (std::memory_order_relaxed);
    const auto stepsPerBeat = triplet ? 3.0 : 4.0;
    const auto steps = static_cast<int> (std::lround (beatsPerBar * stepsPerBeat));
    const auto attack = 1.0f - std::exp (-1.0f / static_cast<float> (rate * 0.0015));
    const auto release = 1.0f - std::exp (-1.0f / static_cast<float> (rate * 0.006));

    for (int i = 0; i < numFrames; ++i)
    {
        const auto step = static_cast<int> (std::floor (beats[i] * stepsPerBeat)) % std::max (1, steps);
        const bool open = ((pattern >> (15 - (step % 16))) & 1u) != 0;
        const auto target = open ? 1.0f : 0.0f;
        gate += (target - gate) * (target > gate ? attack : release);
        slicerDepth += (depthTarget - slicerDepth) * 0.0015f;
        const auto gain = 1.0f - slicerDepth * (1.0f - gate);
        for (int k = 0; k < numChannels; ++k)
            ch[k][i] *= gain;
    }
}

void Modular::runNoise (float* const* ch, int numChannels, int numFrames) noexcept
{
    // Noise under the music: brown, pink or white (and the shades between), ducking while the music is loud so
    // it breathes in the gaps. VINYL adds crackle and a low rumble.
    auto& c = controls[noise];
    const auto levelTarget = c.on.load (std::memory_order_relaxed) ? std::clamp (c.b.load (std::memory_order_relaxed), 0.0f, 1.0f) : 0.0f;
    if (levelTarget <= 0.0f && noiseLevel < 1.0e-5f)
    {
        noiseLevel = 0;
        return;
    }
    const auto colourTarget = std::clamp (c.a.load (std::memory_order_relaxed), 0.0f, 1.0f);
    const auto vinylTarget = c.mode.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    const auto duckAttack = 1.0f - std::exp (-1.0f / static_cast<float> (rate * 0.005));
    const auto duckRelease = 1.0f - std::exp (-1.0f / static_cast<float> (rate * 0.25));
    const auto crackleCut = 1.0f - std::exp (-6.2831853f * 3000.0f / static_cast<float> (rate));
    const auto rumbleTone = 1.0f - std::exp (-6.2831853f * 40.0f / static_cast<float> (rate));
    const auto crackleChance = 9.0f / static_cast<float> (rate);  // about nine pops a second

    for (int i = 0; i < numFrames; ++i)
    {
        noiseLevel += (levelTarget - noiseLevel) * 0.0015f;
        noiseColour += (colourTarget - noiseColour) * 0.0015f;
        noiseVinyl += (vinylTarget - noiseVinyl) * 0.0015f;
        float loud = 0;
        for (int k = 0; k < numChannels; ++k)
            loud = std::max (loud, std::abs (ch[k][i]));
        noiseDuck += (loud - noiseDuck) * (loud > noiseDuck ? duckAttack : duckRelease);
        const auto duck = 1.0f / (1.0f + 4.0f * noiseDuck);

        // A pop now and then, shared by both channels (it's the record), decaying fast.
        if (noiseVinyl > 1.0e-3f && (random() * 0.5f + 0.5f) < crackleChance)
            crackle = random() * (0.4f + 0.6f * std::abs (random()));
        crackle *= 0.985f;

        for (int k = 0; k < std::min (numChannels, 2); ++k)
        {
            const auto ks = static_cast<std::size_t> (k);
            const auto white = random();
            // Pink (Paul Kellet's filter) and brown (leaky integrator).
            auto& p = pink[ks];
            p[0] = 0.99886f * p[0] + white * 0.0555179f;
            p[1] = 0.99332f * p[1] + white * 0.0750759f;
            p[2] = 0.96900f * p[2] + white * 0.1538520f;
            p[3] = 0.86650f * p[3] + white * 0.3104856f;
            p[4] = 0.55000f * p[4] + white * 0.5329522f;
            p[5] = -0.7616f * p[5] - white * 0.0168980f;
            const auto pinkOut = (p[0] + p[1] + p[2] + p[3] + p[4] + p[5] + p[6] + white * 0.5362f) * 0.11f;
            p[6] = white * 0.115926f;
            auto& b = brown[ks];
            b = (b + 0.02f * white) / 1.02f;
            const auto brownOut = b * 3.5f;
            const auto hue = noiseColour * 2.0f;
            auto n = hue < 1.0f ? brownOut + (pinkOut - brownOut) * hue : pinkOut + (white * 0.5f - pinkOut) * (hue - 1.0f);

            if (noiseVinyl > 1.0e-3f)
            {
                auto& t = this->crackleTone[ks];
                t += (crackle - t) * crackleCut;
                auto& r = rumble[ks];
                r += (white - r) * rumbleTone;
                n = n * (1.0f - 0.5f * noiseVinyl) + (t * 1.2f + r * 2.5f) * noiseVinyl;
            }
            ch[k][i] += n * noiseLevel * 0.25f * (0.3f + 0.7f * duck);
        }
    }
}

void Modular::runWavetable (float* const* ch, int numChannels, int numFrames) noexcept
{
    // A wave from the table at the track's root note, ring-modulating the music (the robot, the bell), or with
    // FM bending the music's phase with it (metal, glassy sidebands). MORPH sweeps the table.
    auto& c = controls[wavetable];
    const auto mixTarget = c.on.load (std::memory_order_relaxed) ? std::clamp (c.b.load (std::memory_order_relaxed), 0.0f, 1.0f) : 0.0f;
    const auto size = fmLine[0].size();
    if (mixTarget <= 0.0f && waveMix < 1.0e-4f)
    {
        waveMix = 0;
        // Keep the FM line filled, so switching to it doesn't start from silence.
        for (int i = 0; i < numFrames; ++i)
        {
            for (int k = 0; k < std::min (numChannels, 2); ++k)
                fmLine[static_cast<std::size_t> (k)][fmWrite] = ch[k][i];
            fmWrite = (fmWrite + 1) % size;
        }
        return;
    }
    const auto morphTarget = std::clamp (c.a.load (std::memory_order_relaxed), 0.0f, 1.0f);
    const auto fmTarget = c.mode.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    const auto hzTarget = std::clamp (rootHz.load (std::memory_order_relaxed), 40.0f, 2000.0f);
    const auto ms = static_cast<float> (rate / 1000.0);

    for (int i = 0; i < numFrames; ++i)
    {
        waveMix += (mixTarget - waveMix) * 0.0015f;
        waveMorph += (morphTarget - waveMorph) * 0.002f;
        waveFm += (fmTarget - waveFm) * 0.0015f;
        waveHz += (hzTarget - waveHz) * 0.0005f;

        // The wave: between two neighbouring waves of the table, between two samples of each.
        const auto w = waveMorph * (waveCount - 1);
        const auto w0 = std::min (waveCount - 2, static_cast<int> (w));
        const auto wt = w - static_cast<float> (w0);
        const auto x = wavePhase * waveSize;
        const auto s0 = static_cast<int> (x) % waveSize;
        const auto s1 = (s0 + 1) % waveSize;
        const auto st = static_cast<float> (x - std::floor (x));
        const auto at = [this] (int wave, int s) { return waves[static_cast<std::size_t> (wave * waveSize + s)]; };
        const auto a = at (w0, s0) + (at (w0, s1) - at (w0, s0)) * st;
        const auto b = at (w0 + 1, s0) + (at (w0 + 1, s1) - at (w0 + 1, s0)) * st;
        const auto osc = a + (b - a) * wt;
        wavePhase += waveHz / rate;
        if (wavePhase >= 1.0)
            wavePhase -= 1.0;

        // FM: read the music back through a line whose length the wave moves (up to ±0.6 ms).
        const auto delay = (1.0f + 0.6f * osc) * ms;
        const auto readPos = static_cast<double> (fmWrite) - static_cast<double> (delay) + static_cast<double> (size);
        const auto i0 = static_cast<std::size_t> (readPos) % size;
        const auto frac = static_cast<float> (readPos - std::floor (readPos));
        for (int k = 0; k < std::min (numChannels, 2); ++k)
        {
            const auto ks = static_cast<std::size_t> (k);
            const auto dry = ch[k][i];
            fmLine[ks][fmWrite] = dry;
            const auto& line = fmLine[ks];
            const auto fm = line[i0] + (line[(i0 + 1) % size] - line[i0]) * frac;
            const auto ring = dry * osc;
            const auto wet = ring + (fm - ring) * waveFm;
            ch[k][i] = dry + (wet - dry) * waveMix;
        }
        fmWrite = (fmWrite + 1) % size;
    }
}

void Modular::runDrift (float* const* ch, int numChannels, int numFrames) noexcept
{
    // Tape: the sound runs through a short delay whose length wanders, so the pitch drifts (wow: slow, with a
    // random walk so it never repeats exactly) and wobbles (flutter: fast). TAPE adds saturation and a darker top.
    auto& c = controls[drift];
    const auto on = c.on.load (std::memory_order_relaxed);
    const auto amountTarget = on ? 1.0f : 0.0f;
    if (! on && driftAmount < 1.0e-4f)
    {
        driftAmount = 0;
        return;
    }
    const auto wowTarget = std::clamp (c.a.load (std::memory_order_relaxed), 0.0f, 1.0f);
    const auto flutterTarget = std::clamp (c.b.load (std::memory_order_relaxed), 0.0f, 1.0f);
    const auto tapeTarget = c.mode.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    const auto size = driftLine[0].size();
    const auto ms = static_cast<float> (rate / 1000.0);
    const auto wowStep = 0.55 / rate, flutterStep = 7.3 / rate;
    const auto tone = 1.0f - std::exp (-6.2831853f * 5200.0f / static_cast<float> (rate));
    constexpr float smooth = 0.0015f;

    for (int i = 0; i < numFrames; ++i)
    {
        driftAmount += (amountTarget - driftAmount) * smooth;
        driftWow += (wowTarget - driftWow) * smooth;
        driftFlutter += (flutterTarget - driftFlutter) * smooth;
        driftTape += (tapeTarget - driftTape) * smooth;
        if ((driftWrite & 1023u) == 0)
            wanderTarget = random();
        wander += (wanderTarget - wander) * 0.00002f;

        wowPhase += wowStep * (1.0 + 0.3 * wander);
        flutterPhase += flutterStep;
        if (wowPhase >= 1.0) wowPhase -= 1.0;
        if (flutterPhase >= 1.0) flutterPhase -= 1.0;
        const auto wow = static_cast<float> (std::sin (6.283185307 * wowPhase)) * 0.7f + wander * 0.3f;
        const auto flutter = static_cast<float> (std::sin (6.283185307 * flutterPhase));
        // Up to ±6 ms of wow (about ±40 cents) and ±0.4 ms of flutter, round a 10 ms centre.
        const auto delay = (10.0f + wow * 6.0f * driftWow * driftWow + flutter * 0.4f * driftFlutter) * ms;

        for (int k = 0; k < numChannels; ++k)
            driftLine[static_cast<std::size_t> (k)][driftWrite] = ch[k][i];
        const auto readPos = static_cast<double> (driftWrite) - static_cast<double> (delay) + static_cast<double> (size);
        const auto i0 = static_cast<std::size_t> (readPos) % size;
        const auto frac = static_cast<float> (readPos - std::floor (readPos));
        for (int k = 0; k < numChannels; ++k)
        {
            const auto& line = driftLine[static_cast<std::size_t> (k)];
            auto wet = line[i0] + (line[(i0 + 1) % size] - line[i0]) * frac;
            if (driftTape > 1.0e-4f)
            {
                auto& t = driftTone[static_cast<std::size_t> (k)];
                t += (wet - t) * tone;
                const auto taped = std::tanh (t * 1.6f) / 1.3f;
                wet += (taped - wet) * driftTape;
            }
            ch[k][i] += (wet - ch[k][i]) * driftAmount;
        }
        driftWrite = (driftWrite + 1) % size;
    }
}

void Modular::runMultiTap (float* const* ch, int numChannels, int numFrames) noexcept
{
    // Four taps, evenly spaced in beats, each quieter than the last; the last feeds back. SPREAD sends them
    // left, right, left, right.
    auto& c = controls[multitap];
    const auto amountTarget = c.on.load (std::memory_order_relaxed) ? std::clamp (c.b.load (std::memory_order_relaxed), 0.0f, 1.0f) : 0.0f;
    const auto size = tapLine[0].size();
    const auto spacingIndex = std::clamp (static_cast<int> (std::lround (c.a.load (std::memory_order_relaxed))), 0, 4);
    const auto perBeat = 60.0 / std::max (20.0f, bpm.load (std::memory_order_relaxed)) * rate;
    const auto spacingTarget = static_cast<float> (std::min (tapBeats[static_cast<std::size_t> (spacingIndex)] * perBeat,
                                                             static_cast<double> (size - 4) / 4.0));
    const auto spreadTarget = c.mode.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    const bool idle = amountTarget <= 0.0f && tapAmount < 1.0e-4f;
    if (tapSpacing <= 0.0f)
        tapSpacing = spacingTarget;
    const auto tone = 1.0f - std::exp (-6.2831853f * 4500.0f / static_cast<float> (rate));
    constexpr std::array<float, 4> levels { 0.8f, 0.6f, 0.45f, 0.33f };

    for (int i = 0; i < numFrames; ++i)
    {
        tapAmount += (amountTarget - tapAmount) * 0.0015f;
        tapSpacing += (spacingTarget - tapSpacing) * 0.0004f;  // a slow glide when the time changes (no zipper)
        tapSpread += (spreadTarget - tapSpread) * 0.0015f;
        const auto feedback = 0.7f * tapAmount;

        std::array<float, 2> wet {};
        float last = 0;
        if (! idle)
        {
            for (std::size_t t = 0; t < 4; ++t)
            {
                const auto readPos = static_cast<double> (tapWrite) - static_cast<double> (tapSpacing) * static_cast<double> (t + 1)
                                     + static_cast<double> (size) * 4.0;
                const auto i0 = static_cast<std::size_t> (readPos) % size;
                const auto frac = static_cast<float> (readPos - std::floor (readPos));
                for (std::size_t k = 0; k < 2; ++k)
                {
                    const auto& line = tapLine[k];
                    const auto v = (line[i0] + (line[(i0 + 1) % size] - line[i0]) * frac) * levels[t];
                    // SPREAD: even taps lean left, odd taps lean right.
                    const auto side = (t % 2 == 0) == (k == 0) ? 1.0f : 1.0f - tapSpread;
                    wet[k] += v * side;
                    if (t == 3 && k == 0)
                        last = v;
                }
            }
        }
        for (std::size_t k = 0; k < 2; ++k)
        {
            const auto in = ch[std::min<int> (static_cast<int> (k), numChannels - 1)][i];
            auto& t = tapTone[k];
            t += (last / levels[3] - t) * tone;  // the feedback gets darker each time round
            tapLine[k][tapWrite] = in + t * feedback;
        }
        if (! idle)
            for (int k = 0; k < numChannels; ++k)
                ch[k][i] += wet[static_cast<std::size_t> (k)] * tapAmount;
        tapWrite = (tapWrite + 1) % size;
    }
}

void Modular::runLadder (float* const* ch, int numChannels, int numFrames) noexcept
{
    // A 24 dB ladder: four saturating one-pole stages with the last fed back (Huovilainen's model). At its
    // cutoff's top with no resonance it steps out of the way.
    auto& c = controls[ladder];
    const auto on = c.on.load (std::memory_order_relaxed);
    const auto cutTarget = std::clamp (c.a.load (std::memory_order_relaxed), 0.0f, 1.0f);
    const auto resTarget = std::clamp (c.b.load (std::memory_order_relaxed), 0.0f, 1.0f);
    const auto driveTarget = c.mode.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    const bool engaged = on && (cutTarget < 0.995f || resTarget > 0.0f || driveTarget > 0.0f);
    const auto amountTarget = engaged ? 1.0f : 0.0f;
    if (! engaged && ladderAmount < 1.0e-4f)
    {
        ladderAmount = 0;
        for (auto& s : ladderStage)
            s = {};
        ladderCut = cutTarget;
        return;
    }
    const auto nyquistSafe = static_cast<float> (rate * 0.45);
    for (int i = 0; i < numFrames; ++i)
    {
        ladderAmount += (amountTarget - ladderAmount) * 0.0015f;
        ladderCut += (cutTarget - ladderCut) * 0.002f;
        ladderRes += (resTarget - ladderRes) * 0.002f;
        ladderDrive += (driveTarget - ladderDrive) * 0.002f;
        const auto hz = std::min (nyquistSafe, 20.0f * std::pow (1000.0f, ladderCut));
        const auto g = 1.0f - std::exp (-6.2831853f * hz / static_cast<float> (rate));
        const auto k = 3.9f * ladderRes;
        const auto gain = 1.0f + 3.0f * ladderDrive;
        for (int ci = 0; ci < numChannels; ++ci)
        {
            auto& s = ladderStage[static_cast<std::size_t> (ci)];
            const auto dry = ch[ci][i];
            // Resonance thins the low end; give some of it back.
            const auto x = std::tanh ((dry * gain - k * s[3]) * 0.8f) / 0.8f;
            s[0] += g * (std::tanh (x) - std::tanh (s[0]));
            s[1] += g * (std::tanh (s[0]) - std::tanh (s[1]));
            s[2] += g * (std::tanh (s[1]) - std::tanh (s[2]));
            s[3] += g * (std::tanh (s[2]) - std::tanh (s[3]));
            const auto wet = s[3] * (1.0f + 0.5f * k) / (1.0f + 0.6f * ladderDrive);
            ch[ci][i] = dry + (wet - dry) * ladderAmount;
        }
    }
}

} // namespace dss1::engine
