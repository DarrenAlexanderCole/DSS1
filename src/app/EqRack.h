#pragma once

#include "engine/GraphicEq.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace dss1::ui
{

/// 20BAND$: the graphic EQ's panel, over the lower unit of LISTEN / DESIGN. Twenty upright faders (half an
/// octave apart, ±12 dB), the curve they make on an amber screen, an OUTPUT knob, ON and FLAT. The EQ is the
/// start of the chain: it shapes the sound before STEP, the DELUXE rack and the effects.
class EqRack : public juce::Component
{
public:
    explicit EqRack (engine::GraphicEq& eq);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void flat();

    engine::GraphicEq& eq;
    std::array<juce::Slider, engine::GraphicEq::bands> faders;
    juce::Slider output { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::ToggleButton on;
    juce::TextButton flatKey { "FLAT" };
    juce::Rectangle<int> header, row, screenBay, fadersBay, outBay, screen;
};

} // namespace dss1::ui
