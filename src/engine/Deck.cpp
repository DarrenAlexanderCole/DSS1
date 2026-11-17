#include "engine/Deck.h"

#include <algorithm>
#include <cmath>

#if defined(__clang__) || defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wfloat-conversion"
 #pragma GCC diagnostic ignored "-Wpedantic"
 #if defined(__clang__)
  #pragma GCC diagnostic ignored "-Wimplicit-int-float-conversion"
 #endif
#endif
#include <signalsmith-stretch/signalsmith-stretch.h>
#if defined(__clang__) || defined(__GNUC__)
 #pragma GCC diagnostic pop
#endif

namespace dss1::engine
{

namespace
{
    constexpr double minTempo = 0.5, maxTempo = 1.5;
    constexpr int stretchChannels = 2;
}

struct Deck::Stretcher
{
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    std::vector<std::vector<float>> in, out;
    std::vector<float*> inPointers, outPointers;

    void configure (double rate, int maxBlock)
    {
        stretch.presetDefault (stretchChannels, static_cast<float> (rate));
        const auto inputCapacity = static_cast<std::size_t> (
            std::max (static_cast<int> (std::ceil (maxBlock * maxTempo)) + 64, stretch.inputLatency() + 64));
        in.assign (stretchChannels, std::vector<float> (inputCapacity, 0.0f));
        out.assign (stretchChannels, std::vector<float> (static_cast<std::size_t> (maxBlock), 0.0f));
        inPointers.clear();
        outPointers.clear();
        for (int c = 0; c < stretchChannels; ++c)
        {
            inPointers.push_back (in[static_cast<std::size_t> (c)].data());
            outPointers.push_back (out[static_cast<std::size_t> (c)].data());
        }
    }
};

Deck::Deck() : stretcher (std::make_unique<Stretcher>())
{
    stretcher->configure (deviceRate, maxBlock);
}

Deck::~Deck() = default;

// --- Message thread -----------------------------------------------------------------------------------------

void Deck::load (std::shared_ptr<const io::DecodedAudio> audio)
{
    playing.store (false);
    const auto length = audio ? audio->info.lengthFrames : Frame { 0 };
    const auto rate = audio ? audio->info.sampleRate : 0.0;

    std::shared_ptr<const io::DecodedAudio> toRelease;
    {
        const std::lock_guard lock (handover);
        toRelease = std::move (retired);  // freed below, here on the message thread
        retired.reset();
        if (hasPending)
            toRelease = std::move (pending);  // a track that never got to play
        pending = std::move (audio);
        hasPending = true;
    }
    clearLoop();
    seekRequest.store (0.0);
    heardPosition.store (0.0);
    loadedLength.store (length);
    loadedRate.store (rate);
}

void Deck::unload()
{
    load (nullptr);
}

void Deck::play()
{
    if (loadedLength.load() > 0)
        playing.store (true);
}

void Deck::pause()
{
    playing.store (false);
}

void Deck::setLoop (Frame start, Frame end)
{
    const auto rate = loadedRate.load();
    if (end <= start || (rate > 0 && static_cast<double> (end - start) < 0.01 * rate))
    {
        clearLoop();
        return;
    }
    loopEnd.store (0.0);  // off while the two halves change, so the audio thread never sees a mixed pair
    loopStart.store (static_cast<double> (start));
    loopEnd.store (static_cast<double> (end));
}

void Deck::clearLoop()
{
    loopEnd.store (0.0);
    loopStart.store (0.0);
}

double Deck::wrap (double position, double start, double end) const noexcept
{
    if (end <= start || position < end)
        return position;
    return start + std::fmod (position - start, end - start);
}

void Deck::seek (Frame frame)
{
    const auto clamped = static_cast<double> (std::clamp<Frame> (frame, 0, std::max<Frame> (0, loadedLength.load())));
    seekRequest.store (clamped);
    heardPosition.store (clamped);
}

void Deck::setTempo (double ratio)
{
    tempoRatio.store (std::clamp (ratio, minTempo, maxTempo));
}

void Deck::setKeyLock (bool on)
{
    keyLockOn.store (on);
}

double Deck::position() const noexcept
{
    return heardPosition.load();
}

// --- Audio thread -------------------------------------------------------------------------------------------

void Deck::prepare (double deviceSampleRate, int maxBlockFrames)
{
    deviceRate = deviceSampleRate > 0 ? deviceSampleRate : 44100.0;
    maxBlock = std::max (maxBlockFrames, 16);
    stretcher->configure (deviceRate, maxBlock);
    stretcherWasOn = false;
}

void Deck::takePendingTrack() noexcept
{
    std::unique_lock lock (handover, std::try_to_lock);
    if (! lock.owns_lock() || ! hasPending)
        return;
    retired = std::move (current);  // the message thread frees it on the next load
    current = std::move (pending);
    pending.reset();
    hasPending = false;
    readPosition = 0;
    stretcherWasOn = false;
}

float Deck::sampleAt (std::size_t channel, double pos) const noexcept
{
    // Cubic (Catmull-Rom) interpolation between frames; silence outside the track.
    const auto& data = current->channels[std::min (channel, current->channels.size() - 1)];
    const auto n = static_cast<long long> (data.size());
    const auto i = static_cast<long long> (std::floor (pos));
    const auto t = static_cast<float> (pos - static_cast<double> (i));
    const auto at = [&] (long long k) { return (k >= 0 && k < n) ? data[static_cast<std::size_t> (k)] : 0.0f; };
    const auto y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    return y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + t * (3.0f * (y1 - y2) + y3 - y0)));
}

void Deck::restartStretcher() noexcept
{
    auto& s = *stretcher;
    s.stretch.reset();
    // Give it the audio just ahead of the play position, so what comes out starts where we are.
    const auto fileStep = current->info.sampleRate / deviceRate;
    const auto preroll = std::min (s.stretch.inputLatency(), static_cast<int> (s.in[0].size()));
    for (int i = 0; i < preroll; ++i)
        for (std::size_t c = 0; c < stretchChannels; ++c)
            s.in[c][static_cast<std::size_t> (i)] = sampleAt (c, readPosition + i * fileStep);
    s.stretch.seek (s.inPointers.data(), preroll, tempoRatio.load());
    readPosition += preroll * fileStep;
    tempoCarry = 0;
    stretcherWasOn = true;
}

void Deck::render (float* const* outputs, int outputChannels, int frames) noexcept
{
    for (int c = 0; c < outputChannels; ++c)
        std::fill (outputs[c], outputs[c] + frames, 0.0f);

    takePendingTrack();
    if (! current || current->channels.empty() || current->info.sampleRate <= 0)
    {
        playing.store (false);
        return;
    }

    if (const auto target = seekRequest.exchange (-1.0); target >= 0)
    {
        readPosition = target;
        stretcherWasOn = false;
    }

    const auto keyLocked = keyLock();
    if (! playing.load())
    {
        stretcherWasOn = false;  // restart cleanly on the next play
        return;
    }

    const auto tempoNow = tempo();
    for (int done = 0; done < frames;)
    {
        const auto n = std::min (frames - done, maxBlock);
        float* shifted[8] {};
        for (int c = 0; c < std::min (outputChannels, 8); ++c)
            shifted[c] = outputs[c] + done;

        if (keyLocked)
            renderKeyLocked (shifted, std::min (outputChannels, 8), n, tempoNow);
        else
            renderTurntable (shifted, std::min (outputChannels, 8), n, tempoNow * current->info.sampleRate / deviceRate);
        done += n;
    }

    if (readPosition >= static_cast<double> (current->info.lengthFrames))
        playing.store (false);
}

void Deck::renderTurntable (float* const* outputs, int outputChannels, int frames, double step) noexcept
{
    stretcherWasOn = false;
    const auto sourceChannels = current->channels.size();
    const auto start = loopStart.load(), end = loopEnd.load();
    for (int i = 0; i < frames; ++i)
    {
        for (int c = 0; c < outputChannels; ++c)
            outputs[c][i] = sampleAt (std::min (static_cast<std::size_t> (c), sourceChannels - 1), readPosition);
        readPosition = wrap (readPosition + step, start, end);
    }
    heardPosition.store (readPosition);
}

void Deck::renderKeyLocked (float* const* outputs, int outputChannels, int frames, double tempoNow) noexcept
{
    if (! stretcherWasOn)
        restartStretcher();

    auto& s = *stretcher;
    const auto fileStep = current->info.sampleRate / deviceRate;

    // Feed tempo × frames of input (resampled to the device rate) for `frames` of output: same pitch, new speed.
    const auto wanted = static_cast<double> (frames) * tempoNow + tempoCarry;
    const auto inputCount = std::min (static_cast<int> (std::floor (wanted)), static_cast<int> (s.in[0].size()));
    tempoCarry = wanted - inputCount;

    const auto sourceChannels = current->channels.size();
    const auto start = loopStart.load(), end = loopEnd.load();
    for (int i = 0; i < inputCount; ++i)
    {
        for (std::size_t c = 0; c < stretchChannels; ++c)
            s.in[c][static_cast<std::size_t> (i)] = sampleAt (std::min (c, sourceChannels - 1), readPosition);
        readPosition = wrap (readPosition + fileStep, start, end);
    }

    s.stretch.process (s.inPointers.data(), inputCount, s.outPointers.data(), frames);

    for (int c = 0; c < outputChannels; ++c)
        std::copy_n (s.out[static_cast<std::size_t> (std::min (c, stretchChannels - 1))].data(), frames, outputs[c]);

    // What's heard lags what's read by the stretcher's delay.
    const auto delayFrames = (s.stretch.inputLatency() + s.stretch.outputLatency() * tempoNow) * fileStep;
    auto heard = readPosition - delayFrames;
    if (end > start && heard < start && readPosition >= start && readPosition < end)
        heard += end - start;  // just after a wrap, what's heard is still the end of the loop
    heardPosition.store (std::max (0.0, heard));
}

} // namespace dss1::engine
