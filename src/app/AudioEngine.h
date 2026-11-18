#pragma once

#include "engine/Deck.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>

namespace dss1::ui
{

/// Connects the deck to the Mac's audio output.
class AudioEngine : private juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    engine::Deck& deck() noexcept { return previewDeck; }

    /// The output level of channel 0 (left) or 1 (right): RMS over the last block, 0 … 1. For meters.
    float level (int channel) const noexcept { return levels[channel == 0 ? 0 : 1].load (std::memory_order_relaxed); }

    /// Empty when audio started fine; otherwise a readable reason.
    const juce::String& problem() const noexcept { return startupProblem; }

private:
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs,
                                           int numOutputs, int numFrames,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    engine::Deck previewDeck;
    juce::AudioDeviceManager devices;
    juce::String startupProblem;
    std::atomic<float> levels[2] { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace dss1::ui
