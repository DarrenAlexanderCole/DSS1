#include "EqRack.h"
#include "Hardware.h"
#include "Theme.h"

#include <cmath>

namespace dss1::ui
{

namespace
{
    juce::String hzName (double hz)
    {
        if (hz >= 1000.0)
        {
            const auto k = hz / 1000.0;
            return (k >= 10.0 || std::abs (k - std::round (k)) < 0.05 ? juce::String (juce::roundToInt (k)) : juce::String (k, 1)) + "K";
        }
        return juce::String (juce::roundToInt (hz));
    }

    juce::String dbName (double db)
    {
        if (std::abs (db) < 0.05)
            return "0";
        return (db > 0 ? "+" : "") + juce::String (db, std::abs (db - std::round (db)) < 0.05 ? 0 : 1);
    }
}

EqRack::EqRack (engine::GraphicEq& e) : eq (e)
{
    setOpaque (true);
    for (std::size_t b = 0; b < faders.size(); ++b)
    {
        auto& f = faders[b];
        f.setSliderStyle (juce::Slider::LinearVertical);
        f.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        f.setRange (-12.0, 12.0, 0.5);
        f.setValue (eq.gainDb[b].load(), juce::dontSendNotification);
        f.setDoubleClickReturnValue (true, 0.0);
        f.setTooltip (hzName (engine::GraphicEq::centres[b]) + " Hz: up to 12 dB up or down (double-click: back to 0)");
        f.onValueChange = [this, b] {
            eq.gainDb[b] = static_cast<float> (faders[b].getValue());
            repaint();
        };
        addAndMakeVisible (f);
    }
    output.setRange (-12.0, 12.0, 0.5);
    output.setValue (eq.outputDb.load(), juce::dontSendNotification);
    output.setDoubleClickReturnValue (true, 0.0);
    output.setTooltip ("OUTPUT: the level after the EQ (double-click: back to 0)");
    hwScale (output, "-12|||||+12");
    output.onValueChange = [this] {
        eq.outputDb = static_cast<float> (output.getValue());
        repaint();
    };
    addAndMakeVisible (output);

    on.setButtonText ("EQ");
    on.getProperties().set ("onText", "ON");
    on.getProperties().set ("offText", "OFF");
    on.setToggleState (eq.on.load(), juce::dontSendNotification);
    on.setTooltip ("The EQ in or out (out: the sound passes untouched)");
    on.onClick = [this] {
        eq.on = on.getToggleState();
        repaint();
    };
    addAndMakeVisible (on);

    hwStyle (flatKey, KeyStyle::cream);
    flatKey.setTooltip ("Every band back to 0");
    flatKey.onClick = [this] { flat(); };
    addAndMakeVisible (flatKey);
}

void EqRack::flat()
{
    for (auto& f : faders)
        f.setValue (0.0, juce::sendNotificationSync);
    output.setValue (0.0, juce::sendNotificationSync);
}

void EqRack::resized()
{
    const auto ear = hw::RackUnit::earWidth;
    auto area = getLocalBounds().reduced (ear, 0).reduced (30, 0);
    header = area.removeFromTop (38);
    area.removeFromBottom (16);
    row = area;

    auto r = row;
    screenBay = r.removeFromLeft (300);
    r.removeFromLeft (14);
    outBay = r.removeFromRight (150);
    r.removeFromRight (14);
    fadersBay = r;

    auto s = screenBay.reduced (14, 12);
    auto keys = s.removeFromBottom (34);
    s.removeFromBottom (10);
    screen = s;
    on.setBounds (keys.removeFromLeft (120));
    flatKey.setBounds (keys.removeFromRight (90).withSizeKeepingCentre (86, 28));

    auto f = fadersBay.reduced (10, 10);
    f.removeFromTop (22);     // the dB readouts
    f.removeFromBottom (18);  // the frequencies
    const auto w = f.getWidth() / static_cast<int> (faders.size());
    for (auto& fader : faders)
        fader.setBounds (f.removeFromLeft (w).reduced (2, 0));

    auto o = outBay.reduced (12, 10);
    o.removeFromTop (22);
    output.setBounds (o.removeFromTop (70).withSizeKeepingCentre (64, 64));
}

void EqRack::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto ear = static_cast<float> (hw::RackUnit::earWidth);
    hw::faceplate (g, b.reduced (ear, 0));
    hw::rackEar (g, b.withWidth (ear), 3);
    hw::rackEar (g, b.withLeft (b.getRight() - ear), 3);
    g.setColour (juce::Colours::black);
    g.fillRect (b.withTop (b.getBottom() - 1.0f));

    hw::silk (g, "20BAND$", "GRAPHIC EQ, THE START OF THE CHAIN: BEFORE STEP, DELUXE AND THE EFFECTS",
              header.toFloat().withTrimmedTop (14).withHeight (18));
    for (auto* bay : { &screenBay, &fadersBay, &outBay })
        hw::bay (g, bay->toFloat());

    // The curve, on an amber screen: 20 Hz to 20 kHz across, ±12 dB up and down.
    const auto sc = screen.toFloat();
    hw::recess (g, sc, 3.0f);
    g.setColour (hw::colour::glassBlack);
    g.fillRoundedRectangle (sc, 3.0f);
    auto inner = sc.reduced (10.0f, 10.0f);
    const auto xOf = [&inner] (double hz) { return inner.getX() + inner.getWidth() * static_cast<float> (std::log (hz / 20.0) / std::log (1000.0)); };
    const auto yOf = [&inner] (double db) { return inner.getCentreY() - inner.getHeight() * 0.5f * static_cast<float> (juce::jlimit (-15.0, 15.0, db) / 15.0); };
    for (const auto hz : { 100.0, 1000.0, 10000.0 })
    {
        g.setColour (hw::colour::amber.withAlpha (0.14f));
        g.fillRect (xOf (hz), inner.getY(), 1.0f, inner.getHeight());
        g.setFont (hw::label (10.0f));
        g.setColour (hw::colour::amber.withAlpha (0.45f));
        g.drawText (hz >= 1000 ? juce::String (juce::roundToInt (hz / 1000)) + "K" : juce::String (juce::roundToInt (hz)),
                    juce::Rectangle<float> (xOf (hz) + 3, inner.getBottom() - 12, 30, 12), juce::Justification::left);
    }
    for (const auto db : { -12.0, -6.0, 6.0, 12.0 })
    {
        g.setColour (hw::colour::amber.withAlpha (0.08f));
        g.fillRect (inner.getX(), yOf (db), inner.getWidth(), 1.0f);
    }
    g.setColour (hw::colour::amber.withAlpha (0.3f));
    g.fillRect (inner.getX(), yOf (0), inner.getWidth(), 1.0f);
    const bool in = on.getToggleState();
    juce::Path curve;
    for (int i = 0; i <= 160; ++i)
    {
        const auto hz = 20.0 * std::pow (1000.0, i / 160.0);
        const auto p = juce::Point<float> (xOf (hz), yOf (in ? eq.responseDb (hz) : 0.0));
        if (i == 0)
            curve.startNewSubPath (p);
        else
            curve.lineTo (p);
    }
    g.setColour (hw::colour::amber.withAlpha (in ? 0.25f : 0.1f));
    g.strokePath (curve, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (in ? hw::colour::amber : hw::colour::amberDim);
    g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    hw::glowText (g, in ? "20BAND$" : "20BAND$  OFF", inner.removeFromTop (14.0f), hw::vfd (13.0f), hw::colour::amberMid, juce::Justification::topLeft);
    hw::glass (g, sc, 3.0f);

    // Each band: its gain above, its frequency below.
    for (std::size_t i = 0; i < faders.size(); ++i)
    {
        const auto f = faders[i].getBounds().toFloat();
        hw::readout (g, dbName (faders[i].getValue()), f.withY (f.getY() - 22.0f).withHeight (18.0f).expanded (1.0f, 0), hw::vfd (13.0f));
        hw::silk (g, hzName (engine::GraphicEq::centres[i]), {}, f.withY (f.getBottom() + 3.0f).withHeight (13.0f).expanded (6.0f, 0),
                  juce::Justification::centred, 9.5f);
    }

    // OUTPUT.
    const auto o = output.getBounds().toFloat();
    hw::silk (g, "OUTPUT", {}, o.withY (o.getY() - 18.0f).withHeight (14.0f).expanded (20.0f, 0), juce::Justification::centred, 11.0f);
    hw::readout (g, dbName (output.getValue()) + " DB", o.withY (o.getBottom() + 14.0f).withSizeKeepingCentre (84.0f, 24.0f).withY (o.getBottom() + 14.0f),
                 hw::vfd (18.0f));
}

} // namespace dss1::ui
