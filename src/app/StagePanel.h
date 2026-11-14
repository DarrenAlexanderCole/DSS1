#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui
{

/// One of the four stages. For now each shows what it will do; later each becomes its own screen.
class StagePanel : public juce::Component
{
public:
    StagePanel (juce::String title, juce::String summary, juce::StringArray comingNext);

    /// An optional component shown under the text (e.g. the grid preview on Prep).
    void setShowcase (std::unique_ptr<juce::Component> component);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title, summary;
    juce::StringArray comingNext;
    std::unique_ptr<juce::Component> showcase;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StagePanel)
};

} // namespace dss1::ui
