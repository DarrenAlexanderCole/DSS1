#include "Modular.h"

#include <algorithm>
#include <cmath>

namespace dss1::engine
{

void Modular::setSync (double anchorFrame, double beatFrames, int beatInBar, int beatsPerBar) noexcept
{
    // Written to the slot the audio thread isn't reading, then made current: never torn.
    const auto next = 1 - syncSlot.load (std::memory_order_relaxed);
    sync[static_cast<std::size_t> (next)] = { anchorFrame, beatFrames, beatInBar, std::max (1, beatsPerBar) };
    syncSlot.store (next, std::memory_order_release);
}

void Modular::prepare (double sampleRate, int maxBlock)
{
    rate = sampleRate;
    for (auto& h : history)
        h.assign (static_cast<std::size_t> (sampleRate * 6.0), 0.0f);  // a 1/2-note slice at 40 BPM, and room
    beatBuffer.assign (static_cast<std::size_t> (std::max (maxBlock, 1) * 2), 0.0);
    reset();
}

void Modular::reset() noexcept
{
    for (auto& h : history)
        std::fill (h.begin(), h.end(), 0.0f);
    historyWrite = 0;
    sliceStart = sliceLength = slicePos = 0;
    repeating = haveSlice = false;
    lastSliceIndex = lastPeriodIndex = -1;
    stutterMix = 0;
    gate = 1;
    slicerDepth = 0;
}

void Modular::process (float* const* channels, int numChannels, int numFrames, double trackFrame, bool trackPlaying) noexcept
{
    if (numFrames <= 0 || history[0].empty())
        return;
    numChannels = std::min (numChannels, 2);

    // Where the beat is, sample by sample, in beats from the start of the bar.
    if (static_cast<std::size_t> (numFrames) > beatBuffer.size())
        numFrames = static_cast<int> (beatBuffer.size());
    const auto& s = sync[static_cast<std::size_t> (syncSlot.load (std::memory_order_acquire))];
    const auto perSample = std::max (20.0f, bpm.load (std::memory_order_relaxed)) / 60.0 / rate;
    const auto perBar = static_cast<double> (s.beatsPerBar);
    double beat;
    if (trackPlaying && s.beatFrames > 0)
    {
        beat = static_cast<double> (s.beatInBar) + (trackFrame - s.anchorFrame) / s.beatFrames;
        ownBeat = beat;
    }
    else
        beat = ownBeat;
    for (int i = 0; i < numFrames; ++i)
    {
        auto b = std::fmod (beat + perSample * i, perBar);
        beatBuffer[static_cast<std::size_t> (i)] = b < 0 ? b + perBar : b;
    }
    ownBeat = std::fmod (beat + perSample * numFrames, perBar);

    // The modules, in slot order.
    for (const auto& slot : order)
    {
        switch (slot.load (std::memory_order_relaxed))
        {
            case stutter: runStutter (channels, numChannels, numFrames, beatBuffer.data()); break;
            case slicer:  runSlicer (channels, numChannels, numFrames, beatBuffer.data(), perBar); break;
            default: break;  // not built yet: the sound passes through
        }
    }
}

void Modular::runStutter (float* const* ch, int numChannels, int numFrames, const double* beats) noexcept
{
    auto& c = controls[stutter];
    const auto on = c.on.load (std::memory_order_relaxed);
    const auto mixTarget = on ? std::clamp (c.b.load (std::memory_order_relaxed), 0.0f, 1.0f) : 0.0f;
    const auto hold = c.mode.load (std::memory_order_relaxed);
    const auto lengthIndex = std::clamp (static_cast<int> (std::lround (c.a.load (std::memory_order_relaxed))), 0, 4);
    const auto lengthBeats = stutterBeats[static_cast<std::size_t> (lengthIndex)];
    const auto periodBeats = std::max (1.0, lengthBeats);  // a fresh slice every beat (or every slice, if longer)
    const auto size = history[0].size();
    const auto perSample = std::max (20.0f, bpm.load (std::memory_order_relaxed)) / 60.0 / rate;
    const auto length = std::min (size / 2, static_cast<std::size_t> (lengthBeats / perSample));
    const auto fade = std::max<std::size_t> (1, std::min<std::size_t> (static_cast<std::size_t> (rate * 0.002), length / 4));
    const bool idle = mixTarget <= 0.0f && stutterMix < 1.0e-4f;

    for (int i = 0; i < numFrames; ++i)
    {
        const auto beat = beats[i];
        // Remember everything that's heard.
        for (int k = 0; k < numChannels; ++k)
            history[static_cast<std::size_t> (k)][historyWrite] = ch[k][i];
        if (numChannels == 1)
            history[1][historyWrite] = ch[0][i];

        if (! idle)
        {
            // A new slice starts on the beat: every beat (or every slice length, if longer), unless HOLD keeps
            // the one it has. Turning on waits for the next slice line.
            const auto sliceIndex = static_cast<long long> (std::floor (beat / lengthBeats));
            const auto periodIndex = static_cast<long long> (std::floor (beat / periodBeats));
            const bool onSliceLine = sliceIndex != lastSliceIndex && lastSliceIndex >= 0;
            const bool newPeriod = periodIndex != lastPeriodIndex && lastPeriodIndex >= 0;
            if (on && onSliceLine && (! haveSlice || (newPeriod && ! hold)))
            {
                sliceStart = historyWrite;
                sliceLength = std::max<std::size_t> (length, 16);
                slicePos = 0;
                repeating = false;
                haveSlice = true;
            }
            lastSliceIndex = sliceIndex;
            lastPeriodIndex = periodIndex;

            stutterMix += (mixTarget - stutterMix) * 0.0015f;
            if (haveSlice && repeating)
            {
                // Each repeat fades in and out over 2 ms so it never clicks.
                const auto edge = std::min ({ slicePos, sliceLength - slicePos, fade });
                const auto window = static_cast<float> (edge) / static_cast<float> (fade);
                const auto read = (sliceStart + slicePos) % size;
                for (int k = 0; k < numChannels; ++k)
                {
                    const auto wet = history[static_cast<std::size_t> (k)][read] * window;
                    ch[k][i] += (wet - ch[k][i]) * stutterMix;
                }
            }
            if (haveSlice && ++slicePos >= sliceLength)
            {
                slicePos = 0;
                repeating = true;
            }
            if (! on && stutterMix < 1.0e-4f)
                haveSlice = repeating = false;
        }
        else
        {
            lastSliceIndex = static_cast<long long> (std::floor (beat / lengthBeats));
            lastPeriodIndex = static_cast<long long> (std::floor (beat / periodBeats));
            haveSlice = repeating = false;
        }
        historyWrite = (historyWrite + 1) % size;
    }
}

void Modular::runSlicer (float* const* ch, int numChannels, int numFrames, const double* beats, double beatsPerBar) noexcept
{
    auto& c = controls[slicer];
    const auto depthTarget = c.on.load (std::memory_order_relaxed) ? std::clamp (c.b.load (std::memory_order_relaxed), 0.0f, 1.0f) : 0.0f;
    if (depthTarget <= 0.0f && slicerDepth < 1.0e-4f)
    {
        slicerDepth = 0;
        gate = 1;
        return;
    }
    const auto pattern = slicerPatterns[static_cast<std::size_t> (std::clamp (static_cast<int> (std::lround (c.a.load (std::memory_order_relaxed))), 0, 7))];
    const auto triplet = c.mode.load (std::memory_order_relaxed);
    const auto stepsPerBeat = triplet ? 3.0 : 4.0;
    const auto steps = static_cast<int> (std::lround (beatsPerBar * stepsPerBeat));
    const auto attack = 1.0f - std::exp (-1.0f / static_cast<float> (rate * 0.0015));
    const auto release = 1.0f - std::exp (-1.0f / static_cast<float> (rate * 0.006));

    for (int i = 0; i < numFrames; ++i)
    {
        const auto step = static_cast<int> (std::floor (beats[i] * stepsPerBeat)) % std::max (1, steps);
        const bool open = ((pattern >> (15 - (step % 16))) & 1u) != 0;
        const auto target = open ? 1.0f : 0.0f;
        gate += (target - gate) * (target > gate ? attack : release);
        slicerDepth += (depthTarget - slicerDepth) * 0.0015f;
        const auto gain = 1.0f - slicerDepth * (1.0f - gate);
        for (int k = 0; k < numChannels; ++k)
            ch[k][i] *= gain;
    }
}

} // namespace dss1::engine
