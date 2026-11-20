#pragma once

#include "engine/Sampler.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace dss1::ui
{

/// A backlit light matrix beside CHOP's pads: one column per pad (1 … 16) of the bank on screen, lighting up
/// as loud as that pad plays (its sample after its GAIN and envelope), then falling back, with a peak light
/// that hangs for a moment. Switch banks and it shows that bank's pads.
class PadMeters : public juce::Component, private juce::Timer
{
public:
    PadMeters (engine::Sampler&, std::function<int()> currentBank);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    engine::Sampler& sampler;
    std::function<int()> bank;
    int shownBank = -1;
    std::array<float, 16> level {}, peak {};
    std::array<int, 16> peakHold {};
};

} // namespace dss1::ui
