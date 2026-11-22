#pragma once

#include "Hardware.h"
#include "Session.h"
#include "TempoSteps.h"

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
    /// 20BAND$ pressed: show (true) or hide the graphic EQ.
    std::function<void (bool)> onEq;
    void setEqShown (bool shown) { eqKey.setToggleState (shown, juce::dontSendNotification); }
    void setStage (int index);

    static constexpr int preferredHeight = 256;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    Session& session;
    hw::VuMeter left { "CHANNEL A" }, right { "CHANNEL B" };
    juce::ToggleButton vuPad { "PAD" };
    TempoSteps beatSteps { session, 4 };  ///< the bar's four beats, beside the BPM
    std::array<juce::TextButton, 5> stageKeys;
    juce::TextButton eqKey { "20BAND$" };
    juce::TextButton startKey { "|<" }, backKey { "<<" }, forwardKey { ">>" }, cueKey { "CUE" }, playKey { "PLAY" };
    bool cueWasDown = false;

    juce::Rectangle<int> brandArea, meterArea, displayArea, stageArea, transportArea, eqArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Header)
};

} // namespace dss1::ui
