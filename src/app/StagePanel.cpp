#include "StagePanel.h"
#include "Theme.h"

namespace dss1::ui
{

namespace
{
    constexpr int margin = 40;
    constexpr int textBlockHeight = 210;
}

StagePanel::StagePanel (juce::String t, juce::String s, juce::StringArray next)
    : title (std::move (t)), summary (std::move (s)), comingNext (std::move (next))
{
}

void StagePanel::setShowcase (std::unique_ptr<juce::Component> component)
{
    if (showcase != nullptr)
        removeChildComponent (showcase.get());
    showcase = std::move (component);
    if (showcase != nullptr)
        addAndMakeVisible (*showcase);
    resized();
}

void StagePanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    auto area = getLocalBounds().reduced (margin);

    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (34.0f, juce::Font::bold));
    g.drawText (title, area.removeFromTop (44), juce::Justification::topLeft);

    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (16.0f));
    g.drawFittedText (summary, area.removeFromTop (48), juce::Justification::topLeft, 2);

    g.setFont (juce::FontOptions (14.0f));
    for (const auto& item : comingNext)
    {
        auto line = area.removeFromTop (24);
        g.setColour (Colours::accent);
        g.fillEllipse (line.removeFromLeft (14).withSizeKeepingCentre (6, 6).toFloat());
        g.setColour (Colours::text.withAlpha (0.8f));
        g.drawText (item, line, juce::Justification::centredLeft);
    }
}

void StagePanel::resized()
{
    if (showcase != nullptr)
        showcase->setBounds (getLocalBounds().reduced (margin).withTrimmedTop (textBlockHeight));
}

} // namespace dss1::ui
