#pragma once

#include "engine/Deck.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace dss1::ui
{

/// Connects the deck to the Mac's audio output.
class AudioEngine : private juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    engine::Deck& deck() noexcept { return previewDeck; }

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace dss1::ui
