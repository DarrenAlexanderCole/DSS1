#pragma once

#include <array>
#include <atomic>
#include <vector>

namespace dss1::engine
{

/// The Listen & Design effects, in order: filter, compressor (with an optional crusher in front of it), echo,
/// reverb. Stereo, real time, no allocation on
/// the audio thread. Parameters are atomics set from the message thread (knobs, the XY pad) and smoothed on the
/// audio thread so moves never click. With every effect off the chain passes audio through untouched.
class Effects
{
public:
    // --- Message thread: parameters ------------------------------------------------------------------------

    /// Filter: -1 … 0 low-pass (closing towards -1), 0 … +1 high-pass (closing towards +1). 0 is open.
    std::atomic<float> filter { 0.0f };
    std::atomic<bool> filterOn { false };

    /// Echo: a tempo-synced delay. `echoBeats` is the delay in beats at `bpm`; feedback 0 … 0.95; mix 0 … 1.
    std::atomic<float> echoBeats { 0.75f };
    std::atomic<float> echoFeedback { 0.45f };
    std::atomic<float> echoMix { 0.35f };
    std::atomic<bool> echoOn { false };

    /// Reverb: size 0 … 1 (small room to hall), mix 0 … 1.
    std::atomic<float> reverbSize { 0.6f };
    std::atomic<float> reverbMix { 0.3f };
    std::atomic<bool> reverbOn { false };

    /// Compressor, modelled on four classic designs:
    /// - OPTO (LA-2A style): slow, smooth, program-dependent release, soft knee.
    /// - VCA (SSL bus style): clean and punchy, RMS detector, firm knee.
    /// - TUBE (vari-mu, Fairchild style): ratio rises with level, wide knee, warm saturation.
    /// - FET (1176 style): very fast, aggressive, a little bite.
    enum class CompType { opto = 0, vca = 1, tube = 2, fet = 3 };
    std::atomic<int> compType { static_cast<int> (CompType::vca) };
    std::atomic<bool> compOn { false };
    std::atomic<float> compAmount { 0.4f };   ///< 0 … 1: how hard it squeezes (threshold 0 … -36 dB)
    std::atomic<float> compMix { 1.0f };      ///< 0 … 1: dry/compressed (parallel compression below 1)
    std::atomic<bool> compFastAttack { false }, compFastRelease { false }, compHighRatio { false }, compSidechainHpf { false };
    /// The CRUSH switch: 12-bit, a third of the rate, like an old drum sampler, in front of the compressor.
    std::atomic<bool> crushOn { false };
    /// Gain reduction right now, in dB (0 or negative), for the meter.
    std::atomic<float> gainReduction { 0.0f };

    /// The tempo echo syncs to (the playing track's BPM at the deck's tempo).
    std::atomic<float> bpm { 120.0f };

    // --- Audio thread --------------------------------------------------------------------------------------

    void prepare (double sampleRate, int maxBlock);
    /// Processes `channels` (1 or 2, more are left alone) in place.
    void process (float* const* channels, int numChannels, int numFrames) noexcept;
    /// Clears echo and reverb tails.
    void reset() noexcept;

private:
    struct Smoothed
    {
        float value = 0, target = 0, step = 0.002f;
        float next() noexcept { value += (target - value) * step; return value; }
    };

    struct Svf  // Zavalishin's topology-preserving state-variable filter
    {
        float ic1 = 0, ic2 = 0;
        void reset() noexcept { ic1 = ic2 = 0; }
    };

    struct Comb
    {
        std::vector<float> buffer;
        std::size_t index = 0;
        float store = 0;
    };
    struct Allpass
    {
        std::vector<float> buffer;
        std::size_t index = 0;
    };

    double rate = 44100.0;
    Smoothed filterAmount, filterWet, echoTime, echoWet, echoFb, reverbWet, reverbRoom, crushWet, compWet;
    float compEnvelope = 0;      // gain reduction, dB
    float compRms = 0;           // detector (VCA)
    float compSlow = 0;          // opto: how long it's been working, for the program-dependent release
    std::array<float, 2> sidechainState {};
    std::array<Svf, 2> lowpass, highpass;

    std::array<std::vector<float>, 2> echoLine;
    std::size_t echoWrite = 0;
    std::array<float, 2> echoTone {};

    std::array<std::array<Comb, 8>, 2> combs;
    std::array<std::array<Allpass, 4>, 2> allpasses;

    std::array<float, 2> crushHeld {};
    int crushCounter = 0;
};

} // namespace dss1::engine
