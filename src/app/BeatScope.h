#pragma once

#include "AudioEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <complex>

namespace dss1::ui
{

/// A dot-matrix graphic-EQ display of what you hear, for the recorder's window: 28 bands from 40 Hz to
/// 16 kHz. Bars jump up and leave glowing trails as they fall, peak dots drift down, and the whole matrix
/// swells on each kick, so you see the beat as much as hear it. Lit with the rest of the machine.
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
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder, bands = 28;
    std::vector<float> samples, window;
    std::vector<std::complex<float>> spectrum;
    std::array<float, bands> level {}, peak {}, peakVelocity {};
    std::vector<float> cells;  // brightness of each dot (bands x rows), fading
    int rows = 1;
    float bassAverage = 0, pulse = 0;
};

} // namespace dss1::ui
