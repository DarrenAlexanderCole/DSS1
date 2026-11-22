#include "DeluxeRack.h"
#include "Hardware.h"
#include "Settings.h"
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
        bool mod = false;
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
            { "MULTI-TAP", "MULTI-TAP: four echoes spaced on the beat, each quieter than the last (AMOUNT: their level and feedback)", "SPREAD", "SPREAD: the taps across the stereo field",
              { "TIME", 0, 4, 1, 1, "1/4|||||1.5", [] (double v) {
                    const char* names[] { "1/4 BEAT", "1/2 BEAT", "3/4 BEAT", "1 BEAT", "1.5 BEAT" };
                    return juce::String (names[juce::jlimit (0, 4, juce::roundToInt (v))]); } },
              { "AMOUNT", 0, 1, 0.01, 0, "0|||||10", percent } },
            { "LADDER", "LADDER: a 24 dB resonant ladder filter", "DRIVE", "DRIVE: push the ladder into growl",
              { "CUTOFF", 0, 1, 0.005, 1, "20|||||OPEN", [] (double v) {
                    if (v > 0.99)
                        return juce::String ("OPEN");
                    const auto hz = 20.0 * std::pow (1000.0, v);
                    return hz >= 1000 ? juce::String (hz / 1000.0, 1) + "K" : juce::String (juce::roundToInt (hz)); } },
              { "RESO", 0, 1, 0.01, 0, "0|||||10", percent } },
            { "MOD", "MOD: an LFO in time with the music, moving one knob of another module (pick it under the switch)", "", "",
              { "RATE", 0, 7, 1, 3, "1/32|||||4 BAR", [] (double v) {
                    const char* names[] { "1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BAR", "4 BAR" };
                    return juce::String (names[juce::jlimit (0, 7, juce::roundToInt (v))]); } },
              { "DEPTH", 0, 1, 0.01, 0, "0|||||10", percent }, true },
        };
    }
}

// --- The DELUXE key -----------------------------------------------------------------------------------------

DeluxeKey::DeluxeKey (const juce::String& label, const juce::String& tip) : juce::Button (label)
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip (tip);
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
        g.drawText (getButtonText(), key.withTrimmedRight (18.0f), juce::Justification::centred);
    }
    else
    {
        const auto ink = hw::colour::amberMid.interpolatedWith (hw::colour::amber, breath * 0.8f + (over ? 0.2f : 0.0f));
        hw::glowText (g, getButtonText(), key.withTrimmedRight (18.0f), hw::brand (15.0f), ink, juce::Justification::centred);
    }
    hw::led (g, { key.getRight() - 13.0f, key.getCentreY() }, 4.0f, on ? hw::colour::playGreen : hw::colour::amber, on || breath > 0.35f);
    hw::glass (g, key, 3.0f);
}

// --- A module -----------------------------------------------------------------------------------------------

/// The indent at a module's top right: grab it to carry the module along the rack.
struct Grip : public juce::Component, public juce::SettableTooltipClient
{
    Grip()
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Drag to move this module along the chain");
    }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.0f);
        hw::recess (g, b, 3.0f);
        // Ridges to put a finger on.
        for (int i = 0; i < 4; ++i)
        {
            const auto y = b.getY() + 5.0f + static_cast<float> (i) * (b.getHeight() - 10.0f) / 3.0f;
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.fillRect (juce::Rectangle<float> (b.getX() + 4.0f, y - 0.5f, b.getWidth() - 8.0f, 1.0f));
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.fillRect (juce::Rectangle<float> (b.getX() + 4.0f, y + 0.5f, b.getWidth() - 8.0f, 1.0f));
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { if (onGrab) onGrab (e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (onDrop) onDrop (e); }
    std::function<void (const juce::MouseEvent&)> onGrab, onDrag, onDrop;
};

struct DeluxeRack::Module : public juce::Component, private juce::Timer
{
    Module (const Def& d, engine::Modular& s, int kindIndex) : isMod (d.mod), kind (kindIndex), sound (s), controls (&s.controls[static_cast<std::size_t> (kindIndex)])
    {
        power.setButtonText (d.name);
        power.setTooltip (d.tip);
        for (auto* t : { &power, &mode })
        {
            t->getProperties().set ("onText", "ON");
            t->getProperties().set ("offText", "OFF");
        }
        addAndMakeVisible (power);
        if (isMod)
        {
            hwStyle (target, KeyStyle::dark);
            target.setTooltip ("What MOD moves, and the shape it moves it in");
            target.onClick = [this] { showTargetMenu(); };
            addAndMakeVisible (target);
            updateTarget();
        }
        else
        {
            mode.setButtonText (d.mode);
            mode.setTooltip (d.modeTip);
            addAndMakeVisible (mode);
        }
        const Knob* ks[] { &d.a, &d.b };
        for (std::size_t i = 0; i < 2; ++i)
        {
            auto& k = knobs[i];
            k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            k.setRange (ks[i]->min, ks[i]->max, ks[i]->step);
            k.setValue (ks[i]->value, juce::dontSendNotification);
            k.setDoubleClickReturnValue (true, ks[i]->value);
            hwScale (k, ks[i]->scale);
            k.onValueChange = [this] { send(); repaint(); };
            names[i] = ks[i]->name;
            readouts[i] = ks[i]->readout;
            startValues[i] = ks[i]->value;
            addAndMakeVisible (k);
        }
        for (auto* k : { &left, &right })
        {
            hwStyle (*k, KeyStyle::dark);
            addAndMakeVisible (*k);
        }
        addAndMakeVisible (grip);
        power.onClick = [this] { send(); };
        mode.onClick = [this] { send(); };
        send();
        left.setTooltip ("Move this module earlier in the chain");
        right.setTooltip ("Move this module later in the chain");
        if (isMod)
            startTimerHz (30);
    }

    /// Sets the switches and knobs (from a patch).
    void apply (bool on, bool modeOn, double a, double b)
    {
        power.setToggleState (on, juce::dontSendNotification);
        mode.setToggleState (modeOn, juce::dontSendNotification);
        knobs[0].setValue (a, juce::dontSendNotification);
        knobs[1].setValue (b, juce::dontSendNotification);
        send();
        if (isMod)
            updateTarget();
        repaint();
    }

    void toZero()
    {
        power.setToggleState (false, juce::dontSendNotification);
        mode.setToggleState (false, juce::dontSendNotification);
        for (std::size_t i = 0; i < 2; ++i)
            knobs[i].setValue (startValues[i], juce::dontSendNotification);
        send();
        repaint();
    }

    /// The switches and knobs, to the sound.
    void send()
    {
        controls->on = power.getToggleState();
        controls->mode = mode.getToggleState();
        controls->a = static_cast<float> (knobs[0].getValue());
        controls->b = static_cast<float> (knobs[1].getValue());
    }

    // --- MOD: its target and shape ---

    static juce::String targetName (int t)
    {
        if (t < 0)
            return "NONE";
        const auto all = defs();
        const auto& d = all[static_cast<std::size_t> (t / 2)];
        return juce::String (d.name) + " " + (t % 2 == 0 ? d.a.name : d.b.name);
    }

    void updateTarget()
    {
        target.setButtonText (targetName (sound.modTarget.load()));
        repaint();
    }

    void showTargetMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("MOD MOVES");
        const auto now = sound.modTarget.load();
        const auto all = defs();
        for (int k = 0; k < engine::Modular::mod; ++k)
        {
            juce::PopupMenu knobsOf;
            for (int w = 0; w < 2; ++w)
                knobsOf.addItem (1 + k * 2 + w, w == 0 ? all[static_cast<std::size_t> (k)].a.name : all[static_cast<std::size_t> (k)].b.name, true, now == k * 2 + w);
            menu.addSubMenu (all[static_cast<std::size_t> (k)].name, knobsOf, true, nullptr, now / 2 == k && now >= 0);
        }
        menu.addItem (100, "Nothing", true, now < 0);
        menu.addSectionHeader ("SHAPE");
        const char* shapes[] { "Sine", "Triangle", "Square", "Random (steps)" };
        for (int i = 0; i < 4; ++i)
            menu.addItem (200 + i, shapes[i], true, sound.modShape.load() == i);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [safe = juce::Component::SafePointer<Module> (this)] (int r) {
            if (safe == nullptr || r == 0)
                return;
            if (r >= 200)
                safe->sound.modShape = r - 200;
            else if (r == 100)
                safe->sound.modTarget = -1;
            else
                safe->sound.modTarget = r - 1;
            safe->updateTarget();
        });
    }

    void timerCallback() override
    {
        // MOD's light follows the LFO.
        const auto v = sound.modNow.load();
        if (std::abs (v - shownMod) > 0.02f)
        {
            shownMod = v;
            repaint (lightArea().toNearestInt().expanded (4));
        }
    }

    juce::Rectangle<float> lightArea() const
    {
        return { static_cast<float> (getWidth()) - 34.0f, static_cast<float> (target.getY() + 9), 10.0f, 10.0f };
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

        if (isMod)
        {
            // The light: brighter as the LFO swings up.
            const auto on = power.getToggleState();
            const auto level = on ? 0.5f + 0.5f * shownMod : 0.0f;
            hw::led (g, lightArea().getCentre(), 4.0f, hw::colour::amber.withMultipliedBrightness (0.4f + 0.6f * level), on && level > 0.05f);
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
        grip.setBounds (getWidth() - 30, 20, 20, 28);
        const auto bottom = getLocalBounds().removeFromBottom (27).withTrimmedBottom (5);
        left.setBounds (bottom.getCentreX() - 44, bottom.getY(), 24, bottom.getHeight());
        right.setBounds (bottom.getCentreX() + 20, bottom.getY(), 24, bottom.getHeight());
        power.setBounds (r.removeFromTop (32).withTrimmedRight (26));
        auto second = r.removeFromTop (32);
        mode.setBounds (second.withTrimmedRight (26));
        target.setBounds (second.reduced (2, 4).withTrimmedRight (30));
        r.removeFromTop (18);
        auto knobRow = r.removeFromTop (48);
        const auto w = knobRow.getWidth() / 2;
        for (auto& k : knobs)
        {
            const auto cell = knobRow.removeFromLeft (w);
            const auto size = juce::jmin (cell.getWidth() - 4, cell.getHeight());
            k.setBounds (cell.withSizeKeepingCentre (size, size));
        }
    }

    const bool isMod;
    const int kind;
    engine::Modular& sound;
    engine::Modular::Controls* controls;
    juce::ToggleButton power, mode;
    juce::TextButton target;
    Grip grip;
    float shownMod = 0;
    std::array<juce::Slider, 2> knobs;
    std::array<juce::String, 2> names;
    std::array<std::function<juce::String (double)>, 2> readouts;
    std::array<double, 2> startValues {};
    juce::TextButton left { "<" }, right { ">" };
    int slot = 0;
    float shownX = 0, fromX = 0;
    int targetX = 0;
    double glideStartMs = 0;  // when it set off for its new slot (0: it's there)
    bool mover = false;       // the one being moved: it lifts off the rails and passes in front
    bool settling = false;    // just dropped: coming back down onto the rails
    float lift = 0;
};

// --- The rack -----------------------------------------------------------------------------------------------

DeluxeRack::DeluxeRack (engine::Modular& s) : sound (s)
{
    setOpaque (true);
    int i = 0;
    for (const auto& d : defs())
    {
        // The modules are listed in the sound's Kind order.
        auto m = std::make_unique<Module> (d, sound, i);
        auto* raw = m.get();
        m->grip.onGrab = [this, raw] (const juce::MouseEvent& e) { grab (*raw, e.getEventRelativeTo (this).position.x); };
        m->grip.onDrag = [this, raw] (const juce::MouseEvent& e) { carry (*raw, e.getEventRelativeTo (this).position.x); };
        m->grip.onDrop = [this, raw] (const juce::MouseEvent&) { drop (*raw); };
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
    savePatch.setTooltip ("Save the chain's order and every switch and knob as a patch");
    patches.setTooltip ("Your saved patches: load one, or delete one");
    zero.setTooltip ("Turn every module off and every knob back down (back to INIT)");
    zero.onClick = [this] { allToZero(); };
    savePatch.onClick = [this] { askToSave(); };
    patches.onClick = [this] { showPatches(); };
    for (auto& m : modules)
        for (auto* b : { static_cast<juce::Button*> (&m->power), static_cast<juce::Button*> (&m->mode) })
            b->onStateChange = [this] { repaint (patchWindow.expanded (4)); };
}

DeluxeRack::~DeluxeRack() = default;

void DeluxeRack::open (juce::Image classicBays, juce::Rectangle<int> area, bool animate)
{
    classic = std::move (classicBays);
    classicArea = area;
    introRunning = animate;
    showedSwap = showedSwapBack = ! animate;
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
    // The panel sinks into the case, then slides down into itself; the rails slide in; then the modules, one by one.
    constexpr double recessLength = 450, slideStart = 500, slideLength = 650, railStart = 1050, railLength = 600,
                     firstModule = 1550, moduleGap = 110, moduleLength = 600, barDelay = 2350, barLength = 600;
    constexpr double modulesIn = firstModule + 7 * moduleGap + moduleLength;
    // Then the show: the first module swaps along the chain and back, a light sweeps the rack,
    // and DELUXE comes up in the patch window for a moment.
    constexpr double swapAt = modulesIn + 250, swapBackAt = swapAt + 700, shineStart = swapBackAt + 650, shineLength = 1000,
                     wordStart = shineStart + 250, wordLength = 1600;
    constexpr double introLength = wordStart + wordLength + 100;
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
        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - introStartMs;
        if (! showedSwap && elapsed > swapAt)
        {
            showedSwap = true;
            move (0, 1);
        }
        if (! showedSwapBack && elapsed > swapBackAt)
        {
            showedSwapBack = true;
            move (1, -1);
        }
        if (elapsed > introLength)
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
            m->setAlpha (1.0f);
    }
    // Modules gliding to their new slots (by the clock, so it's the same speed however busy the machine is).
    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (auto& m : modules)
    {
        if (m->glideStartMs > 0)
        {
            const auto p = static_cast<float> (juce::jlimit (0.0, 1.0, (now - m->glideStartMs) / 420.0));
            m->shownX = m->fromX + (static_cast<float> (m->targetX) - m->fromX) * easeInOut (p);
            m->lift = m->mover ? std::sin (juce::MathConstants<float>::pi * p) : (m->settling ? 1.0f - easeInOut (p) : 0.0f);
            if (p >= 1.0f)
            {
                m->glideStartMs = 0;
                m->lift = 0;
                m->settling = false;
            }
            else
                busy = true;
            repaint (row.expanded (12));
        }
        else if (m.get() == carried)
        {
            busy = true;  // it's in the hand: it stays where it's held, lifted
            repaint (row.expanded (12));
        }
        else
        {
            m->shownX = static_cast<float> (m->targetX);
            m->lift = 0;
        }
        if (m->lift > 0)
        {
            const auto grow = 1.0f + 0.035f * m->lift;
            // (A component's transform works in its parent's space: scale it round its own middle.)
            m->setTransform (juce::AffineTransform::scale (grow, grow, m->shownX + static_cast<float> (m->getWidth()) * 0.5f,
                                                           static_cast<float> (row.getY() + m->getHeight() / 2)));
        }
        else if (! introRunning || juce::Time::getMillisecondCounterHiRes() - introStartMs > modulesIn)
            m->setTransform ({});
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
        const auto target = row.getX() + static_cast<int> (s) * (slotW + gap);
        sound.order[s] = order[s];
        if (&m == carried)
        {
            // In the hand: it goes where the mouse takes it, and lands in this slot when it's let go.
            m.targetX = target;
            m.left.setEnabled (s > 0);
            m.right.setEnabled (s + 1 < order.size());
            m.repaint();
            continue;
        }
        if (immediately)
        {
            m.shownX = static_cast<float> (target);
            m.glideStartMs = 0;
        }
        else if (target != m.targetX)
        {
            m.fromX = m.shownX;
            m.glideStartMs = juce::Time::getMillisecondCounterHiRes();
        }
        m.targetX = target;
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
    // The one being moved lifts and passes in front of the one making way.
    for (auto& m : modules)
        m->mover = false;
    auto& moving = *modules[static_cast<std::size_t> (order[static_cast<std::size_t> (slot)])];
    moving.mover = true;
    moving.toFront (false);
    std::swap (order[static_cast<std::size_t> (slot)], order[static_cast<std::size_t> (other)]);
    layoutModules (false);
    startTimerHz (60);
}

void DeluxeRack::grab (Module& m, float x)
{
    if (introRunning)
        return;
    carried = &m;
    carryOffset = x - m.shownX;
    m.glideStartMs = 0;
    m.mover = false;
    m.settling = false;
    m.lift = 1.0f;
    m.toFront (false);
    const auto grow = 1.035f;
    m.setTransform (juce::AffineTransform::scale (grow, grow, m.shownX + static_cast<float> (m.getWidth()) * 0.5f,
                                                  static_cast<float> (row.getY() + m.getHeight() / 2)));
    startTimerHz (60);
    repaint (row.expanded (12));
}

void DeluxeRack::carry (Module& m, float x)
{
    if (carried != &m)
        return;
    const auto slotW = static_cast<float> (m.getWidth());
    m.shownX = juce::jlimit (static_cast<float> (row.getX()), static_cast<float> (row.getRight()) - slotW, x - carryOffset);
    m.setTopLeftPosition (juce::roundToInt (m.shownX), row.getY());
    const auto grow = 1.035f;
    m.setTransform (juce::AffineTransform::scale (grow, grow, m.shownX + slotW * 0.5f, static_cast<float> (row.getY() + m.getHeight() / 2)));
    // The slot under its middle: the others make way as it passes over them.
    const auto pitch = slotW + 5.0f;
    const auto slot = juce::jlimit (0, static_cast<int> (order.size()) - 1,
                                    static_cast<int> (std::floor ((m.shownX + slotW * 0.5f - static_cast<float> (row.getX())) / pitch)));
    if (slot != m.slot)
    {
        const auto kindHere = order[static_cast<std::size_t> (m.slot)];
        order.erase (order.begin() + m.slot);
        order.insert (order.begin() + slot, kindHere);
        layoutModules (false);
        startTimerHz (60);
    }
    repaint (row.expanded (12));
}

void DeluxeRack::drop (Module& m)
{
    if (carried != &m)
        return;
    carried = nullptr;
    // Down onto the rails in its new slot.
    m.fromX = m.shownX;
    m.glideStartMs = juce::Time::getMillisecondCounterHiRes();
    m.settling = true;
    startTimerHz (60);
}

bool DeluxeRack::anythingOn() const
{
    for (const auto& m : modules)
        if (m->power.getToggleState())
            return true;
    return false;
}

namespace
{
    constexpr auto patchesKey = "deluxePatches";

    std::unique_ptr<juce::XmlElement> savedPatches()
    {
        if (auto xml = appSettings().getXmlValue (patchesKey))
            return xml;
        return std::make_unique<juce::XmlElement> ("PATCHES");
    }
}

std::unique_ptr<juce::XmlElement> DeluxeRack::capture() const
{
    auto patch = std::make_unique<juce::XmlElement> ("PATCH");
    juce::StringArray slots;
    for (auto k : order)
        slots.add (juce::String (k));
    patch->setAttribute ("order", slots.joinIntoString (","));
    patch->setAttribute ("modTarget", sound.modTarget.load());
    patch->setAttribute ("modShape", sound.modShape.load());
    for (std::size_t k = 0; k < modules.size(); ++k)
    {
        const auto& m = *modules[k];
        auto* e = patch->createNewChildElement ("MODULE");
        e->setAttribute ("kind", static_cast<int> (k));
        e->setAttribute ("on", m.power.getToggleState());
        e->setAttribute ("mode", m.mode.getToggleState());
        e->setAttribute ("a", m.knobs[0].getValue());
        e->setAttribute ("b", m.knobs[1].getValue());
    }
    return patch;
}

void DeluxeRack::restore (const juce::XmlElement& patch)
{
    sound.modTarget = patch.getIntAttribute ("modTarget", engine::Modular::ladder * 2);
    sound.modShape = juce::jlimit (0, 3, patch.getIntAttribute ("modShape", 0));
    for (auto* e : patch.getChildWithTagNameIterator ("MODULE"))
    {
        const auto k = e->getIntAttribute ("kind", -1);
        if (k >= 0 && k < static_cast<int> (modules.size()))
            modules[static_cast<std::size_t> (k)]->apply (e->getBoolAttribute ("on"), e->getBoolAttribute ("mode"),
                                                          e->getDoubleAttribute ("a"), e->getDoubleAttribute ("b"));
    }
    // The order, if it names every module once.
    std::vector<int> next;
    for (const auto& t : juce::StringArray::fromTokens (patch.getStringAttribute ("order"), ",", ""))
        next.push_back (t.getIntValue());
    auto sorted = next;
    std::sort (sorted.begin(), sorted.end());
    bool valid = sorted.size() == order.size();
    for (std::size_t i = 0; valid && i < sorted.size(); ++i)
        valid = sorted[i] == static_cast<int> (i);
    if (valid && next != order)
    {
        order = next;
        layoutModules (false);
        startTimerHz (60);
    }
    repaint();
}

void DeluxeRack::askToSave()
{
    auto* w = new juce::AlertWindow ("SAVE PATCH", "Name this patch. Saving with the name of a patch you have replaces it.",
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", patchNumber > 0 ? patchName : "PATCH " + juce::String (savedPatches()->getNumChildElements() + 1));
    w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result) {
        const auto name = w->getTextEditorContents ("name").trim().substring (0, 24);
        if (result != 1 || name.isEmpty())
            return;
        auto all = savedPatches();
        auto patch = capture();
        patch->setAttribute ("name", name);
        int number = 0;
        for (auto* e : all->getChildIterator())
        {
            ++number;
            if (e->getStringAttribute ("name").equalsIgnoreCase (name))
            {
                all->replaceChildElement (e, patch.release());
                patchNumber = number;
                break;
            }
        }
        if (patch != nullptr)
        {
            all->addChildElement (patch.release());
            patchNumber = all->getNumChildElements();
        }
        patchName = name;
        appSettings().setValue (patchesKey, all.get());
        repaint (patchWindow.expanded (4));
    }), true);
}

void DeluxeRack::showPatches()
{
    auto all = savedPatches();
    juce::PopupMenu menu, remove;
    menu.addSectionHeader ("PATCHES");
    int n = 0;
    for (auto* e : all->getChildIterator())
    {
        ++n;
        const auto label = juce::String (n).paddedLeft ('0', 2) + "   " + e->getStringAttribute ("name");
        menu.addItem (n, label, true, n == patchNumber);
        remove.addItem (1000 + n, label);
    }
    if (n == 0)
        menu.addItem (-1, "No patches yet: press SAVE PATCH", false);
    menu.addSeparator();
    menu.addItem (999, "INIT (everything off and down)");
    if (n > 0)
        menu.addSubMenu ("Delete", remove);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patches), [this] (int r) {
        if (r == 0)
            return;
        auto list = savedPatches();
        if (r == 999)
        {
            allToZero();
            return;
        }
        if (r > 1000)
        {
            if (auto* e = list->getChildElement (r - 1001))
            {
                list->removeChildElement (e, true);
                appSettings().setValue (patchesKey, list.get());
                if (patchNumber == r - 1000)
                {
                    patchNumber = 0;
                    patchName = "INIT";
                }
                else if (patchNumber > r - 1000)
                    --patchNumber;
                repaint (patchWindow.expanded (4));
            }
            return;
        }
        if (auto* e = list->getChildElement (r - 1))
        {
            restore (*e);
            patchNumber = r;
            patchName = e->getStringAttribute ("name");
            repaint (patchWindow.expanded (4));
        }
    });
}

void DeluxeRack::allToZero()
{
    for (auto& m : modules)
        m->toZero();
    sound.modTarget = engine::Modular::ladder * 2;
    sound.modShape = 0;
    for (auto& m : modules)
        if (m->isMod)
            m->updateTarget();
    patchNumber = 0;
    patchName = "INIT";
    std::vector<int> init (modules.size());
    for (std::size_t i = 0; i < init.size(); ++i)
        init[i] = static_cast<int> (i);
    if (init != order)
    {
        order = init;
        layoutModules (false);
        startTimerHz (60);
    }
    repaint (patchWindow.expanded (4));
}

void DeluxeRack::resized()
{
    const auto ear = hw::RackUnit::earWidth;
    auto area = getLocalBounds().reduced (ear, 0).reduced (30, 0);
    header = area.removeFromTop (38);
    area.removeFromBottom (16);
    row = area;

    // The patch bar: the patch window and its keys, after the title.
    auto bar = header.withTrimmedLeft (stepKeySpace + 250).withTrimmedRight (210).withY (header.getY() + 11).withHeight (24);
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
    const auto titleArea = b.reduced (ear + 30.0f, 0).withTrimmedLeft (stepKeySpace).withTop (b.getY() + 14.0f).withHeight (18.0f);
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
        // For a moment at the end of the intro the window says DELUXE, brightening in and fading out.
        const auto word = introProgress (wordStart, wordLength);
        const auto wordShown = word > 0.0f && word < 1.0f;
        if (wordShown)
        {
            const auto level = juce::jmin (1.0f, word * 6.0f, (1.0f - word) * 5.0f);
            hw::glowText (g, "D E L U X E", w, hw::vfd (20.0f),
                          hw::colour::amberMid.interpolatedWith (hw::colour::amber, level).withAlpha (0.35f + 0.65f * level),
                          juce::Justification::centred);
        }
        else
        {
            hw::glowText (g, "PATCH " + juce::String (patchNumber).paddedLeft ('0', 2) + "   " + patchName.toUpperCase(),
                          w.reduced (10.0f, 0).withTrimmedRight (72.0f), hw::vfd (18.0f), hw::colour::amber);
            hw::glowText (g, anythingOn() ? "LIVE" : "PASSIVE", w.reduced (10.0f, 0), hw::vfd (16.0f), hw::colour::amberMid,
                          juce::Justification::centredRight);
        }
        hw::glass (g, w, 3.0f);
        g.endTransparencyLayer();
    }

    // The case: a black well with a rail along the top and bottom, the modules screwed to the rails.
    const auto well = row.toFloat();
    hw::recess (g, well, 3.0f);
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (row);
        const auto rails = easeInOut (introProgress (railStart, railLength));
        const auto top = well.withHeight (17.0f).translated (-(1.0f - rails) * well.getWidth(), 0);
        const auto bottom = well.withTop (well.getBottom() - 17.0f).translated ((1.0f - rails) * well.getWidth(), 0);
        for (const auto rail : { top, bottom })
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
    }

    // The shadow of a module lifted off the rails while it moves.
    for (auto& m : modules)
        if (m->lift > 0)
        {
            g.setColour (juce::Colours::black.withAlpha (0.5f * m->lift));
            g.fillRoundedRectangle (m->getBounds().toFloat().translated (0, 3.0f + 7.0f * m->lift).expanded (5.0f * m->lift), 5.0f);
        }

    // The classic panel: it sinks into the case, then slides down into itself and out of sight.
    if (introRunning && classic.isValid())
    {
        const auto sink = easeInOut (introProgress (0, recessLength));
        const auto slide = easeInOut (introProgress (slideStart, slideLength));
        if (slide < 1.0f)
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (classicArea);
            const auto full = classicArea.toFloat();
            const auto panel = full.withSizeKeepingCentre (full.getWidth() * (1.0f - 0.025f * sink), full.getHeight() * (1.0f - 0.05f * sink))
                                   .translated (0, slide * full.getHeight());
            g.drawImage (classic, panel, juce::RectanglePlacement::stretchToFit);
            // Sunk below the faceplate: less light reaches it, and the opening's edge throws a shadow onto it.
            g.setColour (juce::Colours::black.withAlpha (0.32f * sink + 0.25f * slide));
            g.fillRect (panel);
            const auto edge = 10.0f * sink;
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.7f * sink), 0, full.getY(), juce::Colours::transparentBlack, 0, full.getY() + edge, false));
            g.fillRect (full.withHeight (edge));
            g.setColour (juce::Colours::black.withAlpha (0.6f * sink));
            g.drawRect (full, 2.0f);
        }
    }
}

void DeluxeRack::paintOverChildren (juce::Graphics& g)
{
    // The ready shine: a soft band of light sweeping across the modules, left to right.
    const auto p = introProgress (shineStart, shineLength);
    if (! introRunning || p <= 0.0f || p >= 1.0f)
        return;
    const auto area = row.toFloat();
    const auto band = 220.0f;
    const auto x = area.getX() - band + easeInOut (p) * (area.getWidth() + 2.0f * band);
    juce::ColourGradient light (juce::Colours::transparentWhite, x - band, area.getY(),
                                juce::Colours::transparentWhite, x + band * 0.4f, area.getBottom(), false);
    light.addColour (0.40, juce::Colours::white.withAlpha (0.14f));
    light.addColour (0.5, juce::Colour (0xfffff3dc).withAlpha (0.5f));
    light.addColour (0.60, juce::Colours::white.withAlpha (0.14f));
    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (row);
    g.setGradientFill (light);
    g.fillRect (area);
}

} // namespace dss1::ui
