#pragma once

#include "Biquad.h"

#include <array>
#include <atomic>

namespace dss1::engine
{

/// 20BAND$: a 20-band graphic EQ at the very start of the chain (before STEP, the DELUXE rack and the
/// effects). Half-octave bands from 25 Hz to 18 kHz, ±12 dB each, and an output gain. Flat (or switched off) it
/// leaves the sound untouched.
class GraphicEq
{
public:
    static constexpr int bands = 20;
    static constexpr std::array<double, bands> centres { 25, 35, 50, 71, 100, 141, 200, 283, 400, 566,
                                                         800, 1130, 1600, 2260, 3200, 4500, 6400, 9000, 12700, 18000 };
    static constexpr double q = 2.1;  // half an octave wide
    static constexpr float range = 12.0f;

    // Message thread:
    std::array<std::atomic<float>, bands> gainDb {};
    std::atomic<float> outputDb { 0.0f };
    std::atomic<bool> on { true };

    /// The whole curve's gain at `hz`, dB, as the settings stand (for drawing it).
    double responseDb (double hz) const noexcept;

    // Audio thread:
    void prepare (double sampleRate);
    void process (float* const* channels, int numChannels, int numFrames) noexcept;

private:
    double rate = 44100.0;
    std::array<float, bands> current {};
    float currentOutput = 0;
    float mix = 0;  // 0: bypassed … 1: in
    std::array<std::array<Biquad, bands>, 2> filters;
    bool flat = true;
};

} // namespace dss1::engine
