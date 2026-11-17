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
}

} // namespace dss1::ui
