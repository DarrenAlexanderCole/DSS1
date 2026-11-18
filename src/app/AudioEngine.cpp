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
    stopRecording();
    devices.removeAudioCallback (this);
    devices.closeAudioDevice();
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    deviceRate = device->getCurrentSampleRate();
    previewDeck.prepare (deviceRate, device->getCurrentBufferSizeSamples());
    fx.prepare (deviceRate, device->getCurrentBufferSizeSamples());
    pads.prepare (deviceRate);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                    int numFrames, const juce::AudioIODeviceCallbackContext&)
{
    previewDeck.render (outputs, numOutputs, numFrames);
    pads.render (outputs, juce::jmin (numOutputs, 2), numFrames);
    fx.process (outputs, juce::jmin (numOutputs, 2), numFrames);

    {
        const juce::ScopedTryLock lock (writerLock);
        if (lock.isLocked() && numOutputs >= 2)
            if (auto* writer = activeWriter.load())
            {
                writer->write (outputs, numFrames);
                recordedFrames += numFrames;
                float peak = 0;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < numFrames; ++i)
                        peak = std::max (peak, std::abs (outputs[c][i]));
                const auto w = peakWrite.load (std::memory_order_relaxed);
                peakRing[static_cast<std::size_t> (w)].store (peak, std::memory_order_relaxed);
                peakWrite.store ((w + 1) % peakRingSize, std::memory_order_release);
            }
    }
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

juce::String AudioEngine::startRecording (const juce::File& file)
{
    stopRecording();
    if (devices.getCurrentAudioDevice() == nullptr)
        return "No audio output to record.";
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return "Couldn't create " + file.getFullPathName();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out (std::move (stream));
    auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions().withSampleRate (deviceRate).withNumChannels (2).withBitsPerSample (24));
    if (writer == nullptr)
        return "Couldn't write a WAV file there.";
    if (! writerThread.isThreadRunning())
        writerThread.startThread();
    threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread, 32768);
    recordedFrames = 0;
    peakRead = peakWrite.load();
    recordingFile = file;
    const juce::ScopedLock lock (writerLock);
    activeWriter = threadedWriter.get();
    return {};
}

juce::File AudioEngine::stopRecording()
{
    {
        const juce::ScopedLock lock (writerLock);
        activeWriter = nullptr;
    }
    if (threadedWriter == nullptr)
        return {};
    threadedWriter.reset();  // flushes and closes the file
    return std::exchange (recordingFile, juce::File());
}

void AudioEngine::drainRecordedPeaks (std::vector<float>& into)
{
    const auto w = peakWrite.load (std::memory_order_acquire);
    while (peakRead != w)
    {
        into.push_back (peakRing[static_cast<std::size_t> (peakRead)].load (std::memory_order_relaxed));
        peakRead = (peakRead + 1) % peakRingSize;
    }
}

double AudioEngine::recordedSeconds() const noexcept
{
    return deviceRate > 0 ? static_cast<double> (recordedFrames.load()) / deviceRate : 0.0;
}

} // namespace dss1::ui
