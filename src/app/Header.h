#pragma once

#include "Hardware.h"
#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui
{

/// The top rack unit, on every stage: the DSS1 badge and pilot lamp, VU meters, the orange display (track,
/// BPM, key, time, bar), the stage keys and the transport keys.
class Header : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit Header (Session& session);
    ~Header() override;

    /// Called with 0 … 4 when a stage key is pressed.
    std::function<void (int)> onStage;
    void setStage (int index);

    static constexpr int preferredHeight = 256;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    Session& session;
    hw::VuMeter left { "CHANNEL A" }, right { "CHANNEL B" };
    std::array<juce::TextButton, 5> stageKeys;
    juce::TextButton startKey { "|<" }, backKey { "<<" }, forwardKey { ">>" }, cueKey { "CUE" }, playKey { "PLAY" };
    bool cueWasDown = false;

    juce::Rectangle<int> brandArea, meterArea, displayArea, stageArea, transportArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Header)
};

} // namespace dss1::ui
