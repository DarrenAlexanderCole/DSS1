#include "AudioEngine.h"

namespace dss1::ui
{

AudioEngine::AudioEngine()
{
    // Output only: no microphone permission needed.
    startupProblem = devices.initialiseWithDefaultDevices (0, 2);
    if (startupProblem.isEmpty() && devices.getCurrentAudioDevice() == nullptr)
        startupProblem = "No audio output device was found.";
    devices.addAudioCallback (this);
}

AudioEngine::~AudioEngine()
{
    devices.removeAudioCallback (this);
    devices.closeAudioDevice();
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    previewDeck.prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                    int numFrames, const juce::AudioIODeviceCallbackContext&)
{
    previewDeck.render (outputs, numOutputs, numFrames);
    for (int c = 0; c < 2; ++c)
    {
        float sum = 0;
        if (c < numOutputs && outputs[c] != nullptr)
            for (int i = 0; i < numFrames; ++i)
                sum += outputs[c][i] * outputs[c][i];
        const auto rms = numFrames > 0 ? std::sqrt (sum / static_cast<float> (numFrames)) : 0.0f;
        levels[c].store (c < numOutputs ? rms : levels[0].load(), std::memory_order_relaxed);
    }
}

} // namespace dss1::ui
