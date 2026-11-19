#pragma once

#include "io/AudioDecoder.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace dss1::engine
{

/// The pad sampler: 8 banks (A–H) of 16 pads, 128 slots. Each slot plays a slice: frames [start, end) of a
/// sample that the pads of a bank share (a chopped track or sound file).
///
/// Voices behave like an MPC's:
/// - A pad is monophonic: pressing it again fades out what it was playing and starts over.
/// - Choke (on by default): a pad also fades out every other pad playing in its bank, so slices play one at a
///   time, the way a chopped break is played back.
/// - One-shot, or hold: `release` fades a pad out (for pads that play only while held).
/// Every start, cut and end is faded (a couple of milliseconds), so nothing clicks. Mixes into the output
/// buffer, so effects and the recorder hear it.
///
/// Threads: everything before `prepare` is for the message thread; `prepare` and `render` for the audio thread.
/// Samples are swapped without locks; a replaced sample is kept alive until no voice can still be playing it.
class Sampler
{
public:
    static constexpr int banks = 8, padsPerBank = 16, slots = banks * padsPerBank;
    static constexpr int maxVoices = 16;

    Sampler();
    ~Sampler();
    Sampler (const Sampler&) = delete;
    Sampler& operator= (const Sampler&) = delete;

    // --- Message thread ------------------------------------------------------------------------------------

    /// Puts `audio` in `slot` (0 … 127), or empties it with nullptr. The pad plays frames [start, end) of it;
    /// end < 0 means to the end.
    void setSample (int slot, std::shared_ptr<const io::DecodedAudio> audio, std::int64_t start = 0, std::int64_t end = -1);
    /// Moves the slice a pad plays (takes effect on the next press).
    void setRegion (int slot, std::int64_t start, std::int64_t end);
    std::shared_ptr<const io::DecodedAudio> sample (int slot) const;
    void trigger (int slot, float gain = 1.0f);
    /// Fades the pad out (key up, for pads that play while held).
    void release (int slot);
    void stopAll();
    /// Choke: a pad cuts the other pads of its bank. On by default.
    void setChoke (bool on) noexcept { choke.store (on); }
    bool chokes() const noexcept { return choke.load(); }
    /// Frees samples that were replaced long enough ago. Call now and then (a UI timer).
    void collectGarbage();
    /// Slots playing right now (approximately; for lighting pads).
    bool isPlaying (int slot) const noexcept;
    /// Where voice `v` is: its slot and frame in the sample, or slot -1 when it's free (for drawing playheads).
    std::pair<int, double> voicePosition (int v) const noexcept;

    // --- Audio thread --------------------------------------------------------------------------------------

    void prepare (double deviceRate);
    /// Adds the playing pads into `outputs` (1 or 2 channels used).
    void render (float* const* outputs, int numChannels, int numFrames) noexcept;

    static constexpr double attackSeconds = 0.002, cutSeconds = 0.004, releaseSeconds = 0.012;

private:
    struct Voice
    {
        const io::DecodedAudio* audio = nullptr;
        double position = 0, step = 1, start = 0, end = 0, edgeFade = 1;
        float gain = 1, level = 1, fadeStep = 0;  // fadeStep > 0: fading out
        int slot = -1;
        std::uint64_t age = 0;
    };
    struct Command
    {
        enum Type : std::int16_t { trigger, release, stopAll } type = trigger;
        std::int16_t slot = 0;
        float gain = 1;
    };
    void push (Command) noexcept;
    void fadeOut (Voice&, double seconds) noexcept;

    std::array<std::shared_ptr<const io::DecodedAudio>, slots> owned;  // message thread
    std::array<std::atomic<const io::DecodedAudio*>, slots> live {};    // read by the audio thread
    std::array<std::atomic<std::int64_t>, slots> regionStart {}, regionEnd {};
    struct Retired
    {
        std::shared_ptr<const io::DecodedAudio> audio;
        std::uint64_t freeAfterFrame;
    };
    std::vector<Retired> retired;

    // Single-producer, single-consumer queue of pad presses and releases.
    static constexpr int queueSize = 256;
    std::array<Command, queueSize> queue {};
    std::atomic<int> queueWrite { 0 }, queueRead { 0 };

    std::array<Voice, maxVoices> voices;
    std::uint64_t voiceCounter = 0;
    std::array<std::atomic<bool>, slots> playing {};
    std::array<std::atomic<int>, maxVoices> shownSlot {};
    std::array<std::atomic<double>, maxVoices> shownPosition {};
    std::atomic<bool> choke { true };
    std::atomic<double> rate { 44100.0 };
    std::atomic<std::uint64_t> framesRendered { 0 };
};

} // namespace dss1::engine
