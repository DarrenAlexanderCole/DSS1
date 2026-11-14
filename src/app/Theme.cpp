#include "Theme.h"

namespace dss1::ui
{

Theme::Theme()
{
    setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
    setColour (juce::DocumentWindow::textColourId, Colours::text);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::TabbedComponent::backgroundColourId, Colours::background);
    setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
}

void Theme::drawTabbedButtonBarBackground (juce::TabbedButtonBar& bar, juce::Graphics& g)
{
    g.setColour (Colours::panel);
    g.fillRect (bar.getLocalBounds());
}

int Theme::getTabButtonBestWidth (juce::TabBarButton& button, int tabDepth)
{
    juce::ignoreUnused (tabDepth);
    return button.getTabbedButtonBar().getWidth() / juce::jmax (1, button.getTabbedButtonBar().getNumTabs());
}

void Theme::drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool)
{
    const auto area = button.getLocalBounds().toFloat();
    const bool active = button.isFrontTab();

    if (active || isMouseOver)
    {
        g.setColour (active ? Colours::background : Colours::background.withAlpha (0.4f));
        g.fillRect (area);
    }
    if (active)
    {
        g.setColour (Colours::accent);
        g.fillRect (area.withHeight (3.0f));
    }

    // "1  CATALOG": the stage number, dim, then its name.
    const auto index = button.getIndex() + 1;
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.setColour (active ? Colours::text : Colours::dimText);
    g.drawText (juce::String (index) + "   " + button.getButtonText().toUpperCase(),
                area.reduced (12.0f, 0.0f), juce::Justification::centred);
}

} // namespace dss1::ui
