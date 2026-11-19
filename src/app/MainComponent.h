#pragma once

#include "AudioEngine.h"
#include "Crate.h"
#include "Session.h"
#include "Header.h"
#include "TransportBar.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui
{

/// The window's content: the header unit (badge, meters, display, stage and transport keys) on top, the
/// current stage's units in the middle, and the deck unit (overview, pitch) at the bottom.
class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    Theme theme;
    AudioEngine audio;
    Crate crate;
    Session session { crate, audio };
    Header header { session };
    TransportBar transport { session };
    std::vector<std::unique_ptr<juce::Component>> stages;
    int currentStage = 0;

    void showStage (int index);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace dss1::ui
