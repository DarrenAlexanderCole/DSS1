#include "engine/Effects.h"

#include <algorithm>
#include <cmath>

namespace dss1::engine
{

namespace
{
    constexpr float pi = 3.14159265358979f;

    // Freeverb's tunings (at 44.1 kHz), the right channel offset by 23 samples for width.
    constexpr int combTunings[8] { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    constexpr int allpassTunings[4] { 556, 441, 341, 225 };
    constexpr int stereoSpread = 23;

    /// One TPT state-variable filter step; returns {low, high}.
    inline std::pair<float, float> svf (float& ic1, float& ic2, float x, float g, float k) noexcept
    {
        const auto a1 = 1.0f / (1.0f + g * (g + k));
        const auto a2 = g * a1;
        const auto a3 = g * a2;
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        const auto low = v2;
        const auto band = v1;
        const auto high = x - k * band - low;
        return { low, high };
    }
}

void Effects::prepare (double sampleRate, int)
{
    rate = sampleRate > 0 ? sampleRate : 44100.0;
    const auto scale = rate / 44100.0;
    for (auto& line : echoLine)
        line.assign (static_cast<std::size_t> (rate * 6.0) + 1, 0.0f);  // up to 6 s of delay
    for (std::size_t c = 0; c < 2; ++c)
    {
        for (std::size_t i = 0; i < 8; ++i)
            combs[c][i].buffer.assign (static_cast<std::size_t> ((combTunings[i] + static_cast<int> (c) * stereoSpread) * scale) + 1, 0.0f);
        for (std::size_t i = 0; i < 4; ++i)
            allpasses[c][i].buffer.assign (static_cast<std::size_t> ((allpassTunings[i] + static_cast<int> (c) * stereoSpread) * scale) + 1, 0.0f);
    }
    // Smoothing: about 20 ms for levels and the filter, 150 ms for delay time (a tape-like glide).
    const auto per = [this] (double seconds) { return static_cast<float> (1.0 - std::exp (-1.0 / (seconds * rate))); };
    for (auto* s : { &filterAmount, &filterWet, &echoWet, &echoFb, &reverbWet, &reverbRoom, &crushWet })
        s->step = per (0.02);
    echoTime.step = per (0.15);
    echoTime.value = echoTime.target = static_cast<float> (60.0 / 120.0 * 0.75 * rate);
    reset();
}

void Effects::reset() noexcept
{
    for (auto& f : lowpass)
        f.reset();
    for (auto& f : highpass)
        f.reset();
    for (auto& line : echoLine)
        std::fill (line.begin(), line.end(), 0.0f);
    echoTone = {};
    for (auto& side : combs)
        for (auto& c : side)
        {
            std::fill (c.buffer.begin(), c.buffer.end(), 0.0f);
            c.store = 0;
        }
    for (auto& side : allpasses)
        for (auto& a : side)
            std::fill (a.buffer.begin(), a.buffer.end(), 0.0f);
    crushHeld = {};
    crushCounter = 0;
}

void Effects::process (float* const* channels, int numChannels, int numFrames) noexcept
{
    if (numChannels <= 0 || numFrames <= 0 || echoLine[0].empty())
        return;
    const auto stereo = numChannels >= 2;
    float* left = channels[0];
    float* right = stereo ? channels[1] : channels[0];

    // Targets for this block.
    filterAmount.target = std::clamp (filter.load (std::memory_order_relaxed), -1.0f, 1.0f);
    filterWet.target = filterOn.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    const auto beats = std::clamp (echoBeats.load (std::memory_order_relaxed), 0.0625f, 4.0f);
    const auto tempo = std::clamp (bpm.load (std::memory_order_relaxed), 40.0f, 300.0f);
    echoTime.target = std::min (static_cast<float> (beats * 60.0 / tempo * rate), static_cast<float> (echoLine[0].size() - 2));
    echoWet.target = echoOn.load (std::memory_order_relaxed) ? std::clamp (echoMix.load (std::memory_order_relaxed), 0.0f, 1.0f) : 0.0f;
    echoFb.target = std::clamp (echoFeedback.load (std::memory_order_relaxed), 0.0f, 0.95f);
    reverbWet.target = reverbOn.load (std::memory_order_relaxed) ? std::clamp (reverbMix.load (std::memory_order_relaxed), 0.0f, 1.0f) : 0.0f;
    reverbRoom.target = 0.7f + 0.28f * std::clamp (reverbSize.load (std::memory_order_relaxed), 0.0f, 1.0f);
    crushWet.target = crushOn.load (std::memory_order_relaxed) ? std::clamp (crushMix.load (std::memory_order_relaxed), 0.0f, 1.0f) : 0.0f;
    const auto bits = std::clamp (crushBits.load (std::memory_order_relaxed), 2.0f, 16.0f);
    const auto levels = std::pow (2.0f, bits - 1.0f);
    const auto hold = std::clamp (static_cast<int> (crushRate.load (std::memory_order_relaxed)), 1, 32);

    const auto lineSize = echoLine[0].size();
    const auto nyquistGuard = static_cast<float> (rate * 0.45);

    for (int i = 0; i < numFrames; ++i)
    {
        float in[2] { left[i], right[i] };
        float out[2] { in[0], in[1] };

        // --- Filter: low-pass below centre, high-pass above. Off fades out through filterWet.
        const auto amount = filterAmount.next();
        const auto fWet = filterWet.next();
        if (fWet > 1e-4f)
        {
            const auto closeness = std::abs (amount);
            float cutoff;
            if (amount < 0)
                cutoff = 20000.0f * std::pow (60.0f / 20000.0f, closeness);  // 20 kHz → 60 Hz
            else
                cutoff = 20.0f * std::pow (9000.0f / 20.0f, closeness);      // 20 Hz → 9 kHz
            cutoff = std::min (cutoff, nyquistGuard);
            const auto g = std::tan (pi * cutoff / static_cast<float> (rate));
            const auto k = 2.0f - 1.3f * closeness;  // a little resonance as it closes
            for (std::size_t c = 0; c < 2; ++c)
            {
                const auto lp = svf (lowpass[c].ic1, lowpass[c].ic2, out[c], g, k).first;
                const auto hp = svf (highpass[c].ic1, highpass[c].ic2, out[c], g, k).second;
                const auto filtered = amount < 0 ? lp : hp;
                out[c] += (filtered - out[c]) * fWet;
            }
        }

        // --- Crusher: hold samples (lower rate), then quantise (fewer bits).
        const auto cWet = crushWet.next();
        if (cWet > 1e-4f)
        {
            if (crushCounter == 0)
                for (std::size_t c = 0; c < 2; ++c)
                    crushHeld[c] = std::round (out[c] * levels) / levels;
            crushCounter = (crushCounter + 1) % hold;
            for (std::size_t c = 0; c < 2; ++c)
                out[c] += (crushHeld[c] - out[c]) * cWet;
        }

        // --- Echo: read behind the write head (linear interpolation), darken the repeats, feed back.
        const auto delay = echoTime.next();
        const auto eWet = echoWet.next();
        const auto fb = echoFb.next();
        auto readPos = static_cast<double> (echoWrite) - static_cast<double> (delay);
        while (readPos < 0)
            readPos += static_cast<double> (lineSize);
        const auto r0 = static_cast<std::size_t> (readPos);
        const auto frac = static_cast<float> (readPos - static_cast<double> (r0));
        const auto r1 = (r0 + 1) % lineSize;
        for (std::size_t c = 0; c < 2; ++c)
        {
            const auto delayed = echoLine[c][r0] * (1.0f - frac) + echoLine[c][r1] * frac;
            echoTone[c] += (delayed - echoTone[c]) * 0.35f;  // gentle low-pass in the loop, like tape
            // Only what goes in while echo is on gets echoed; the tail keeps ringing after it's turned off.
            echoLine[c][echoWrite] = out[c] * eWet + echoTone[c] * fb;
            out[c] += echoTone[c];
        }
        echoWrite = (echoWrite + 1) % lineSize;

        // --- Reverb (Freeverb): eight damped combs in parallel, four allpasses in series.
        const auto rWet = reverbWet.next();
        const auto room = reverbRoom.next();
        if (rWet > 1e-4f)
        {
            const auto input = (out[0] + out[1]) * 0.015f * rWet;
            for (std::size_t c = 0; c < 2; ++c)
            {
                float acc = 0;
                for (auto& comb : combs[c])
                {
                    auto& v = comb.buffer[comb.index];
                    comb.store = v * 0.8f + comb.store * 0.2f;  // damping
                    acc += v;
                    v = input + comb.store * room;
                    comb.index = (comb.index + 1) % comb.buffer.size();
                }
                for (auto& ap : allpasses[c])
                {
                    auto& v = ap.buffer[ap.index];
                    const auto o = v - acc;
                    v = acc + v * 0.5f;
                    acc = o;
                    ap.index = (ap.index + 1) % ap.buffer.size();
                }
                out[c] += acc * 3.0f;
            }
        }

        left[i] = out[0];
        if (stereo)
            right[i] = out[1];
    }
}

} // namespace dss1::engine
