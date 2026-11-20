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
    /// Echo switches: PING PONG bounces the repeats left and right; TAPE wobbles and saturates them like a
    /// worn tape echo (and lets them run away into a dub howl at high feedback); DUCK keeps the repeats down
    /// while the music plays and lets them bloom in the gaps; FREEZE stops taking new sound and loops what's in
    /// the echo, forever, until it's turned off.
    std::atomic<bool> echoPingPong { false }, echoTape { false }, echoDuck { false }, echoFreeze { false };

    /// Reverb: size 0 … 1 (small room to hall), mix 0 … 1.
    std::atomic<float> reverbSize { 0.6f };
    std::atomic<float> reverbMix { 0.3f };
    std::atomic<bool> reverbOn { false };
    /// Reverb shaping: GATED cuts the tail off sharply when the hit ends (the 80s gated snare); SHIMMER feeds
    /// the tail back an octave up, so it rises into a glowing halo.
    std::atomic<bool> reverbGated { false }, reverbShimmer { false };

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
    /// Opto attack and release, ms (the other types have their own).
    std::atomic<float> compAttackMs { 10.0f }, compReleaseMs { 400.0f };
    /// The 3-band EQ in front of the compressor (part of the COMP module, on with it): low shelf 120 Hz, mid
    /// bell 1 kHz, high shelf 6 kHz, each -12 … +12 dB.
    std::atomic<float> eqLowDb { 0.0f }, eqMidDb { 0.0f }, eqHighDb { 0.0f };
    std::atomic<bool> compFastAttack { false }, compFastRelease { false }, compHighRatio { false }, compSidechainHpf { false };
    /// The CRUSH switch: 12-bit, a third of the rate, like an old drum sampler, in front of the compressor.
    std::atomic<bool> crushOn { false };
    /// COMP switches (with COMP on): DRIVE pushes the EQ'd sound hard into an asymmetric saturator before the
    /// squeeze (grit, then fuzz); PUNCH is a transient shaper after it (hits jump out, up to +12 dB);
    /// SQUASH turns the compressor into a brick (20:1, very fast, 12 dB deeper): pumping, breathing, loud.
    std::atomic<bool> compDrive { false }, compPunch { false }, compSquash { false };
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
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<float, 2> z1 {}, z2 {};
        float process (float x, std::size_t c) noexcept
        {
            const auto y = b0 * x + z1[c];
            z1[c] = b1 * x - a1 * y + z2[c];
            z2[c] = b2 * x - a2 * y;
            return y;
        }
    };
    std::array<Biquad, 3> eq;
    std::array<float, 3> eqSet { 999.0f, 999.0f, 999.0f };
    void updateEq();
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

    // DRIVE: a DC blocker after the asymmetric saturator (last input, last output, per channel).
    std::array<float, 2> driveX {}, driveY {};
    // PUNCH: fast and slow envelopes of the level.
    float punchFast = 0, punchSlow = 0;
    // TAPE echo: wow and flutter phases.
    double wowPhase = 0, flutterPhase = 0;
    // DUCK: the dry level the echo ducks under.
    float duckEnv = 0;
    // GATED reverb: input envelope, how long the gate stays open, and its gain.
    float gateEnv = 0, gateGain = 0;
    int gateHold = 0;
    // SHIMMER: an octave-up pitch shifter (two crossfaded taps reading twice as fast) on the reverb's return.
    std::array<std::vector<float>, 2> shimmerLine;
    std::size_t shimmerWrite = 0;
    double shimmerPhase = 0;
    std::array<float, 2> shimmerBack {};
    Smoothed gateMix, shimmerMix, freezeMix;
};

} // namespace dss1::engine
