#include "core/Hits.h"

#include <algorithm>
#include <cmath>

namespace dss1
{

namespace
{
    constexpr std::size_t hop = 128;            // energy is measured every 128 frames (~3 ms)
    constexpr std::size_t window = 512;          // over 512 frames (~12 ms)
    constexpr std::size_t history = 16;          // compared with the ~45 ms before it
    constexpr std::size_t envelopeBlock = 32;    // attack is found to within 32 frames (< 1 ms)

    /// Energy of each hop.
    std::vector<double> energies (const std::vector<float>& mono)
    {
        std::vector<double> e;
        if (mono.size() < window)
            return e;
        e.reserve ((mono.size() - window) / hop + 1);
        for (std::size_t start = 0; start + window <= mono.size(); start += hop)
        {
            double sum = 0;
            for (std::size_t i = start; i < start + window; ++i)
                sum += static_cast<double> (mono[i]) * static_cast<double> (mono[i]);
            e.push_back (sum / static_cast<double> (window));
        }
        return e;
    }

    /// Within [from, to), the start of the 32-frame block where the level climbs fastest: the attack.
    Frame attackIn (const std::vector<float>& mono, std::size_t from, std::size_t to)
    {
        to = std::min (to, mono.size());
        std::vector<float> envelope;
        for (std::size_t b = from; b + envelopeBlock <= to; b += envelopeBlock)
        {
            float peak = 0;
            for (std::size_t i = b; i < b + envelopeBlock; ++i)
                peak = std::max (peak, std::abs (mono[i]));
            envelope.push_back (peak);
        }
        if (envelope.size() < 2)
            return static_cast<Frame> (from);

        std::size_t best = 1;
        float bestRise = envelope[1] - envelope[0];
        for (std::size_t k = 2; k < envelope.size(); ++k)
            if (const auto rise = envelope[k] - envelope[k - 1]; rise > bestRise)
            {
                bestRise = rise;
                best = k;
            }

        // Then the first frame in that block that reaches half way up the jump.
        const auto blockStart = from + best * envelopeBlock;
        const auto threshold = envelope[best - 1] + 0.5f * (envelope[best] - envelope[best - 1]);
        for (std::size_t i = blockStart; i < blockStart + envelopeBlock; ++i)
            if (std::abs (mono[i]) >= threshold)
                return static_cast<Frame> (i);
        return static_cast<Frame> (blockStart);
    }
} // namespace

std::vector<float> mixDown (const std::vector<std::vector<float>>& channels)
{
    if (channels.empty())
        return {};
    if (channels.size() == 1)
        return channels.front();
    std::vector<float> out (channels.front().size(), 0.0f);
    const auto scale = 1.0f / static_cast<float> (channels.size());
    for (const auto& c : channels)
        for (std::size_t i = 0; i < out.size() && i < c.size(); ++i)
            out[i] += c[i] * scale;
    return out;
}

std::vector<Frame> findHits (const std::vector<float>& mono, double sampleRate, std::size_t count,
                             Frame fromFrame, Frame toFrame)
{
    std::vector<Frame> hits;
    const auto e = energies (mono);
    if (e.size() <= history || count == 0 || sampleRate <= 0)
        return hits;

    const auto first = std::max<std::size_t> (history, static_cast<std::size_t> (std::max<Frame> (0, fromFrame)) / hop);
    const auto last = std::min (e.size(), static_cast<std::size_t> (std::max<Frame> (0, toFrame)) / hop);
    if (last <= first + count)
        return hits;

    // Louder-than-before strength of each hop: how far its energy jumps above the recent past.
    std::vector<double> strength (e.size(), 0.0);
    double loudest = 0;
    for (std::size_t k = history; k < e.size(); ++k)
    {
        double before = 0;
        for (std::size_t j = k - history; j < k; ++j)
            before += e[j];
        before /= static_cast<double> (history);
        strength[k] = e[k] - before;
        loudest = std::max (loudest, e[k]);
    }
    const auto clearHit = loudest * 0.05;  // a hit must jump by at least 5% of the track's loudest energy

    const auto span = (last - first) / count;
    for (std::size_t s = 0; s < count; ++s)
    {
        const auto a = first + s * span, b = first + (s + 1) * span;
        const auto k = static_cast<std::size_t> (std::max_element (strength.begin() + static_cast<std::ptrdiff_t> (a),
                                                                   strength.begin() + static_cast<std::ptrdiff_t> (b))
                                                 - strength.begin());
        if (strength[k] <= clearHit || strength[k] < 1.0e-10)
            continue;
        // The energy window that jumped covers [k·hop, k·hop + window); the attack is in there or just before.
        const auto from = k * hop >= window ? k * hop - window / 2 : 0;
        hits.push_back (attackIn (mono, from, k * hop + window));
    }
    return hits;
}

} // namespace dss1
