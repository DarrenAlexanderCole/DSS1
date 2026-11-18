#include "StagePanel.h"
#include "Hardware.h"
#include "Theme.h"

namespace dss1::ui
{

StagePanel::StagePanel (juce::String t, juce::String s, juce::StringArray next)
    : title (std::move (t)), summary (std::move (s)), comingNext (std::move (next))
{
}

void StagePanel::setShowcase (std::unique_ptr<juce::Component> component)
{
    showcase = std::move (component);
    if (showcase != nullptr)
        addAndMakeVisible (*showcase);
    resized();
}

void StagePanel::paint (juce::Graphics& g)
{
    // A blanking plate: the module for this stage isn't fitted yet.
    const auto b = getLocalBounds().toFloat();
    const auto ear = static_cast<float> (hw::RackUnit::earWidth);
    hw::faceplate (g, b.reduced (ear, 0));
    hw::rackEar (g, b.withWidth (ear), 4);
    hw::rackEar (g, b.withLeft (b.getRight() - ear), 4);

    auto area = b.reduced (ear, 0).reduced (40, 36);
    hw::silk (g, title.toUpperCase(), "MODULE NOT FITTED", area.removeFromTop (22), juce::Justification::centredLeft, 16.0f);
    area.removeFromTop (8);
    g.setColour (hw::colour::silkDim);
    g.setFont (hw::label (16.0f, false));
    g.drawText (summary, area.removeFromTop (24), juce::Justification::centredLeft);
    area.removeFromTop (24);

    // Vent slots across the plate.
    auto vents = area.removeFromTop (juce::jmin (120.0f, area.getHeight() * 0.4f));
    const int rows = 3, cols = juce::jmax (4, static_cast<int> (vents.getWidth() / 70));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            const auto cell = juce::Rectangle<float> (vents.getX() + vents.getWidth() * static_cast<float> (c) / static_cast<float> (cols),
                                                      vents.getY() + vents.getHeight() * static_cast<float> (r) / rows,
                                                      vents.getWidth() / static_cast<float> (cols), vents.getHeight() / rows)
                                  .reduced (8, 10);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRoundedRectangle (cell.translated (0, 1), cell.getHeight() / 2);
            g.setColour (juce::Colour (0xff060606));
            g.fillRoundedRectangle (cell, cell.getHeight() / 2);
        }
    area.removeFromTop (24);
    hw::silk (g, "COMING", "PHASE 6", area.removeFromTop (18));
    area.removeFromTop (6);
    g.setFont (hw::label (15.0f, false));
    for (const auto& line : comingNext)
    {
        g.setColour (hw::colour::silk.withAlpha (0.85f));
        g.drawText (juce::String::fromUTF8 ("\xe2\x80\xa2  ") + line, area.removeFromTop (24), juce::Justification::centredLeft);
    }
}

void StagePanel::resized()
{
    if (showcase != nullptr)
        showcase->setBounds (getLocalBounds().reduced (hw::RackUnit::earWidth + 40, 40).removeFromBottom (300));
}

} // namespace dss1::ui
