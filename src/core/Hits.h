#pragma once

#include "core/BeatGrid.h"

#include <vector>

namespace dss1
{

// Finds sharp hits (kicks, snares, claps) in audio and the frame where each one starts. Used by the timing
// test: cues placed exactly on hits make any offset between DSS1 and another app easy to see by eye.
// Later this becomes part of beat detection.

/// The start of the strongest hit inside each of `count` equal stretches of [fromFrame, toFrame), in order.
/// A stretch with no clear hit is skipped, so fewer than `count` frames may come back.
/// `mono` holds one channel (or a mix-down) of the track.
std::vector<Frame> findHits (const std::vector<float>& mono, double sampleRate, std::size_t count,
                             Frame fromFrame, Frame toFrame);

/// Mixes channels down to one.
std::vector<float> mixDown (const std::vector<std::vector<float>>& channels);

} // namespace dss1
