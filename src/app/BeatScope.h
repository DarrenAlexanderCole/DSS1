#pragma once

#include "AudioEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <complex>

namespace dss1::ui
{

/// A spectrum of what you hear, for the recorder's window: a smooth luminous curve from 30 Hz to 16 kHz with a
/// gradient fill, ghosts of the last moments fading behind it, a peak-hold line that drifts down, and a bloom
/// on each kick, so you see the beat as much as hear it. Lit with the rest of the machine.
class BeatScope : public juce::Component, private juce::Timer
{
public:
    explicit BeatScope (AudioEngine& engine);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void analyse();

    AudioEngine& engine;
    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder, bands = 72, ghosts = 7;
    std::vector<float> samples, window;
    std::vector<std::complex<float>> spectrum;
    std::array<float, bands> level {}, peak {}, peakVelocity {};
    std::array<std::array<float, bands>, ghosts> history {};  // the last few frames, newest first
    int frame = 0;
    float bassAverage = 0, pulse = 0;
};

} // namespace dss1::ui
