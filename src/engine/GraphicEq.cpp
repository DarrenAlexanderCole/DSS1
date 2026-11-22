#include "GraphicEq.h"

#include <algorithm>
#include <cmath>

namespace dss1::engine
{

void GraphicEq::prepare (double sampleRate)
{
    rate = sampleRate;
    for (auto& channel : filters)
        for (std::size_t b = 0; b < bands; ++b)
        {
            channel[b].set (Biquad::Kind::peak, rate, centres[b], q, current[b]);
            channel[b].reset();
        }
}

double GraphicEq::responseDb (double hz) const noexcept
{
    double total = outputDb.load();
    Biquad f;
    for (std::size_t b = 0; b < bands; ++b)
    {
        const auto g = std::clamp (gainDb[b].load(), -range, range);
        if (g == 0.0f)
            continue;
        f.set (Biquad::Kind::peak, 48000.0, centres[b], q, g);
        total += f.magnitudeDb (48000.0, hz);
    }
    return total;
}

void GraphicEq::process (float* const* ch, int numChannels, int numFrames) noexcept
{
    numChannels = std::min (numChannels, 2);
    // New settings move the filters a step at a time, block by block, so dragging a fader never clicks.
    flat = true;
    for (std::size_t b = 0; b < bands; ++b)
    {
        const auto target = std::clamp (gainDb[b].load (std::memory_order_relaxed), -range, range);
        if (target != current[b])
        {
            const auto step = target - current[b];
            current[b] = std::abs (step) < 0.25f ? target : current[b] + step * 0.35f;
            for (auto& channel : filters)
                channel[b].set (Biquad::Kind::peak, rate, centres[b], q, current[b]);
        }
        if (current[b] != 0.0f)
            flat = false;
    }
    const auto outputTarget = std::clamp (outputDb.load (std::memory_order_relaxed), -12.0f, 12.0f);
    const auto onTarget = on.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    // Flat, with no output gain: nothing to do (flat bands are exactly transparent).
    if (flat && outputTarget == 0.0f && std::abs (currentOutput) < 1.0e-4f)
    {
        currentOutput = 0;
        mix = onTarget;
        return;
    }
    for (int i = 0; i < numFrames; ++i)
    {
        currentOutput += (outputTarget - currentOutput) * 0.001f;
        mix += (onTarget - mix) * 0.0015f;
        const auto gain = std::pow (10.0f, currentOutput * mix / 20.0f);
        for (int k = 0; k < numChannels; ++k)
        {
            const auto dry = ch[k][i];
            auto wet = dry;
            for (auto& f : filters[static_cast<std::size_t> (k)])
                wet = f.process (wet);
            ch[k][i] = (dry + (wet - dry) * mix) * gain;
        }
    }
}

} // namespace dss1::engine
