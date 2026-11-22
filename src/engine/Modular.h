#pragma once

#include "Biquad.h"

#include <array>
#include <atomic>
#include <vector>

namespace dss1::engine
{

/// The DELUXE rack's sound: eight slots of remix modules, run in slot order, in front of the classic effects.
/// Every module starts off, and with every module off the audio passes through untouched.
///
/// The modules: STUTTER, SLICER, NOISE, WAVETABLE, DRIFT, MULTI-TAP, LADDER, and MOD (an LFO that moves one
/// knob of another module, in time with the music).
///
/// Both lock to the music's beat: the app tells the rack where the beats are in the playing track (setSync);
/// with nothing playing (pads, a chop preview) the rack keeps its own beat at `bpm`.
class Modular
{
public:
    /// The modules, in the order the rack shows them at first.
    enum Kind { stutter = 0, slicer, noise, wavetable, drift, multitap, ladder, mod, kinds };
    static constexpr int slots = 8;

    /// One module's controls: its power switch, its mode switch, and its two knobs (in the knob's own units,
    /// as the rack shows them):
    /// - STUTTER: a = repeat length (0 … 4: 1/32, 1/16, 1/8, 1/4, 1/2 note), b = mix 0 … 1, mode = HOLD.
    /// - SLICER: a = pattern (0 … 7), b = depth 0 … 1, mode = TRIPLET.
    /// - NOISE: a = colour 0 … 1 (brown, pink, white), b = level 0 … 1, mode = VINYL (crackle and rumble).
    ///   It ducks under the music, so it fills the gaps.
    /// - WAVETABLE: a = morph 0 … 1 (64 waves: sine, triangle, saw, square, thin pulse), b = mix 0 … 1,
    ///   mode = FM (the wave bends the music's phase, instead of ring-modulating it). The wave plays the
    ///   track's key (`rootHz`).
    /// - DRIFT: a = wow 0 … 1 (slow pitch drift), b = flutter 0 … 1 (fast wobble), mode = TAPE (saturation and a
    ///   darker top).
    /// - MULTI-TAP: a = tap spacing (0 … 4: 1/4, 1/2, 3/4, 1, 1 1/2 beats), b = amount 0 … 1 (the taps' level and
    ///   how much feeds back), mode = SPREAD (taps alternate left and right).
    /// - LADDER: a = cutoff 0 … 1 (20 Hz … 20 kHz; 1 is open), b = resonance 0 … 1, mode = DRIVE.
    /// - MOD: a = rate (0 … 7: 1/32, 1/16, 1/8, 1/4, 1/2 note, 1, 2, 4 bars), b = depth 0 … 1. It moves the knob
    ///   `modTarget` up and down round where it's set, by up to its whole range, with the shape `modShape`.
    struct Controls
    {
        std::atomic<bool> on { false }, mode { false };
        std::atomic<float> a { 0.0f }, b { 0.0f };
    };

    // --- Message thread ----------------------------------------------------------------------------------------

    std::array<Controls, kinds> controls;
    /// Which module sits in each slot (a Kind).
    std::array<std::atomic<int>, slots> order { { { stutter }, { slicer }, { noise }, { wavetable }, { drift }, { multitap }, { ladder }, { mod } } };
    /// The tempo, for keeping the beat when there's no track playing.
    std::atomic<float> bpm { 120.0f };
    /// MOD's target: a module's knob, as kind * 2 + knob (0 = its first knob, 1 = its second); -1 for none.
    std::atomic<int> modTarget { ladder * 2 };
    enum class Shape { sine = 0, triangle, square, random };
    std::atomic<int> modShape { 0 };
    /// MOD's output right now, -1 … 1 (for its light).
    std::atomic<float> modNow { 0.0f };
    /// NOTE REPEAT's divisions of a step, and SPACE ECHO's times in beats.
    static constexpr std::array<int, 5> repeatDivisions { 1, 2, 3, 4, 8 };
    static constexpr std::array<double, 5> echoBeats { 0.25, 0.5, 0.75, 1.0, 1.5 };
    static constexpr std::array<double, 8> modBeats { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
    /// Each knob's full range, by kind and knob (what MOD's depth is a fraction of).
    static constexpr std::array<std::array<float, 2>, kinds> knobRange { { { 4, 1 }, { 7, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 4, 1 }, { 1, 1 }, { 7, 1 } } };

    /// The root of the track's key, Hz (C3 … B3), for WAVETABLE.
    std::atomic<float> rootHz { 220.0f };

    /// Where the beats are: `anchorFrame` (a frame of the playing track) is on beat `beatInBar` (0-based) of a
    /// bar of `beatsPerBar`, and a beat lasts `beatFrames` frames there. Call often while a track plays (the
    /// tempo can change along the track); pass beatFrames <= 0 when there's no grid.
    void setSync (double anchorFrame, double beatFrames, int beatInBar, int beatsPerBar) noexcept;

    /// STEP, the sample snatcher: GRAB takes the next 1, 2 or 4 beats of what's playing (from the next beat),
    /// cuts them into sixteenths, and a 16-step sequencer plays them back in time. Message thread sets these;
    /// the audio thread does the rest.
    struct Step
    {
        Step()
        {
            for (int i = 0; i < 16; ++i)
            {
                on[static_cast<std::size_t> (i)] = true;
                slice[static_cast<std::size_t> (i)] = i;
            }
        }
        enum State { empty = 0, waiting, grabbing, ready };
        std::atomic<int> beats { 1 };             // how much GRAB takes: 1, 2 or 4 beats
        std::atomic<bool> grab { false };          // request: grab (from the next beat)
        std::atomic<bool> clear { false };         // request: throw the sample away
        std::atomic<bool> running { false };       // RUN
        std::atomic<bool> replace { false };       // REPLACE: the track goes quiet while it runs
        std::atomic<float> level { 0.8f }, swing { 0.0f };
        // The steps' own effects (they touch only the steps, never the track):
        std::atomic<float> gateLength { 1.0f };    // GATE LENGTH: how much of each slice plays, 0.1 … 1
        std::atomic<float> gateDecay { 0.0f };     // GATE DECAY: 0 flat … 1 a short, percussive fall
        std::atomic<bool> reverse { false };       // REVERSE: each slice plays backwards
        std::atomic<int> repeat { 0 };             // NOTE REPEAT: 0 off, 1 1/32, 2 1/32 triplet, 3 1/64, 4 1/128
        std::atomic<int> echoTime { 2 };           // SPACE ECHO time: 0 1/16, 1 1/8, 2 3/16, 3 1/4, 4 3/8 note
        std::atomic<float> echoAmount { 0.0f };    // SPACE ECHO: its level and feedback, 0 … 1
        // The steps' own tempo and tone:
        std::atomic<bool> follow { true };         // SYNC: run in time with the track (off: at `tempo`)
        std::atomic<float> tempo { 120.0f };       // the steps' own BPM, when they don't follow the track
        std::atomic<float> low { 0.0f }, mid { 0.0f }, high { 0.0f };  // their EQ, dB (±12)
        std::array<std::atomic<bool>, 16> on;
        std::array<std::atomic<int>, 16> slice;    // which sixteenth of the sample each step plays
        // From the audio thread:
        std::atomic<int> state { empty };
        std::atomic<int> slices { 0 };             // sixteenths in the sample (4, 8 or 16)
        std::atomic<int> length { 0 };             // frames in the sample
        std::atomic<int> playing { -1 };           // the step sounding now (-1: none)
        std::atomic<float> progress { 0.0f };      // how far through grabbing, 0 … 1
    };
    Step step;
    /// The grabbed sample, a channel of it (read it only while step.state is `ready`).
    const std::vector<float>& stepSample (int channel) const noexcept { return stepBuffer[static_cast<std::size_t> (channel & 1)]; }

    // --- Audio thread ----------------------------------------------------------------------------------------

    void prepare (double sampleRate, int maxBlock);
    /// Processes `channels` (1 or 2) in place. `trackFrame` is the playing track's frame at the start of the
    /// block, and `trackPlaying` whether it's playing (if not, the rack keeps its own beat).
    void process (float* const* channels, int numChannels, int numFrames, double trackFrame, bool trackPlaying) noexcept;
    void reset() noexcept;

    /// The slicer's patterns: 16 steps a bar, the first step in the top bit.
    static constexpr std::array<unsigned, 8> slicerPatterns {
        0b1010101010101010u,  // 1: straight eighths
        0b1011101110111011u,  // 2: the trance gate
        0b1101101101101101u,  // 3: dotted, rolling across the bar
        0b1110111010101110u,  // 4: long, short, short
        0b1000100010001000u,  // 5: quarter stabs
        0b1100110011001100u,  // 6: pairs
        0b1011010110110101u,  // 7: broken
        0b1111111011101010u,  // 8: a fill into the next bar
    };
    /// STUTTER's repeat lengths, in beats (1/32, 1/16, 1/8, 1/4, 1/2 notes).
    static constexpr std::array<double, 5> stutterBeats { 0.125, 0.25, 0.5, 1.0, 2.0 };
    /// MULTI-TAP's tap spacings, in beats.
    static constexpr std::array<double, 5> tapBeats { 0.25, 0.5, 0.75, 1.0, 1.5 };

private:
    struct Sync
    {
        double anchorFrame = 0, beatFrames = 0;
        int beatInBar = 0, beatsPerBar = 4;
    };
    std::array<Sync, 2> sync {};
    std::atomic<int> syncSlot { 0 };

    double rate = 44100.0;
    double ownBeat = 0;  // the rack's own beat, when no track plays

    // STUTTER: everything heard lately; the slice being repeated (its start in the history, and its length);
    // where in the slice it's playing.
    std::array<std::vector<float>, 2> history;
    std::size_t historyWrite = 0;
    std::size_t sliceStart = 0, sliceLength = 0, slicePos = 0;
    bool repeating = false;   // past the slice's first pass (the first pass is the live sound)
    bool haveSlice = false;
    long long lastSliceIndex = -1, lastPeriodIndex = -1;
    float stutterMix = 0;

    // SLICER: the gate's level.
    float gate = 1, slicerDepth = 0;

    // NOISE: pink and brown filter states, the duck, the vinyl crackle.
    std::array<std::array<float, 7>, 2> pink {};
    std::array<float, 2> brown {}, crackleTone {}, rumble {};
    float noiseLevel = 0, noiseColour = 0.5f, noiseVinyl = 0, noiseDuck = 0;
    float crackle = 0;

    // WAVETABLE: 64 waves of `waveSize` samples, the oscillator's phase, a short line for FM.
    static constexpr int waveCount = 64, waveSize = 2048;
    std::vector<float> waves;
    double wavePhase = 0;
    float waveMix = 0, waveMorph = 0, waveFm = 0, waveHz = 220;
    std::array<std::vector<float>, 2> fmLine;
    std::size_t fmWrite = 0;

    // DRIFT: a short modulated delay (the tape path), its LFOs, a slow random walk for the wow, the TAPE tone.
    std::array<std::vector<float>, 2> driftLine;
    std::size_t driftWrite = 0;
    double wowPhase = 0, flutterPhase = 0;
    float wander = 0, wanderTarget = 0;
    unsigned noiseSeed = 22222u;
    std::array<float, 2> driftTone {};
    float driftAmount = 0, driftWow = 0, driftFlutter = 0, driftTape = 0;

    // MULTI-TAP: the delay line, the taps' level, the feedback tone.
    std::array<std::vector<float>, 2> tapLine;
    std::size_t tapWrite = 0;
    float tapAmount = 0, tapSpacing = 0, tapSpread = 0;
    std::array<float, 2> tapTone {};

    // LADDER: four one-pole stages per channel, the smoothed cutoff, resonance and drive.
    std::array<std::array<float, 4>, 2> ladderStage {};
    float ladderAmount = 0, ladderCut = 1, ladderRes = 0, ladderDrive = 0;

    void runStutter (float* const* ch, int numChannels, int numFrames, const double* beats) noexcept;
    void runSlicer (float* const* ch, int numChannels, int numFrames, const double* beats, double beatsPerBar) noexcept;
    void runNoise (float* const* ch, int numChannels, int numFrames) noexcept;
    void runWavetable (float* const* ch, int numChannels, int numFrames) noexcept;
    void runDrift (float* const* ch, int numChannels, int numFrames) noexcept;
    void runMultiTap (float* const* ch, int numChannels, int numFrames) noexcept;
    void runLadder (float* const* ch, int numChannels, int numFrames) noexcept;
    float random() noexcept;
    void runStep (float* const* ch, int numChannels, int numFrames, const double* beats, double beatsPerBar) noexcept;
    std::array<std::vector<float>, 2> stepBuffer;
    int stepWrite = 0, stepTarget = 0, stepLastBeat = -1, stepLastStep = -1;
    int voiceSlice = -1, voicePos = 0;
    // What the voice is playing, fixed when it starts (a new GRAB can't pull the sample out from under it).
    int voiceStart = 0, voiceLength = 0, voicePlayed = 0, voiceRepeat = 0, voiceFade = 1;
    double ownStepBeat = 0;  // the steps' own clock, when they don't follow the track
    std::array<std::array<Biquad, 3>, 2> stepEq;
    float eqLow = 0, eqMid = 0, eqHigh = 0;
    bool eqFlat = true;
    // SPACE ECHO: a tape delay on the steps alone, its wobble and its darkening feedback.
    std::array<std::vector<float>, 2> echoLine;
    std::size_t echoWrite = 0;
    std::array<float, 2> echoTone {};
    double echoWobble = 0;
    float echoDelay = 0, echoMix = 0;
    float stepRun = 0;
    /// A knob's value with MOD's movement added (audio thread).
    float knob (Kind kind, int which) const noexcept;
    void runMod (double barBeat, double beatsPerBar) noexcept;
    std::array<std::array<float, 2>, kinds> modOffset {};
    long long modBars = 0, modLastCycle = -1;
    double modLastBeat = 0;
    float modValue = 0, modHeld = 0;
    std::vector<double> beatBuffer;
};

} // namespace dss1::engine
