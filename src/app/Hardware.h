#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui::hw
{

// DSS1's look: 60s-80s hi-fi and studio hardware. Brushed aluminium faces with printed labels, walnut cheeks,
// machined aluminium and brass knobs, black and ivory keys, chrome toggles, black-glass windows lit amber from
// behind, and backlit VU meters. The parts (knobs, keys, toggles, screws, pads) are rendered in 3D with Blender
// (tools/render) and drawn here with live light on top: displays and lamps warm up when DSS1 starts and
// breathe slightly, and dip with the music like a real power supply. Fonts (Archivo Narrow, Michroma, VT323;
// SIL OFL), textures and renders are compiled into the app.

namespace colour
{
    inline const juce::Colour silk        { 0xffeceae4 };   // light print (on black keys and glass)
    inline const juce::Colour ink         { 0xff17181b };   // printed on the aluminium faces
    inline const juce::Colour inkRed      { 0xffb02a1a };
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

/// The light level of DSS1's lamps and displays right now (about 0 … 1.02): warms up when DSS1 starts,
/// breathes slightly, and dips briefly with loud music, like a valve amp's supply. Multiply glows by it.
float light();
/// Tells the light how loud the music is (linear RMS), for the dips. The VU meters call it.
void feedLight (float rms);

/// A rendered part (from assets/render), e.g. "knob_alu", "key_dark_up", "toggle_down", "pad_up".
juce::Image part (const juce::String& name);
/// Draws `image` stretched over `dest` keeping its corners (`cornerX`, `cornerY` source pixels) unscaled
/// relative to `scale`: a key at any size.
void nineSlice (juce::Graphics&, const juce::Image&, juce::Rectangle<float> dest, int cornerX, int cornerY, float scale);

juce::Font label (float size, bool bold = true);   ///< Archivo Narrow: silk-screen labels and body text
juce::Font brand (float size);                     ///< Michroma: the DSS1 name, pad letters
juce::Font vfd (float size);                       ///< VT323: display readouts

/// A brushed aluminium face filling `area`: the grain, a broad sheen from the light, a little wear, a bevel,
/// and a screw in each corner.
void faceplate (juce::Graphics&, juce::Rectangle<float> area, bool screws = true);

/// A walnut cheek (where rack gear has its ears). `slots` is ignored now (kept for callers).
void rackEar (juce::Graphics&, juce::Rectangle<float> area, int slots);

void screw (juce::Graphics&, juce::Point<float> centre, float radius = 8.0f);

/// A recessed window (display, waveform, list). Draw its content after, then `glass` over it.
void recess (juce::Graphics&, juce::Rectangle<float> area, float corner = 4.0f);
void glass (juce::Graphics&, juce::Rectangle<float> area, float corner = 4.0f);

/// A small lit readout window (a knob's value): a recess, the glowing text, the glass.
void readout (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, const juce::Font&, juce::Colour = colour::amber);

/// A module bay on the face: a panel section outlined by an engraved line, slightly shaded.
void bay (juce::Graphics&, juce::Rectangle<float> area, float corner = 5.0f);

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
    float needle = 0, velocity = 0;  // a sprung needle with some weight: it swings, overshoots a touch, settles
    double lastTick = 0;
};

} // namespace dss1::ui::hw
