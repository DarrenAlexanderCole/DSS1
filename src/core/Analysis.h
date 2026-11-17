#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dss1::analysis
{

// Track analysis: tempo, key and loudness, from decoded samples. No JUCE, no OS libraries, so results are the
// same on every platform. These are first versions, tuned on synthetic tests; they'll be checked against real
// crates and improved.

/// Estimated tempo in BPM, folded into [minBpm, maxBpm) by halving or doubling. nullopt for music without a
/// steady pulse (or silence).
std::optional<double> detectBpm (const std::vector<float>& mono, double sampleRate, double minBpm = 70.0, double maxBpm = 180.0);

struct Key
{
    int tonic = 0;        ///< 0 = C, 1 = C#, … 11 = B
    bool minor = false;
    double confidence = 0; ///< correlation with the best key profile, -1 … 1

    std::string name() const;     ///< "F minor", "A# major"
    std::string camelot() const;  ///< "4A", "6B"
};

/// Estimated musical key. nullopt for silence or noise with no tonal centre.
std::optional<Key> detectKey (const std::vector<float>& mono, double sampleRate);

/// Integrated loudness in LUFS (ITU-R BS.1770 / EBU R128: K-weighting, 400 ms blocks, absolute and relative
/// gates). `channels` holds one vector per channel. nullopt for silence.
std::optional<double> integratedLoudness (const std::vector<std::vector<float>>& channels, double sampleRate);

/// In-place complex FFT (radix 2). `real` and `imag` must have the same power-of-two size.
void fft (std::vector<double>& real, std::vector<double>& imag);

} // namespace dss1::analysis
