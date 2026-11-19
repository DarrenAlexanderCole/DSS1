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
    for (auto* s : { &filterAmount, &filterWet, &echoWet, &echoFb, &reverbWet, &reverbRoom, &crushWet, &compWet })
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
    compEnvelope = compRms = compSlow = 0;
    sidechainState = {};
    gainReduction.store (0.0f);
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
    crushWet.target = crushOn.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    const auto levels = std::pow (2.0f, 11.0f);  // 12 bit
    const int hold = 3;                           // a third of the rate

    // Compressor settings for this block, by type.
    compWet.target = compOn.load (std::memory_order_relaxed) ? std::clamp (compMix.load (std::memory_order_relaxed), 0.0f, 1.0f) : 0.0f;
    const auto type = static_cast<CompType> (std::clamp (compType.load (std::memory_order_relaxed), 0, 3));
    const bool fastA = compFastAttack.load (std::memory_order_relaxed), fastR = compFastRelease.load (std::memory_order_relaxed);
    const bool high = compHighRatio.load (std::memory_order_relaxed);
    struct Character
    {
        float attackMs, releaseMs, ratio, kneeDb, drive;
    };
    Character ch {};
    switch (type)
    {
        case CompType::opto: ch = { fastA ? 3.0f : 10.0f, fastR ? 150.0f : 500.0f, high ? 6.0f : 3.0f, 10.0f, 0.0f }; break;
        case CompType::vca:  ch = { fastA ? 0.3f : 3.0f, fastR ? 40.0f : 120.0f, high ? 10.0f : 4.0f, 2.0f, 0.0f }; break;
        case CompType::tube: ch = { fastA ? 8.0f : 20.0f, fastR ? 150.0f : 400.0f, high ? 4.0f : 2.0f, 18.0f, 0.5f }; break;
        case CompType::fet:  ch = { fastA ? 0.05f : 0.8f, fastR ? 30.0f : 80.0f, high ? 20.0f : 4.0f, 1.0f, 0.25f }; break;
    }
    const auto thresholdDb = -36.0f * std::clamp (compAmount.load (std::memory_order_relaxed), 0.0f, 1.0f);
    const auto attackCoeff = std::exp (-1.0f / (0.001f * ch.attackMs * static_cast<float> (rate)));
    const auto releaseCoeff = std::exp (-1.0f / (0.001f * ch.releaseMs * static_cast<float> (rate)));
    const auto rmsCoeff = std::exp (-1.0f / (0.010f * static_cast<float> (rate)));
    const auto hpfCoeff = std::exp (-2.0f * pi * 100.0f / static_cast<float> (rate));
    const bool hpf = compSidechainHpf.load (std::memory_order_relaxed);
    // Make-up gain: about half of what the threshold and ratio take away at full scale.
    const auto makeupDb = -thresholdDb * (1.0f - 1.0f / ch.ratio) * 0.5f;
    float deepest = 0;

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

        // --- Crush switch: hold samples (lower rate), then quantise (fewer bits).
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

        // --- Compressor: a stereo-linked detector, a soft-knee gain computer, attack/release in dB, make-up,
        // and each type's colour.
        const auto kWet = compWet.next();
        if (kWet > 1e-4f)
        {
            float detect[2] { out[0], out[1] };
            if (hpf)  // ignore the low end, so the kick doesn't pump everything
                for (std::size_t c = 0; c < 2; ++c)
                {
                    sidechainState[c] = hpfCoeff * sidechainState[c] + (1.0f - hpfCoeff) * detect[c];
                    detect[c] -= sidechainState[c];
                }
            auto level = std::max (std::abs (detect[0]), std::abs (detect[1]));
            if (type == CompType::vca)
            {
                compRms = rmsCoeff * compRms + (1.0f - rmsCoeff) * level * level;
                level = std::sqrt (compRms) * 1.414f;
            }
            const auto levelDb = 20.0f * std::log10 (std::max (level, 1e-6f));
            auto ratio = ch.ratio;
            if (type == CompType::tube)  // vari-mu: the ratio rises the harder it's pushed
                ratio = std::min (ch.ratio * 3.0f, ch.ratio + std::max (0.0f, levelDb - thresholdDb) * 0.15f);
            const auto over = levelDb - thresholdDb;
            float target = 0;
            if (2.0f * over < -ch.kneeDb)
                target = 0;
            else if (2.0f * std::abs (over) <= ch.kneeDb)
                target = (1.0f / ratio - 1.0f) * (over + ch.kneeDb / 2.0f) * (over + ch.kneeDb / 2.0f) / (2.0f * std::max (ch.kneeDb, 0.01f));
            else
                target = (1.0f / ratio - 1.0f) * over;
            auto release = releaseCoeff;
            if (type == CompType::opto)
            {
                // The light cell: the longer it's been squeezing, the slower it lets go.
                compSlow = 0.9999f * compSlow + 0.0001f * std::min (0.0f, compEnvelope);
                release = std::pow (releaseCoeff, 1.0f / (1.0f + std::abs (compSlow) * 0.4f));
            }
            compEnvelope = target < compEnvelope ? attackCoeff * compEnvelope + (1.0f - attackCoeff) * target
                                                 : release * compEnvelope + (1.0f - release) * target;
            deepest = std::min (deepest, compEnvelope);
            const auto gain = std::pow (10.0f, (compEnvelope + makeupDb) / 20.0f);
            for (std::size_t c = 0; c < 2; ++c)
            {
                auto squeezed = out[c] * gain;
                if (ch.drive > 0)
                {
                    // Tube: warm, even harmonics (a little asymmetry). FET: a touch of odd-harmonic bite.
                    const auto d = 1.0f + ch.drive * 2.0f;
                    squeezed = type == CompType::tube ? (std::tanh (d * squeezed + 0.1f) - std::tanh (0.1f)) / d
                                                      : std::tanh (d * squeezed) / d;
                }
                out[c] += (squeezed - out[c]) * kWet;
            }
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
    gainReduction.store (compWet.value > 1e-4f ? deepest : 0.0f, std::memory_order_relaxed);
}

} // namespace dss1::engine
