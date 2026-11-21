#include "DeluxeRack.h"
#include "Hardware.h"
#include "Theme.h"

#include <cmath>

namespace dss1::ui
{

namespace
{
    float easeInOut (float p) { return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * p); }
    float easeOut (float p) { return 1.0f - std::pow (1.0f - p, 3.0f); }

    juce::String percent (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; }

    struct Knob
    {
        const char* name;
        double min, max, step, value;
        const char* scale;
        std::function<juce::String (double)> readout;
    };

    struct Def
    {
        const char* name;
        const char* tip;
        const char* mode;
        const char* modeTip;
        Knob a, b;
        bool blank = false;
    };

    std::vector<Def> defs()
    {
        const auto steps = [] (std::vector<juce::String> names) {
            return [names] (double v) { return names[static_cast<std::size_t> (juce::jlimit (0, static_cast<int> (names.size()) - 1, juce::roundToInt (v)))]; };
        };
        return {
            { "STUTTER", "STUTTER: beat-synced repeats, the classic remix roll", "HOLD", "HOLD: keep repeating the slice you caught",
              { "RATE", 0, 4, 1, 1, "1/32|||||1/2", steps ({ "1/32", "1/16", "1/8", "1/4", "1/2" }) },
              { "MIX", 0, 1, 0.01, 0, "0|||||10", percent } },
            { "SLICER", "SLICER: a trance gate that chops the sound in time", "TRIPLET", "TRIPLET: the pattern in threes",
              { "PATTERN", 0, 7, 1, 0, "1|||||8", [] (double v) { return "PAT " + juce::String (juce::roundToInt (v) + 1); } },
              { "DEPTH", 0, 1, 0.01, 0, "0|||||10", percent } },
            { "NOISE", "NOISE: dirt and air under the music", "VINYL", "VINYL: crackle and hiss like an old record",
              { "COLOUR", 0, 1, 0.01, 0.5, "DARK|||||BRIGHT",
                [] (double v) { return juce::String (v < 0.34 ? "BROWN" : (v < 0.67 ? "PINK" : "WHITE")); } },
              { "LEVEL", 0, 1, 0.01, 0, "0|||||10", percent } },
            { "WAVETABLE", "WAVETABLE: a wavetable oscillator ringing against the music", "FM", "FM: frequency modulation instead of ring",
              { "MORPH", 0, 1, 0.01, 0, "1|||||64", [] (double v) { return "WAVE " + juce::String (1 + juce::roundToInt (v * 63.0)); } },
              { "MIX", 0, 1, 0.01, 0, "0|||||10", percent } },
            { "DRIFT", "DRIFT: wow and flutter, pitch drifting like worn tape", "TAPE", "TAPE: saturation and a darker top",
              { "WOW", 0, 1, 0.01, 0, "0|||||10", percent },
              { "FLUTTER", 0, 1, 0.01, 0, "0|||||10", percent } },
            { "MULTI-TAP", "MULTI-TAP: a four-tap delay for rhythmic echoes", "SPREAD", "SPREAD: the taps across the stereo field",
              { "TIME", 0, 4, 1, 2, "1/8|||||1", [] (double v) {
                    const char* names[] { "1/8", "1/4", "1/2", "3/4", "1" };
                    return juce::String (names[juce::jlimit (0, 4, juce::roundToInt (v))]) + " BEAT"; } },
              { "FEEDBACK", 0, 0.95, 0.01, 0, "0|||||MAX", percent } },
            { "LADDER", "LADDER: a 24 dB resonant ladder filter", "DRIVE", "DRIVE: push the ladder into growl",
              { "CUTOFF", 0, 1, 0.005, 1, "20|||||OPEN", [] (double v) {
                    if (v > 0.99)
                        return juce::String ("OPEN");
                    const auto hz = 20.0 * std::pow (1000.0, v);
                    return hz >= 1000 ? juce::String (hz / 1000.0, 1) + "K" : juce::String (juce::roundToInt (hz)); } },
              { "RESO", 0, 1, 0.01, 0, "0|||||10", percent } },
            { "EMPTY", "A free slot", "", "", {}, {}, true },
        };
    }
}

// --- The DELUXE key -----------------------------------------------------------------------------------------

DeluxeKey::DeluxeKey() : juce::Button ("DELUXE")
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("DELUXE: flip the effects into a modular rack of remix modules (everything starts turned down)");
    startTimerHz (30);
}

void DeluxeKey::timerCallback()
{
    // A slow breath while it waits; steady once it's on.
    breath = 0.5f + 0.5f * std::sin (static_cast<float> (juce::Time::getMillisecondCounterHiRes() / 1000.0) * juce::MathConstants<float>::twoPi / 2.8f);
    if (! getToggleState())
        repaint();
}

void DeluxeKey::paintButton (juce::Graphics& g, bool over, bool down)
{
    const bool on = getToggleState();
    const auto all = getLocalBounds().toFloat();
    const auto key = all.reduced (8.0f, 6.0f).translated (0, down ? 1.0f : 0.0f);

    // The backlight spilling out round the key onto the aluminium.
    const auto spill = on ? 0.5f : 0.16f + 0.30f * breath + (over ? 0.06f : 0.0f);
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (hw::colour::amber.withAlpha (spill * 0.22f));
        g.fillRoundedRectangle (key.expanded (static_cast<float> (i) * 2.0f), 5.0f + static_cast<float> (i) * 2.0f);
    }

    hw::recess (g, key, 3.0f);
    if (on)
    {
        // Lit from behind: warm amber glass.
        juce::ColourGradient lit (juce::Colour (0xffffc46a), 0, key.getY(), juce::Colour (0xffe9780f), 0, key.getBottom(), false);
        g.setGradientFill (lit);
        g.fillRoundedRectangle (key, 3.0f);
        juce::ColourGradient hot (juce::Colour (0x70fff1d0), key.getCentreX(), key.getCentreY(), juce::Colours::transparentWhite, key.getX(), key.getCentreY(), true);
        g.setGradientFill (hot);
        g.fillRoundedRectangle (key, 3.0f);
        g.setFont (hw::brand (15.0f));
        g.setColour (juce::Colour (0xff2a1203));
        g.drawText ("DELUXE", key.withTrimmedRight (18.0f), juce::Justification::centred);
    }
    else
    {
        const auto ink = hw::colour::amberMid.interpolatedWith (hw::colour::amber, breath * 0.8f + (over ? 0.2f : 0.0f));
        hw::glowText (g, "DELUXE", key.withTrimmedRight (18.0f), hw::brand (15.0f), ink, juce::Justification::centred);
    }
    hw::led (g, { key.getRight() - 13.0f, key.getCentreY() }, 4.0f, on ? hw::colour::playGreen : hw::colour::amber, on || breath > 0.35f);
    hw::glass (g, key, 3.0f);
}

// --- A module -----------------------------------------------------------------------------------------------

struct DeluxeRack::Module : public juce::Component
{
    explicit Module (const Def& d) : blank (d.blank)
    {
        if (blank)
        {
            hwStyle (add, KeyStyle::cream);
            add.setTooltip ("Add a module (the classic FILTER, ECHO, REVERB and COMP will live here too)");
            addAndMakeVisible (add);
        }
        else
        {
            power.setButtonText (d.name);
            power.setTooltip (d.tip);
            mode.setButtonText (d.mode);
            mode.setTooltip (d.modeTip);
            for (auto* t : { &power, &mode })
            {
                t->getProperties().set ("onText", "ON");
                t->getProperties().set ("offText", "OFF");
                addAndMakeVisible (*t);
            }
            const Knob* ks[] { &d.a, &d.b };
            for (std::size_t i = 0; i < 2; ++i)
            {
                auto& s = knobs[i];
                s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                s.setRange (ks[i]->min, ks[i]->max, ks[i]->step);
                s.setValue (ks[i]->value, juce::dontSendNotification);
                s.setDoubleClickReturnValue (true, ks[i]->value);
                hwScale (s, ks[i]->scale);
                s.onValueChange = [this] { repaint(); };
                names[i] = ks[i]->name;
                readouts[i] = ks[i]->readout;
                startValues[i] = ks[i]->value;
                addAndMakeVisible (s);
            }
        }
        for (auto* k : { &left, &right })
        {
            hwStyle (*k, KeyStyle::dark);
            addAndMakeVisible (*k);
        }
        left.setTooltip ("Move this module earlier in the chain");
        right.setTooltip ("Move this module later in the chain");
    }

    void toZero()
    {
        if (blank)
            return;
        power.setToggleState (false, juce::dontSendNotification);
        mode.setToggleState (false, juce::dontSendNotification);
        for (std::size_t i = 0; i < 2; ++i)
            knobs[i].setValue (startValues[i], juce::dontSendNotification);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        hw::faceplate (g, b, false);
        // The panel's edges, where it meets the next one.
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRect (b, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillRect (b.withWidth (1.0f).translated (1.0f, 0));
        for (const auto& p : { juce::Point<float> { b.getX() + 11.0f, b.getY() + 9.0f }, { b.getRight() - 11.0f, b.getY() + 9.0f },
                               { b.getX() + 11.0f, b.getBottom() - 9.0f }, { b.getRight() - 11.0f, b.getBottom() - 9.0f } })
            hw::screw (g, p, 4.5f);

        hw::silk (g, juce::String (slot + 1).paddedLeft ('0', 2), {}, juce::Rectangle<float> (b.getCentreX() - 14.0f, left.getY() + 0.0f, 28.0f, 20.0f),
                  juce::Justification::centred, 11.0f);

        if (blank)
        {
            hw::silk (g, "FREE SLOT", {}, b.withY (b.getY() + 30.0f).withHeight (16.0f), juce::Justification::centred, 12.0f);
            hw::bay (g, b.reduced (14.0f, 54.0f).withTrimmedBottom (-14.0f).translated (0, 10.0f));
            const char* lines[] { "LIBRARY", "FILTER   ECHO", "REVERB   COMP" };
            auto y = add.getBottom() + 12.0f;
            for (auto* line : lines)
            {
                hw::silk (g, line, {}, b.withY (y).withHeight (14.0f), juce::Justification::centred, 10.5f);
                y += 15.0f;
            }
            return;
        }
        for (std::size_t i = 0; i < 2; ++i)
        {
            const auto k = knobs[i].getBounds().toFloat();
            hw::silk (g, names[i], {}, k.withHeight (14.0f).translated (0, -15.0f).expanded (12.0f, 0), juce::Justification::centred, 11.0f);
            hw::readout (g, readouts[i] (knobs[i].getValue()), k.withY (k.getBottom() + 2.0f).withHeight (19.0f), hw::vfd (16.0f));
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 0);
        r.removeFromTop (18);
        const auto bottom = getLocalBounds().removeFromBottom (27).withTrimmedBottom (5);
        left.setBounds (bottom.getCentreX() - 44, bottom.getY(), 24, bottom.getHeight());
        right.setBounds (bottom.getCentreX() + 20, bottom.getY(), 24, bottom.getHeight());
        if (blank)
        {
            add.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 24, 96), 28).withY (r.getY() + 50));
            return;
        }
        power.setBounds (r.removeFromTop (32));
        mode.setBounds (r.removeFromTop (32));
        r.removeFromTop (18);
        auto knobRow = r.removeFromTop (48);
        const auto w = knobRow.getWidth() / 2;
        for (auto& s : knobs)
        {
            const auto cell = knobRow.removeFromLeft (w);
            const auto size = juce::jmin (cell.getWidth() - 4, cell.getHeight());
            s.setBounds (cell.withSizeKeepingCentre (size, size));
        }
    }

    const bool blank;
    juce::ToggleButton power, mode;
    std::array<juce::Slider, 2> knobs;
    std::array<juce::String, 2> names;
    std::array<std::function<juce::String (double)>, 2> readouts;
    std::array<double, 2> startValues {};
    juce::TextButton left { "<" }, right { ">" }, add { "+ ADD" };
    int slot = 0;
    float shownX = 0;
    int targetX = 0;
};

// --- The rack -----------------------------------------------------------------------------------------------

DeluxeRack::DeluxeRack()
{
    setOpaque (true);
    int i = 0;
    for (const auto& d : defs())
    {
        auto m = std::make_unique<Module> (d);
        m->left.onClick = [this, raw = m.get()] { move (raw->slot, -1); };
        m->right.onClick = [this, raw = m.get()] { move (raw->slot, 1); };
        addAndMakeVisible (*m);
        modules.push_back (std::move (m));
        order.push_back (i++);
    }
    for (auto* k : { &savePatch, &patches, &zero })
    {
        hwStyle (*k, KeyStyle::cream);
        addAndMakeVisible (*k);
    }
    savePatch.setTooltip ("Save this chain and its settings as a patch (coming with the sound engine)");
    patches.setTooltip ("Your saved patches (coming with the sound engine)");
    zero.setTooltip ("Turn every module off and every knob back down");
    zero.onClick = [this] { allToZero(); };
}

DeluxeRack::~DeluxeRack() = default;

void DeluxeRack::open (juce::Image classicBays, juce::Rectangle<int> area, bool animate)
{
    classic = std::move (classicBays);
    classicArea = area;
    introRunning = animate;
    introStartMs = juce::Time::getMillisecondCounterHiRes();
    layoutModules (true);
    setVisible (true);
    if (animate)
        startTimerHz (60);
    timerCallback();
}

void DeluxeRack::close()
{
    introRunning = false;
    classic = {};
    setVisible (false);
}

float DeluxeRack::introProgress (double delayMs, double lengthMs) const
{
    if (! introRunning)
        return 1.0f;
    const auto t = juce::Time::getMillisecondCounterHiRes() - introStartMs - delayMs;
    return static_cast<float> (juce::jlimit (0.0, 1.0, t / lengthMs));
}

namespace
{
    // The intro's timing (ms): the bays lift out, then the modules slide in one after another, then the patch bar.
    constexpr double liftLength = 750, firstModule = 520, moduleGap = 115, moduleLength = 620, barDelay = 1350, barLength = 600;
    constexpr double introLength = firstModule + 7 * moduleGap + moduleLength + 80;
}

void DeluxeRack::timerCallback()
{
    bool busy = false;
    if (introRunning)
    {
        busy = true;
        for (std::size_t s = 0; s < order.size(); ++s)
        {
            auto& m = *modules[static_cast<std::size_t> (order[s])];
            const auto p = easeOut (introProgress (firstModule + static_cast<double> (s) * moduleGap, moduleLength));
            m.setAlpha (p);
            m.setTransform (juce::AffineTransform::translation (0, (1.0f - p) * -16.0f));
        }
        const auto bar = easeInOut (introProgress (barDelay, barLength));
        for (auto* k : { &savePatch, &patches, &zero })
            k->setAlpha (bar);
        repaint (getLocalBounds().withBottom (row.getBottom() + 2));
        if (juce::Time::getMillisecondCounterHiRes() - introStartMs > juce::jmax (introLength, barDelay + barLength))
        {
            introRunning = false;
            classic = {};
            for (auto& m : modules)
            {
                m->setAlpha (1.0f);
                m->setTransform ({});
            }
            for (auto* k : { &savePatch, &patches, &zero })
                k->setAlpha (1.0f);
            repaint();
        }
    }
    else
    {
        for (auto& m : modules)
        {
            m->setAlpha (1.0f);
            m->setTransform ({});
        }
    }
    // Modules gliding to their new slots.
    for (auto& m : modules)
    {
        const auto gap = static_cast<float> (m->targetX) - m->shownX;
        if (std::abs (gap) > 0.5f)
        {
            m->shownX += gap * 0.2f;
            busy = true;
        }
        else
            m->shownX = static_cast<float> (m->targetX);
        m->setTopLeftPosition (juce::roundToInt (m->shownX), row.getY());
    }
    if (! busy)
        stopTimer();
}

void DeluxeRack::layoutModules (bool immediately)
{
    constexpr int gap = 5;
    const auto slotW = (row.getWidth() - 7 * gap) / 8;
    for (std::size_t s = 0; s < order.size(); ++s)
    {
        auto& m = *modules[static_cast<std::size_t> (order[s])];
        m.slot = static_cast<int> (s);
        m.targetX = row.getX() + static_cast<int> (s) * (slotW + gap);
        if (immediately)
            m.shownX = static_cast<float> (m.targetX);
        m.setBounds (juce::roundToInt (m.shownX), row.getY(), slotW, row.getHeight());
        m.left.setEnabled (s > 0);
        m.right.setEnabled (s + 1 < order.size());
        m.repaint();
    }
}

void DeluxeRack::move (int slot, int by)
{
    const auto other = slot + by;
    if (other < 0 || other >= static_cast<int> (order.size()))
        return;
    std::swap (order[static_cast<std::size_t> (slot)], order[static_cast<std::size_t> (other)]);
    layoutModules (false);
    startTimerHz (60);
}

void DeluxeRack::allToZero()
{
    for (auto& m : modules)
        m->toZero();
}

void DeluxeRack::resized()
{
    const auto ear = hw::RackUnit::earWidth;
    auto area = getLocalBounds().reduced (ear, 0).reduced (30, 0);
    header = area.removeFromTop (38);
    area.removeFromBottom (16);
    row = area;

    // The patch bar: the patch window and its keys, after the title.
    auto bar = header.withTrimmedLeft (300).withTrimmedRight (210).withY (header.getY() + 11).withHeight (24);
    patchWindow = bar.removeFromLeft (juce::jmin (280, bar.getWidth() / 2));
    bar.removeFromLeft (18);
    for (auto* k : { &savePatch, &patches, &zero })
    {
        k->setBounds (bar.removeFromLeft (112).withHeight (27).translated (0, -1));
        bar.removeFromLeft (8);
    }
    layoutModules (true);
}

void DeluxeRack::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto ear = static_cast<float> (hw::RackUnit::earWidth);
    hw::faceplate (g, b.reduced (ear, 0));
    g.setColour (juce::Colours::black);
    g.fillRect (b.withTop (b.getBottom() - 1.0f));

    // The title: EFFECTS fades to DELUXE.
    const auto titleArea = b.reduced (ear + 30.0f, 0).withTop (b.getY() + 14.0f).withHeight (18.0f);
    const auto title = easeInOut (introProgress (120, 600));
    if (title < 1.0f)
    {
        g.beginTransparencyLayer (1.0f - title);
        hw::silk (g, "EFFECTS", "PLAY OVER EVERYTHING YOU HEAR", titleArea);
        g.endTransparencyLayer();
    }
    g.beginTransparencyLayer (title);
    hw::silk (g, "DELUXE", "MODULAR REMIX RACK", titleArea);
    g.endTransparencyLayer();

    // The patch window.
    {
        const auto a = easeInOut (introProgress (barDelay, barLength));
        g.beginTransparencyLayer (a);
        const auto w = patchWindow.toFloat();
        hw::recess (g, w, 3.0f);
        hw::glowText (g, "PATCH 00   INIT", w.reduced (10.0f, 0), hw::vfd (18.0f), hw::colour::amber);
        hw::glowText (g, "PASSIVE", w.reduced (10.0f, 0), hw::vfd (16.0f), hw::colour::amberMid, juce::Justification::centredRight);
        hw::glass (g, w, 3.0f);
        g.endTransparencyLayer();
    }

    // The case: a black well with a rail along the top and bottom, the modules screwed to the rails.
    const auto well = row.toFloat();
    hw::recess (g, well, 3.0f);
    for (const auto rail : { well.withHeight (17.0f), well.withTop (well.getBottom() - 17.0f) })
    {
        hw::faceplate (g, rail, false);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (rail.withTop (rail.getBottom() - 1.0f));
        // The threaded strip the screws run along.
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (rail.reduced (4.0f, 6.0f), 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillRect (rail.reduced (4.0f, 6.0f).withTop (rail.getBottom() - 6.0f).withHeight (1.0f));
    }

    // The classic bays lifting out of the case.
    if (introRunning && classic.isValid())
    {
        const auto p = introProgress (0, liftLength);
        if (p < 1.0f)
        {
            const auto e = easeInOut (p);
            const auto dest = classicArea.toFloat().translated (0, e * 18.0f);
            // Its shadow on the case as it lifts.
            g.setColour (juce::Colours::black.withAlpha (0.35f * (1.0f - e)));
            g.fillRoundedRectangle (dest.translated (0, 6.0f + 10.0f * e).reduced (6.0f, 0), 6.0f);
            g.setOpacity (1.0f - e);
            g.drawImage (classic, dest, juce::RectanglePlacement::stretchToFit);
            g.setOpacity (1.0f);
        }
    }
}

} // namespace dss1::ui
