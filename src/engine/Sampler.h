#pragma once

#include "io/AudioDecoder.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace dss1::engine
{

/// The pad sampler: 8 banks (A–H) of 16 pads, 128 slots, like a drum machine. Each slot holds a sample, or a
/// slice of one (a start and end frame in a track that several pads share); a pad press plays it as a one-shot
/// over everything else (up to 16 at once; pressing a playing pad restarts it), with 3 ms fades at each end so
/// slices don't click. Mixes into the output buffer, so effects and the recorder hear it.
///
/// Threads: `setSample`, `trigger`, `stopAll` and `collectGarbage` are for the message thread; `prepare` and
/// `render` for the audio thread. Samples are swapped without locks; a replaced sample is kept alive until no
/// voice can still be playing it.
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
    void stopAll();
    /// Frees samples that were replaced long enough ago. Call now and then (a UI timer).
    void collectGarbage();
    /// Slots playing right now (approximately; for lighting pads).
    bool isPlaying (int slot) const noexcept;

    // --- Audio thread --------------------------------------------------------------------------------------

    void prepare (double deviceRate);
    /// Adds the playing pads into `outputs` (1 or 2 channels used).
    void render (float* const* outputs, int numChannels, int numFrames) noexcept;

private:
    struct Voice
    {
        const io::DecodedAudio* audio = nullptr;
        double position = 0, step = 1, start = 0, end = 0, fade = 1;
        float gain = 1;
        int slot = -1;
    };

    std::array<std::shared_ptr<const io::DecodedAudio>, slots> owned;  // message thread
    std::array<std::atomic<const io::DecodedAudio*>, slots> live {};    // read by the audio thread
    std::array<std::atomic<std::int64_t>, slots> regionStart {}, regionEnd {};
    struct Retired
    {
        std::shared_ptr<const io::DecodedAudio> audio;
        std::uint64_t freeAfterFrame;
    };
    std::vector<Retired> retired;

    // Single-producer, single-consumer queue of pad presses: (slot << 8 | gain 0…255), -1 = stop all.
    static constexpr int queueSize = 256;
    std::array<std::atomic<int>, queueSize> queue {};
    std::atomic<int> queueWrite { 0 }, queueRead { 0 };

    std::array<Voice, maxVoices> voices;
    std::array<std::atomic<bool>, slots> playing {};
    std::atomic<double> rate { 44100.0 };
    std::atomic<std::uint64_t> framesRendered { 0 };
};

} // namespace dss1::engine
