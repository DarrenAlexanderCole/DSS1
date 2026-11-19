#pragma once

#include "io/AudioDecoder.h"

#include <cstdint>
#include <vector>

namespace dss1::engine
{

/// Helpers for chopping a sample into slices.

/// The nearest point to `at` (within `radius` frames) where the sound crosses zero, so a slice that starts there
/// doesn't click. Returns `at` when there's no crossing that close.
std::int64_t nearestZeroCrossing (const io::DecodedAudio&, std::int64_t at, std::int64_t radius);

/// Where hits start between `from` and `to`: up to `maxCount` of the strongest onsets, at least `minGapSeconds`
/// apart, in time order. Each lands just before its hit (on a zero crossing), the way you'd place a chop by hand.
std::vector<std::int64_t> findTransients (const io::DecodedAudio&, std::int64_t from, std::int64_t to, int maxCount = 16,
                                          double minGapSeconds = 0.07);

} // namespace dss1::engine
