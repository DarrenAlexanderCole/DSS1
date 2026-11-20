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
    take.prepare (deviceRate);
    preview.prepare (deviceRate);
    for (auto& b : padBuffer)
        b.assign (static_cast<std::size_t> (juce::jmax (8192, device->getCurrentBufferSizeSamples() * 2)), 0.0f);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                    int numFrames, const juce::AudioIODeviceCallbackContext&)
{
    previewDeck.render (outputs, numOutputs, numFrames);
    pads.render (outputs, juce::jmin (numOutputs, 2), numFrames);
    preview.render (outputs, juce::jmin (numOutputs, 2), numFrames);
    fx.process (outputs, juce::jmin (numOutputs, 2), numFrames);
    take.render (outputs, juce::jmin (numOutputs, 2), numFrames);

    {
        const juce::ScopedTryLock lock (writerLock);
        if (lock.isLocked() && numOutputs >= 2)
            if (auto* writer = activeWriter.load())
            {
                // The pad: record a turned-down copy (what you hear stays as it is).
                const auto gain = recordGain.load (std::memory_order_relaxed);
                const float* const* source = outputs;
                const float* padded[2] { padBuffer[0].data(), padBuffer[1].data() };
                if (gain < 0.999f && static_cast<std::size_t> (numFrames) <= padBuffer[0].size())
                {
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < numFrames; ++i)
                            padBuffer[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)] = outputs[c][i] * gain;
                    source = padded;
                }
                writer->write (source, numFrames);
                recordedFrames += numFrames;
                float peak = 0;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < numFrames; ++i)
                        peak = std::max (peak, std::abs (source[c][i]));
                const auto w = peakWrite.load (std::memory_order_relaxed);
                peakRing[static_cast<std::size_t> (w)].store (peak, std::memory_order_relaxed);
                peakWrite.store ((w + 1) % peakRingSize, std::memory_order_release);
            }
    }
    // For the spectrum display: what you hear, mono.
    if (numOutputs >= 1)
    {
        auto w = scopeWrite.load (std::memory_order_relaxed);
        for (int i = 0; i < numFrames; ++i)
        {
            const auto v = numOutputs >= 2 ? 0.5f * (outputs[0][i] + outputs[1][i]) : outputs[0][i];
            scope[static_cast<std::size_t> (w)].store (v, std::memory_order_relaxed);
            w = (w + 1) % scopeSize;
        }
        scopeWrite.store (w, std::memory_order_release);
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

void AudioEngine::recentOutput (std::vector<float>& into, int n) const
{
    n = juce::jlimit (1, scopeSize, n);
    into.resize (static_cast<std::size_t> (n));
    const auto end = scopeWrite.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        into[static_cast<std::size_t> (i)] = scope[static_cast<std::size_t> ((end - n + i + scopeSize) % scopeSize)].load (std::memory_order_relaxed);
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
