#include "DustCover.h"
#include "Hardware.h"

#include <cmath>

namespace dss1::ui
{

namespace
{
    constexpr double liftMs = 1100.0;

    float easeInOut (float t)
    {
        // A lid: a slow start as it unsticks, quick through the middle, settling as it leaves.
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow (-2.0f * t + 2.0f, 3.0f) * 0.5f;
    }
}

DustCover::DustCover()
{
    setWantsKeyboardFocus (true);
    setRepaintsOnMouseActivity (false);
    idleStartMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

void DustCover::open()
{
    if (opening)
        return;
    opening = true;
    startMs = juce::Time::getMillisecondCounterHiRes();
}

void DustCover::mouseUp (const juce::MouseEvent&) { open(); }

bool DustCover::keyPressed (const juce::KeyPress&)
{
    open();
    return true;
}

void DustCover::timerCallback()
{
    if (opening)
    {
        const auto t = static_cast<float> ((juce::Time::getMillisecondCounterHiRes() - startMs) / liftMs);
        progress = easeInOut (juce::jlimit (0.0f, 1.0f, t));
        if (t >= 1.0f)
        {
            stopTimer();
            setVisible (false);
            if (onOpened)
                onOpened();
            return;
        }
    }
    repaint();
}

void DustCover::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto lift = progress * (bounds.getHeight() + 40.0f);
    const auto glass = bounds.translated (0, -lift);
    const auto lit = hw::light();

    // The cover's shadow on the machine, just below its leading edge, sweeping up as it lifts.
    if (opening)
    {
        const auto edgeY = glass.getBottom();
        juce::ColourGradient shadow (juce::Colours::black.withAlpha (0.45f * (1.0f - progress * 0.6f)), 0, edgeY, juce::Colours::transparentBlack, 0,
                                     edgeY + 70.0f, false);
        g.setGradientFill (shadow);
        g.fillRect (bounds.withTop (edgeY).withHeight (70.0f));
    }
    if (glass.getBottom() <= 0)
        return;

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (glass.toNearestInt());

    // Smoked glass: the machine shows through, darkened and a touch warm.
    g.setColour (juce::Colour (0xff0b0907).withAlpha (0.62f));
    g.fillRect (glass);
    juce::ColourGradient tint (juce::Colour (0x22ffb060), glass.getCentreX(), glass.getCentreY(), juce::Colour (0x00000000), glass.getX(), glass.getY(), true);
    g.setGradientFill (tint);
    g.fillRect (glass);

    // The room reflected in it: two broad soft diagonal bands and a thin bright one.
    const auto reflection = [&] (float x0, float width, float alpha) {
        juce::Path band;
        const auto h = glass.getHeight();
        band.startNewSubPath (glass.getX() + x0, glass.getY());
        band.lineTo (glass.getX() + x0 + width, glass.getY());
        band.lineTo (glass.getX() + x0 + width - h * 0.55f, glass.getBottom());
        band.lineTo (glass.getX() + x0 - h * 0.55f, glass.getBottom());
        band.closeSubPath();
        juce::ColourGradient grad (juce::Colours::white.withAlpha (0.0f), glass.getX() + x0, 0, juce::Colours::white.withAlpha (0.0f),
                                   glass.getX() + x0 + width, 0, false);
        grad.addColour (0.5, juce::Colours::white.withAlpha (alpha));
        g.setGradientFill (grad);
        g.fillPath (band);
    };
    reflection (glass.getWidth() * 0.18f, glass.getWidth() * 0.22f, 0.085f);
    reflection (glass.getWidth() * 0.47f, glass.getWidth() * 0.035f, 0.12f);
    reflection (glass.getWidth() * 0.78f, glass.getWidth() * 0.16f, 0.065f);
    // A top light falling off down the lid.
    juce::ColourGradient top (juce::Colours::white.withAlpha (0.10f), 0, glass.getY(), juce::Colours::transparentWhite, 0, glass.getY() + glass.getHeight() * 0.35f, false);
    g.setGradientFill (top);
    g.fillRect (glass);

    // Dust: fine specks, always the same ones.
    juce::Random random (1979);
    for (int i = 0; i < 420; ++i)
    {
        const auto x = glass.getX() + random.nextFloat() * glass.getWidth();
        const auto y = glass.getY() + random.nextFloat() * glass.getHeight();
        const auto s = 0.6f + random.nextFloat() * 1.4f;
        g.setColour (juce::Colours::white.withAlpha (0.05f + random.nextFloat() * 0.10f));
        g.fillEllipse (x, y, s, s);
    }
    // The glass thickens towards its edges: darker there, like looking through more of it.
    juce::ColourGradient vignette (juce::Colours::transparentBlack, glass.getCentreX(), glass.getCentreY(), juce::Colours::black.withAlpha (0.35f),
                                   glass.getX(), glass.getY(), true);
    g.setGradientFill (vignette);
    g.fillRect (glass);

    // DSS1, etched: a frosted fill with a bright lower lip and a dark upper cut, like engraving in glass.
    {
        const auto size = juce::jlimit (60.0f, 190.0f, glass.getWidth() * 0.11f);
        g.setFont (hw::brand (size));
        const auto text = juce::Rectangle<float> (glass.getX(), glass.getCentreY() - size, glass.getWidth(), size * 1.6f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawText ("DSS1", text.translated (0, -1.5f), juce::Justification::centred, false);
        g.setColour (juce::Colours::white.withAlpha (0.30f));
        g.drawText ("DSS1", text.translated (0, 1.5f), juce::Justification::centred, false);
        g.setColour (juce::Colour (0xffe8e2da).withAlpha (0.22f));
        g.drawText ("DSS1", text, juce::Justification::centred, false);
        // The warm glow of the machine behind catches the etching.
        g.setColour (hw::colour::amber.withAlpha (0.05f * lit));
        g.drawText ("DSS1", text.expanded (2.0f), juce::Justification::centred, false);

        // Below it, a quiet invitation that breathes.
        const auto t = static_cast<float> ((juce::Time::getMillisecondCounterHiRes() - idleStartMs) / 1000.0);
        const auto breathe = 0.5f + 0.5f * std::sin (t * 2.2f);
        g.setFont (hw::label (13.0f));
        g.setColour (juce::Colours::white.withAlpha (opening ? 0.0f : 0.18f + 0.22f * breathe));
        g.drawText ("CLICK TO LIFT THE COVER", juce::Rectangle<float> (glass.getX(), text.getBottom() + 6.0f, glass.getWidth(), 20.0f),
                    juce::Justification::centred, false);
    }

    // The glass edge: a bright bevel all round, thicker along the leading (bottom) edge as it lifts.
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawRect (glass.reduced (1.0f), 1.5f);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawRect (glass.reduced (3.0f), 1.0f);
    const auto edge = glass.withTop (glass.getBottom() - 8.0f);
    juce::ColourGradient lip (juce::Colours::white.withAlpha (0.35f), 0, edge.getY(), juce::Colour (0xff1a1410).withAlpha (0.9f), 0, edge.getBottom(), false);
    g.setGradientFill (lip);
    g.fillRect (edge);
}

} // namespace dss1::ui
