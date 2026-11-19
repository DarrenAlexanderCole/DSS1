#include "engine/Slicing.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace dss1::engine
{

namespace
{
    float mono (const io::DecodedAudio& a, std::size_t i)
    {
        float sum = 0;
        for (const auto& ch : a.channels)
            sum += ch[i];
        return sum / static_cast<float> (std::max<std::size_t> (1, a.channels.size()));
    }
}

std::int64_t nearestZeroCrossing (const io::DecodedAudio& a, std::int64_t at, std::int64_t radius)
{
    if (a.channels.empty() || a.channels.front().size() < 2)
        return at;
    const auto length = static_cast<std::int64_t> (a.channels.front().size());
    at = std::clamp<std::int64_t> (at, 0, length - 1);
    for (std::int64_t d = 0; d <= radius; ++d)
        for (const auto i : { at - d, at + d })
        {
            if (i < 1 || i >= length)
                continue;
            const auto prev = mono (a, static_cast<std::size_t> (i - 1)), now = mono (a, static_cast<std::size_t> (i));
            if (now == 0.0f || (prev < 0.0f) != (now < 0.0f))
                return i;
        }
    return at;
}

std::vector<std::int64_t> findTransients (const io::DecodedAudio& a, std::int64_t from, std::int64_t to, int maxCount, double minGapSeconds)
{
    std::vector<std::int64_t> result;
    if (a.channels.empty() || maxCount <= 0)
        return result;
    const auto length = static_cast<std::int64_t> (a.channels.front().size());
    from = std::clamp<std::int64_t> (from, 0, length);
    to = std::clamp<std::int64_t> (to, from, length);
    constexpr std::int64_t hop = 256;
    const auto hops = static_cast<std::size_t> ((to - from) / hop);
    if (hops < 3)
        return result;

    // Energy of the sound's changes (a first difference brings out the attack of a hit) in each hop.
    std::vector<double> energy (hops, 0.0);
    for (std::size_t h = 0; h < hops; ++h)
    {
        double e = 0;
        const auto start = static_cast<std::size_t> (from) + h * static_cast<std::size_t> (hop);
        float prev = start > 0 ? mono (a, start - 1) : 0.0f;
        for (std::size_t i = start; i < start + static_cast<std::size_t> (hop); ++i)
        {
            const auto now = mono (a, i);
            const auto d = static_cast<double> (now - prev);
            e += d * d + 0.25 * static_cast<double> (now) * static_cast<double> (now);
            prev = now;
        }
        energy[h] = std::log (1e-9 + e);
    }
    // Onset strength: how much the (log) energy rises.
    std::vector<double> rise (hops, 0.0);
    for (std::size_t h = 1; h < hops; ++h)
        rise[h] = std::max (0.0, energy[h] - energy[h - 1]);
    const auto mean = std::accumulate (rise.begin(), rise.end(), 0.0) / static_cast<double> (hops);
    double var = 0;
    for (const auto r : rise)
        var += (r - mean) * (r - mean);
    const auto threshold = mean + 0.5 * std::sqrt (var / static_cast<double> (hops));

    // Peaks, strongest first, at least the gap apart.
    const auto gapHops = std::max<std::size_t> (1, static_cast<std::size_t> (minGapSeconds * a.info.sampleRate / hop));
    std::vector<std::size_t> candidates;
    for (std::size_t h = 1; h + 1 < hops; ++h)
        if (rise[h] > threshold && rise[h] >= rise[h - 1] && rise[h] >= rise[h + 1])
            candidates.push_back (h);
    std::sort (candidates.begin(), candidates.end(), [&] (auto x, auto y) { return rise[x] > rise[y]; });
    std::vector<std::size_t> chosen;
    for (const auto h : candidates)
    {
        if (static_cast<int> (chosen.size()) >= maxCount)
            break;
        if (std::all_of (chosen.begin(), chosen.end(), [&] (auto c) { return (c > h ? c - h : h - c) >= gapHops; }))
            chosen.push_back (h);
    }
    std::sort (chosen.begin(), chosen.end());

    // Each just before its hit: back from the hop to where the sound is still quiet, then onto a zero crossing.
    for (const auto h : chosen)
    {
        const auto hopStart = from + static_cast<std::int64_t> (h) * hop;
        const auto searchFrom = std::max (from, hopStart - hop);
        float peak = 0;
        for (auto i = searchFrom; i < std::min (to, hopStart + hop); ++i)
            peak = std::max (peak, std::abs (mono (a, static_cast<std::size_t> (i))));
        auto at = hopStart;
        for (auto i = searchFrom; i < std::min (to, hopStart + hop); ++i)
            if (std::abs (mono (a, static_cast<std::size_t> (i))) > peak * 0.1f)
            {
                at = i;
                break;
            }
        at = nearestZeroCrossing (a, std::max (from, at - 1), static_cast<std::int64_t> (a.info.sampleRate * 0.002));
        result.push_back (std::min (at, std::max<std::int64_t> (from, at)));
    }
    return result;
}

} // namespace dss1::engine
