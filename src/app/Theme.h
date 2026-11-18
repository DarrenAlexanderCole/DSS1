#pragma once

#include "Hardware.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui
{

/// Colours shared by the waveform and lists (the hardware look's palette lives in hw::colour).
namespace Colours
{
    inline const juce::Colour background { 0xff0d0d0e };
    inline const juce::Colour panel      { 0xff1a1b1d };
    inline const juce::Colour text       { 0xffeceae4 };
    inline const juce::Colour dimText    { 0xff8f8b84 };
    inline const juce::Colour accent     { 0xffff3b1f };  // playhead red
    inline const juce::Colour beat       { 0x30ffc88c };
    inline const juce::Colour downbeat   { 0x90ffc88c };
    inline const juce::Colour tempoMark  { 0xff3fd15a };
}

/// How a TextButton is drawn: set with `hwStyle (button, ...)`.
enum class KeyStyle { dark, cream, cue, play, pad };

void hwStyle (juce::Button& button, KeyStyle style);
/// A hot cue pad lit in `colour` (or unlit when `lit` is false).
void hwPad (juce::Button& button, juce::Colour colour, bool lit);
/// Rotary knob scale labels, spread over the knob's travel ("1|2|4|8").
void hwScale (juce::Slider& slider, const juce::String& labelsSeparatedByBars);

/// The worn 90s rack look for every JUCE control: square keys, chrome toggles, fluted knobs, a fader,
/// display-style menus and text fields.
class Theme : public juce::LookAndFeel_V4
{
public:
    Theme();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTableHeaderBackground (juce::Graphics&, juce::TableHeaderComponent&) override;
    void drawTableHeaderColumn (juce::Graphics&, juce::TableHeaderComponent&, const juce::String& name, int id, int w, int h,
                                bool over, bool down, int flags) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart,
                        int thumbSize, bool over, bool down) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
};

} // namespace dss1::ui
