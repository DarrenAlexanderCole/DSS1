#pragma once

#include "engine/Deck.h"
#include "engine/Effects.h"
#include "engine/Sampler.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
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
    engine::Effects& effects() noexcept { return fx; }
    engine::Sampler& sampler() noexcept { return pads; }
    double sampleRate() const noexcept { return deviceRate; }

    /// Peaks of what's being recorded, one per audio block, for drawing the take as it records. Call from the
    /// message thread: appends the peaks since the last call to `into`.
    void drainRecordedPeaks (std::vector<float>& into);

    // --- Recorder: what you hear (after effects), to a 24-bit WAV ----------------------------------------
    /// Starts recording to `file`. Returns an error message, or empty on success.
    juce::String startRecording (const juce::File& file);
    /// Stops and closes the file. Returns the file that was written (or an invalid File if none).
    juce::File stopRecording();
    bool isRecording() const noexcept { return activeWriter.load() != nullptr; }
    /// Seconds recorded so far.
    double recordedSeconds() const noexcept;

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
    engine::Effects fx;
    engine::Sampler pads;
    static constexpr int peakRingSize = 4096;
    std::array<std::atomic<float>, peakRingSize> peakRing {};
    std::atomic<int> peakWrite { 0 };
    int peakRead = 0;
    double deviceRate = 44100.0;

    juce::TimeSliceThread writerThread { "DSS1 recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::CriticalSection writerLock;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
    std::atomic<juce::int64> recordedFrames { 0 };
    juce::File recordingFile;
    juce::AudioDeviceManager devices;
    juce::String startupProblem;
    std::atomic<float> levels[2] { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace dss1::ui
