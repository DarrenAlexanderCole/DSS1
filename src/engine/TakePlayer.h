#pragma once

#include "io/AudioDecoder.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace dss1::engine
{

/// Plays back one recorded take, on its own, so it can be heard before it's kept or thrown away. Mixes into the
/// output after the effects (the take already has them).
///
/// Threads: `setTake`, `play`, `stop` and the queries are for the message thread; `render` for the audio thread.
/// A replaced take is kept alive until the audio thread has moved on from it.
class TakePlayer
{
public:
    /// Loads a take (stopped, at the start), or unloads with nullptr.
    void setTake (std::shared_ptr<const io::DecodedAudio> take);
    bool hasTake() const noexcept { return owned != nullptr; }
    /// Plays from `fraction` (0 … 1) of the take.
    void play (double fraction = 0);
    void stop() noexcept { playing.store (false); }
    bool isPlaying() const noexcept { return playing.load(); }
    /// Where playback is, 0 … 1.
    double progress() const noexcept;
    double lengthSeconds() const noexcept;

    void prepare (double deviceRate) noexcept { rate.store (deviceRate > 0 ? deviceRate : 44100.0); }
    void render (float* const* outputs, int numChannels, int numFrames) noexcept;

private:
    std::shared_ptr<const io::DecodedAudio> owned;
    std::vector<std::pair<std::shared_ptr<const io::DecodedAudio>, std::uint64_t>> retired;  // take, block when retired
    std::atomic<const io::DecodedAudio*> live { nullptr };
    std::atomic<double> position { 0 }, rate { 44100.0 };
    std::atomic<bool> playing { false };
    std::atomic<std::uint64_t> blocks { 0 };
};

} // namespace dss1::engine
