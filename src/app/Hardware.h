#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui::hw
{

// DSS1's look: worn 90s studio rack gear. Black anodised faceplates with silk-screened labels, aluminium rack
// ears, Phillips screws, cream VU meters, fluted knobs, square keys and a warm orange display. These are the
// drawing pieces every screen is built from. Fonts (Archivo Narrow, Michroma, VT323; SIL OFL) and the wear
// textures are compiled into the app.

namespace colour
{
    inline const juce::Colour silk        { 0xffeceae4 };   // silk-screen white
    inline const juce::Colour silkDim     { 0xffa7a49c };
    inline const juce::Colour red         { 0xffd23a26 };   // silk-screen red
    inline const juce::Colour amber       { 0xffff9a2e };   // display glow
    inline const juce::Colour amberDim    { 0xff8a4d12 };
    inline const juce::Colour amberMid    { 0xffb8661a };
    inline const juce::Colour faceplate   { 0xff1a1b1d };
    inline const juce::Colour glassBlack  { 0xff0b0702 };
    inline const juce::Colour cream       { 0xffd3c9ad };
    inline const juce::Colour cueOrange   { 0xffff8a3d };
    inline const juce::Colour playGreen   { 0xff7dff9a };
    inline const juce::Colour loopBlue    { 0xff2fa8d8 };
}

juce::Font label (float size, bool bold = true);   ///< Archivo Narrow: silk-screen labels and body text
juce::Font brand (float size);                     ///< Michroma: the DSS1 name, pad letters
juce::Font vfd (float size);                       ///< VT323: display readouts

/// A black anodised faceplate filling `area`: grain, scratches, grime, paint rubbed thin at the edges, a
/// bevel, and a screw in each corner.
void faceplate (juce::Graphics&, juce::Rectangle<float> area, bool screws = true);

/// A brushed aluminium rack ear with `slots` mounting slots.
void rackEar (juce::Graphics&, juce::Rectangle<float> area, int slots);

void screw (juce::Graphics&, juce::Point<float> centre, float radius = 8.0f);

/// A recessed window (display, waveform, list). Draw its content after, then `glass` over it.
void recess (juce::Graphics&, juce::Rectangle<float> area, float corner = 4.0f);
void glass (juce::Graphics&, juce::Rectangle<float> area, float corner = 4.0f);

/// A silk-screened label: `text` in white, then " — sub" in red, with worn ink.
void silk (juce::Graphics&, const juce::String& text, const juce::String& sub, juce::Rectangle<float> area,
           juce::Justification = juce::Justification::centredLeft, float size = 12.0f);

/// Text that glows like a vacuum-fluorescent display.
void glowText (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, const juce::Font&,
               juce::Colour = colour::amber, juce::Justification = juce::Justification::centredLeft);

/// A small round LED.
void led (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour, bool on);

/// A cream VU meter face with its needle at `level` (0 = -20 VU … 1 = +3 VU).
void vuMeter (juce::Graphics&, juce::Rectangle<float> area, float level, const juce::String& caption);

/// One rack unit: an ear either side and a worn faceplate between. Children go in `content()`.
class RackUnit : public juce::Component
{
public:
    explicit RackUnit (int earSlots = 2) : slots (earSlots) {}
    juce::Rectangle<int> content() const;
    void paint (juce::Graphics&) override;

    static constexpr int earWidth = 46;
    static constexpr int inset = 22;

private:
    int slots;
};

/// A VU meter with needle ballistics, fed levels (linear RMS) by `setLevel`.
class VuMeter : public juce::Component, private juce::Timer
{
public:
    explicit VuMeter (juce::String caption);
    /// Linear RMS, 0 … 1. Call from the message thread (a timer reading the engine's meters).
    std::function<float()> source;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    juce::String caption;
    float needle = 0;
};

} // namespace dss1::ui::hw
