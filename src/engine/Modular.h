#pragma once

#include <array>
#include <atomic>
#include <vector>

namespace dss1::engine
{

/// The DELUXE rack's sound: eight slots of remix modules, run in slot order, in front of the classic effects.
/// Every module starts off, and with every module off the audio passes through untouched.
///
/// Built so far: STUTTER and SLICER. The others pass the sound through until they're built.
///
/// Both lock to the music's beat: the app tells the rack where the beats are in the playing track (setSync);
/// with nothing playing (pads, a chop preview) the rack keeps its own beat at `bpm`.
class Modular
{
public:
    /// The modules, in the order the rack shows them at first.
    enum Kind { stutter = 0, slicer, noise, wavetable, drift, multitap, ladder, empty, kinds };
    static constexpr int slots = 8;

    /// One module's controls: its power switch, its mode switch, and its two knobs (in the knob's own units,
    /// as the rack shows them):
    /// - STUTTER: a = repeat length (0 … 4: 1/32, 1/16, 1/8, 1/4, 1/2 note), b = mix 0 … 1, mode = HOLD.
    /// - SLICER: a = pattern (0 … 7), b = depth 0 … 1, mode = TRIPLET.
    struct Controls
    {
        std::atomic<bool> on { false }, mode { false };
        std::atomic<float> a { 0.0f }, b { 0.0f };
    };

    // --- Message thread ----------------------------------------------------------------------------------------

    std::array<Controls, kinds> controls;
    /// Which module sits in each slot (a Kind).
    std::array<std::atomic<int>, slots> order { { { stutter }, { slicer }, { noise }, { wavetable }, { drift }, { multitap }, { ladder }, { empty } } };
    /// The tempo, for keeping the beat when there's no track playing.
    std::atomic<float> bpm { 120.0f };

    /// Where the beats are: `anchorFrame` (a frame of the playing track) is on beat `beatInBar` (0-based) of a
    /// bar of `beatsPerBar`, and a beat lasts `beatFrames` frames there. Call often while a track plays (the
    /// tempo can change along the track); pass beatFrames <= 0 when there's no grid.
    void setSync (double anchorFrame, double beatFrames, int beatInBar, int beatsPerBar) noexcept;

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

    void runStutter (float* const* ch, int numChannels, int numFrames, const double* beats) noexcept;
    void runSlicer (float* const* ch, int numChannels, int numFrames, const double* beats, double beatsPerBar) noexcept;
    std::vector<double> beatBuffer;
};

} // namespace dss1::engine
