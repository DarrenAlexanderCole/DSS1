#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui
{

/// The window's content: the four stages as tabs along the top.
class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

private:
    Theme theme;
    juce::TabbedComponent stages { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace dss1::ui
