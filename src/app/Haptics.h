#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui::haptics
{

/// A small physical tap on a Force Touch trackpad (macOS), like a knob clicking into a detent. Does nothing
/// where there's no such trackpad, or on other systems.
void tap();

/// Gives every knob and fader under `root` detents you can feel: a tap on each step of a stepped control, and
/// every twentieth of the way (and at the centre) on a smooth one.
void addDetents (juce::Component& root);

} // namespace dss1::ui::haptics
