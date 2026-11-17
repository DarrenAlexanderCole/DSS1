#pragma once

#include "io/AudioDecoder.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace dss1::engine
{

/// One playback deck: plays a decoded track at any tempo, with or without key lock. It knows nothing about
/// sound cards; the audio callback calls `render`. Everything except `prepare` and `render` is called from the
/// message thread and is safe while audio runs.
///
/// Positions are frames of the track at its own sample rate, like everything else in DSS1.
class Deck
{
public:
    Deck();
    ~Deck();
    Deck (const Deck&) = delete;
    Deck& operator= (const Deck&) = delete;

    // --- Message thread ------------------------------------------------------------------------------------

    /// Loads a track, paused at the start. The previous track is released on this thread, never on the audio
    /// thread.
    void load (std::shared_ptr<const io::DecodedAudio> audio);
    void unload();

    void play();
    void pause();
    bool isPlaying() const noexcept { return playing.load(); }

    void seek (Frame frame);

    /// 1.0 = normal speed. Clamped to 0.5 … 1.5.
    void setTempo (double ratio);
    double tempo() const noexcept { return tempoRatio.load(); }

    /// With key lock on, tempo changes keep the pitch; off, they behave like a turntable.
    void setKeyLock (bool on);
    bool keyLock() const noexcept { return keyLockOn.load(); }

    /// The frame being heard now (allowing for key lock's processing delay).
    double position() const noexcept;
    Frame length() const noexcept { return loadedLength.load(); }
    double trackSampleRate() const noexcept { return loadedRate.load(); }

    // --- Audio thread -------------------------------------------------------------------------------------

    /// Called when the audio device starts or changes, before any render.
    void prepare (double deviceSampleRate, int maxBlockFrames);

    /// Writes `frames` frames to `outputs` (adding nothing: it overwrites). Silence when paused or empty.
    void render (float* const* outputs, int outputChannels, int frames) noexcept;

private:
    struct Stretcher;

    void takePendingTrack() noexcept;
    void renderTurntable (float* const* outputs, int outputChannels, int frames, double step) noexcept;
    void renderKeyLocked (float* const* outputs, int outputChannels, int frames, double tempoNow) noexcept;
    float sampleAt (std::size_t channel, double position) const noexcept;
    void restartStretcher() noexcept;

    // Track handover between threads.
    std::mutex handover;
    std::shared_ptr<const io::DecodedAudio> pending;
    std::shared_ptr<const io::DecodedAudio> retired;
    bool hasPending = false;

    // Owned by the audio thread.
    std::shared_ptr<const io::DecodedAudio> current;
    double readPosition = 0;      ///< next frame to read
    double deviceRate = 44100;
    int maxBlock = 512;
    bool stretcherWasOn = false;
    std::unique_ptr<Stretcher> stretcher;
    double tempoCarry = 0;

    // Shared settings and state.
    std::atomic<bool> playing { false };
    std::atomic<double> tempoRatio { 1.0 };
    std::atomic<bool> keyLockOn { true };
    std::atomic<double> seekRequest { -1.0 };
    std::atomic<double> heardPosition { 0.0 };
    std::atomic<Frame> loadedLength { 0 };
    std::atomic<double> loadedRate { 0.0 };
};

} // namespace dss1::engine
