#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui
{

/// The app's colours in one place. A placeholder palette until the visual design is settled.
namespace Colours
{
    inline const juce::Colour background { 0xff15161a };
    inline const juce::Colour panel      { 0xff1f2127 };
    inline const juce::Colour text       { 0xfff1ece2 };
    inline const juce::Colour dimText    { 0xff8f8b84 };
    inline const juce::Colour accent     { 0xffe2412e };  // DSS1 red
    inline const juce::Colour beat       { 0x55f1ece2 };
    inline const juce::Colour downbeat   { 0xccf1ece2 };
    inline const juce::Colour tempoMark  { 0xff3fd15a };
}

class Theme : public juce::LookAndFeel_V4
{
public:
    Theme();

    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    void drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics&) override;
    int getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;
};

} // namespace dss1::ui
