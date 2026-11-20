#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace dss1::ui::scope
{

// How DSS1's windows draw sound: smooth, luminous shapes behind the glass, lit warm like the VU meters. No
// blocks or dots: anti-aliased contours with a soft bloom, gradient fills that are brightest at the edge of
// the sound, a lighter core showing its body, and a fine graticule. The raw materials are old; the
// instruments are new.

/// One column of a waveform: the lowest and highest sample in it, -1 … 1.
struct Span
{
    float lo = 0, hi = 0;
};

/// A luminous waveform, mirrored about its centre line. `cols` are spaced `step` pixels apart starting at `area.getX()`. Columns left of
/// `playedX` are drawn dimmer (the part already played). `scale` is the share of half the height a full-scale
/// sample reaches.
void wave (juce::Graphics&, juce::Rectangle<float> area, const std::vector<Span>& cols, float step, juce::Colour,
           float playedX = -1.0e9f, float scale = 0.47f);

/// A fine instrument graticule: `across` x `down` divisions, faint, with a brighter centre line if `centre`.
void graticule (juce::Graphics&, juce::Rectangle<float> area, int across, int down, juce::Colour, bool centre = true);

/// A glowing line: a wide soft bloom, a halo and a bright core.
void trace (juce::Graphics&, const juce::Path&, juce::Colour, float width = 1.5f, float glow = 1.0f);

/// A smooth curve (Catmull-Rom) through `points`, left to right.
juce::Path smooth (const std::vector<juce::Point<float>>& points);

/// Fills under an open curve down to `floorY` with a gradient: `colour` at `topY`, fading to nearly nothing
/// at the floor.
void fillUnder (juce::Graphics&, const juce::Path& curve, float topY, float floorY, juce::Colour, float alpha = 0.55f);

// --- Backlit monitors: small amber-lit screens like the VU meters, with curves printed in dark ink. ----------

/// The ink the monitors draw with.
juce::Colour monitorInk();
/// Draws a monitor's bezel and lit face in `area` and returns the plot area inside it.
juce::Rectangle<float> monitorBegin (juce::Graphics&, juce::Rectangle<float> area);
/// The glass over a monitor (call after drawing its contents, with the same `area`).
void monitorEnd (juce::Graphics&, juce::Rectangle<float> area);
/// A faint printed grid on a monitor's plot.
void monitorGrid (juce::Graphics&, juce::Rectangle<float> plot, int across = 4, int down = 4);
/// A curve in ink with a soft shadow.
void inked (juce::Graphics&, const juce::Path&, float width = 1.7f, float alpha = 0.9f);
/// A dashed ink line from `a` to `b`.
void dashed (juce::Graphics&, juce::Point<float> a, juce::Point<float> b, float alpha = 0.4f);
/// A tiny printed caption in a monitor's corner.
void monitorCaption (juce::Graphics&, juce::Rectangle<float> plot, const juce::String&, juce::Justification = juce::Justification::topLeft);

} // namespace dss1::ui::scope
