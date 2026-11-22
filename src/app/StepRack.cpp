#include "StepRack.h"
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

    // The intro (ms): the old panel sinks and slides away, the sequencer comes up, then its steps light in turn.
    constexpr double recessLength = 450, slideStart = 500, slideLength = 650, panelStart = 1050, panelLength = 600,
                     firstStep = 1500, stepGap = 70, stepLength = 380, wordStart = firstStep + 16 * stepGap + 200, wordLength = 1500;
    constexpr double introLength = wordStart + wordLength + 100;

    const juce::Colour stepColour (0xffe8892a);
}

/// One step: click to switch it on or off; drag up or down (or scroll) to pick the slice it plays.
struct StepRack::Pad : public juce::TextButton
{
    using juce::TextButton::clicked;
    void mouseDown (const juce::MouseEvent& e) override
    {
        startY = e.position.y;
        startSlice = slice ? slice() : 0;
        dragged = false;
        juce::TextButton::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto steps = static_cast<int> ((startY - e.position.y) / 10.0f);
        if (std::abs (startY - e.position.y) > 6.0f)
            dragged = true;
        if (dragged && setSlice)
            setSlice (startSlice + steps);
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (setSlice && slice && std::abs (w.deltaY) > 0.0f)
            setSlice (slice() + (w.deltaY > 0 ? 1 : -1));
    }
    void clicked() override
    {
        if (! dragged && onToggle)
            onToggle();
        dragged = false;
    }

    std::function<int()> slice;
    std::function<void (int)> setSlice;
    std::function<void()> onToggle;
    float startY = 0;
    int startSlice = 0;
    bool dragged = false;
    float appear = 1;  // for the intro
};

StepRack::StepRack (engine::Modular& s) : sound (s), step (s.step)
{
    setOpaque (true);
    for (int i = 0; i < 16; ++i)
    {
        auto pad = std::make_unique<Pad>();
        const auto si = static_cast<std::size_t> (i);
        pad->slice = [this, si] { return step.slice[si].load(); };
        pad->setSlice = [this, si] (int v) {
            const auto slices = juce::jmax (1, step.slices.load() > 0 ? step.slices.load() : 16);
            step.slice[si] = ((v % slices) + slices) % slices;
            repaint();
        };
        pad->onToggle = [this, si] {
            step.on[si] = ! step.on[si].load();
            refreshPads();
        };
        pad->setTooltip ("Click: this step on or off. Drag up or down (or scroll): which slice it plays");
        addAndMakeVisible (*pad);
        pads[si] = std::move (pad);
    }

    hwStyle (grab, KeyStyle::cream);
    grab.setTooltip ("Grab the next beats of what's playing, from the next beat");
    grab.onClick = [this] { step.grab = true; };
    hwStyle (clear, KeyStyle::dark);
    clear.setTooltip ("Throw the sample away, to grab again");
    clear.onClick = [this] {
        step.clear = true;
        run.setToggleState (false, juce::sendNotification);
    };
    for (auto* k : { &one, &two, &four })
    {
        hwStyle (*k, KeyStyle::dark);
        k->setClickingTogglesState (false);
        addAndMakeVisible (*k);
    }
    one.setTooltip ("GRAB takes 1 beat (4 slices)");
    two.setTooltip ("GRAB takes 2 beats (8 slices)");
    four.setTooltip ("GRAB takes 4 beats: a bar (16 slices)");
    one.onClick = [this] { setBeats (1); };
    two.onClick = [this] { setBeats (2); };
    four.onClick = [this] { setBeats (4); };
    hwStyle (reset, KeyStyle::dark);
    reset.setTooltip ("Every step on, playing the slices in order");
    reset.onClick = [this] { resetSteps(); };
    for (auto* k : { &grab, &clear, &reset })
        addAndMakeVisible (*k);

    run.setButtonText ("RUN");
    run.setTooltip ("Play the steps, in time with the music");
    run.onClick = [this] { step.running = run.getToggleState(); };
    replace.setButtonText ("REPLACE");
    replace.setTooltip ("The track goes quiet while the steps run, so you hear only them");
    replace.onClick = [this] { step.replace = replace.getToggleState(); };
    for (auto* t : { &run, &replace })
    {
        t->getProperties().set ("onText", "ON");
        t->getProperties().set ("offText", "OFF");
        addAndMakeVisible (*t);
    }

    level.setRange (0.0, 1.0, 0.01);
    level.setValue (step.level.load(), juce::dontSendNotification);
    level.setDoubleClickReturnValue (true, 0.8);
    hwScale (level, "0|||||10");
    level.onValueChange = [this] { step.level = static_cast<float> (level.getValue()); repaint(); };
    swing.setRange (0.0, 1.0, 0.01);
    swing.setValue (0.0, juce::dontSendNotification);
    swing.setDoubleClickReturnValue (true, 0.0);
    hwScale (swing, "0|||||MAX");
    swing.onValueChange = [this] { step.swing = static_cast<float> (swing.getValue()); repaint(); };
    for (auto* k : { &level, &swing })
        addAndMakeVisible (*k);

    setBeats (step.beats.load());
    refreshPads();
}

StepRack::~StepRack() = default;

void StepRack::setBeats (int beats)
{
    step.beats = beats;
    one.setToggleState (beats == 1, juce::dontSendNotification);
    two.setToggleState (beats == 2, juce::dontSendNotification);
    four.setToggleState (beats == 4, juce::dontSendNotification);
}

void StepRack::resetSteps()
{
    for (std::size_t i = 0; i < 16; ++i)
    {
        step.on[i] = true;
        step.slice[i] = static_cast<int> (i);
    }
    refreshPads();
}

void StepRack::refreshPads()
{
    const auto playing = step.playing.load();
    for (std::size_t i = 0; i < 16; ++i)
    {
        const bool on = step.on[i].load();
        const bool now = static_cast<int> (i) == playing;
        auto colour = on ? stepColour : stepColour.withMultipliedBrightness (0.3f);
        hwPad (*pads[i], now ? colour.brighter (0.8f) : colour, on || now);
        pads[i]->repaint();
    }
    repaint();
}

void StepRack::readSample()
{
    // The monitor's outline: the loudest sample under each pixel column.
    outline.clear();
    const auto length = step.length.load();
    if (step.state.load() != engine::Modular::Step::ready || length <= 0)
        return;
    const auto columns = juce::jmax (1, monitor.getWidth() - 16);
    const auto& l = sound.stepSample (0);
    const auto& r = sound.stepSample (1);
    outline.resize (static_cast<std::size_t> (columns), 0.0f);
    for (int c = 0; c < columns; ++c)
    {
        const auto from = static_cast<std::size_t> (static_cast<long long> (length) * c / columns);
        const auto to = static_cast<std::size_t> (static_cast<long long> (length) * (c + 1) / columns);
        float peak = 0;
        for (auto i = from; i < to && i < l.size(); ++i)
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
        outline[static_cast<std::size_t> (c)] = peak;
    }
}

void StepRack::open (juce::Image previousPanel, juce::Rectangle<int> panelArea, bool animate)
{
    previous = std::move (previousPanel);
    previousArea = panelArea;
    introRunning = animate;
    introStartMs = juce::Time::getMillisecondCounterHiRes();
    setVisible (true);
    startTimerHz (60);
    timerCallback();
}

void StepRack::close()
{
    introRunning = false;
    previous = {};
    setVisible (false);
    stopTimer();
}

float StepRack::introProgress (double delayMs, double lengthMs) const
{
    if (! introRunning)
        return 1.0f;
    const auto t = juce::Time::getMillisecondCounterHiRes() - introStartMs - delayMs;
    return static_cast<float> (juce::jlimit (0.0, 1.0, t / lengthMs));
}

void StepRack::timerCallback()
{
    if (introRunning)
    {
        // Everything but the pads fades up with the panel; the pads come up one after another.
        const auto panel = easeInOut (introProgress (panelStart, panelLength));
        for (auto* c : std::initializer_list<juce::Component*> { &grab, &clear, &one, &two, &four, &reset, &run, &replace, &level, &swing })
            c->setAlpha (panel);
        for (std::size_t i = 0; i < 16; ++i)
        {
            const auto p = easeOut (introProgress (firstStep + static_cast<double> (i) * stepGap, stepLength));
            pads[i]->setAlpha (p);
            pads[i]->setTransform (juce::AffineTransform::translation (0, (1.0f - p) * -10.0f));
        }
        repaint();
        if (juce::Time::getMillisecondCounterHiRes() - introStartMs > introLength)
        {
            introRunning = false;
            previous = {};
            for (auto* c : getChildren())
            {
                c->setAlpha (1.0f);
                c->setTransform ({});
            }
            repaint();
        }
    }
    // Keep up with the sound: the sample arriving, the step playing.
    const auto state = step.state.load();
    if (state != shownState)
    {
        shownState = state;
        readSample();
        repaint (monitor.expanded (4));
    }
    if (state == engine::Modular::Step::grabbing || state == engine::Modular::Step::waiting)
        repaint (monitor.expanded (4));
    const auto playing = step.playing.load();
    if (playing != shownPlaying)
    {
        shownPlaying = playing;
        refreshPads();
    }
    if (run.getToggleState() != step.running.load())
        run.setToggleState (step.running.load(), juce::dontSendNotification);
}

void StepRack::resized()
{
    const auto ear = hw::RackUnit::earWidth;
    auto area = getLocalBounds().reduced (ear, 0).reduced (30, 0);
    header = area.removeFromTop (38);
    area.removeFromBottom (16);
    row = area;

    // Left: the sample monitor and the GRAB keys.
    leftBay = row.removeFromLeft (340);
    row.removeFromLeft (16);
    rightBay = row;
    row = leftBay.getUnion (rightBay);

    auto l = leftBay.reduced (14, 12);
    monitor = l.removeFromTop (108);
    l.removeFromTop (12);
    auto keys = l.removeFromTop (30);
    grab.setBounds (keys.removeFromLeft (96));
    keys.removeFromLeft (10);
    for (auto* k : { &one, &two, &four })
    {
        k->setBounds (keys.removeFromLeft (36));
        keys.removeFromLeft (4);
    }
    keys.removeFromLeft (6);
    clear.setBounds (keys.removeFromLeft (juce::jmin (84, keys.getWidth())));

    // Right: the steps, their lights above, the controls below.
    auto r = rightBay.reduced (14, 10);
    ledRow = r.removeFromTop (12);
    auto padRow = r.removeFromTop (70);
    const auto gap = 6;
    const auto padW = (padRow.getWidth() - 15 * gap) / 16;
    for (std::size_t i = 0; i < 16; ++i)
        pads[i]->setBounds (padRow.getX() + static_cast<int> (i) * (padW + gap) + (i >= 8 ? 0 : 0), padRow.getY(), padW, padRow.getHeight());
    r.removeFromTop (14);
    auto controls = r;
    run.setBounds (controls.removeFromLeft (130).withHeight (34));
    replace.setBounds (controls.removeFromLeft (150).withHeight (34));
    reset.setBounds (controls.removeFromLeft (90).withSizeKeepingCentre (84, 28).withY (controls.getY() + 3));
    controls.removeFromLeft (40);
    auto knobCell = [&controls] { auto c = controls.removeFromLeft (110); return c; };
    for (auto* k : { &level, &swing })
    {
        auto c = knobCell();
        k->setBounds (c.withTrimmedTop (16).withHeight (46).withSizeKeepingCentre (46, 46).withY (c.getY() + 16));
        controls.removeFromLeft (10);
    }
}

void StepRack::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto ear = static_cast<float> (hw::RackUnit::earWidth);
    hw::faceplate (g, b.reduced (ear, 0));
    g.setColour (juce::Colours::black);
    g.fillRect (b.withTop (b.getBottom() - 1.0f));

    const auto titleArea = b.reduced (ear + 30.0f, 0).withTrimmedLeft (static_cast<float> (DeluxeRack::stepKeySpace)).withTop (b.getY() + 14.0f).withHeight (18.0f);
    const auto title = easeInOut (introProgress (120, 600));
    if (title < 1.0f)
    {
        g.beginTransparencyLayer (1.0f - title);
        hw::silk (g, "EFFECTS", "PLAY OVER EVERYTHING YOU HEAR", titleArea);
        g.endTransparencyLayer();
    }
    g.beginTransparencyLayer (title);
    hw::silk (g, "STEP", "SAMPLE SNATCHER  ·  GRAB A BEAT, PLAY IT IN 16 STEPS", titleArea);
    g.endTransparencyLayer();

    // The case under the panel, as the old one slides away.
    hw::recess (g, row.toFloat(), 3.0f);

    // The sequencer panel coming up.
    const auto panel = easeInOut (introProgress (panelStart, panelLength));
    if (panel > 0.0f)
    {
        g.beginTransparencyLayer (panel);
        hw::faceplate (g, row.toFloat(), false);
        hw::bay (g, leftBay.toFloat());
        hw::bay (g, rightBay.toFloat());

        // The monitor.
        const auto m = monitor.toFloat();
        hw::recess (g, m, 3.0f);
        g.setColour (hw::colour::glassBlack);
        g.fillRoundedRectangle (m, 3.0f);
        auto inner = m.reduced (8.0f, 8.0f);
        const auto state = step.state.load();
        const auto word = introProgress (wordStart, wordLength);
        if (introRunning && word > 0.0f && word < 1.0f)
        {
            const auto lvl = juce::jmin (1.0f, word * 6.0f, (1.0f - word) * 5.0f);
            hw::glowText (g, "S T E P", inner, hw::vfd (30.0f), hw::colour::amberMid.interpolatedWith (hw::colour::amber, lvl).withAlpha (0.35f + 0.65f * lvl),
                          juce::Justification::centred);
        }
        else if (state == engine::Modular::Step::ready && ! outline.empty())
        {
            // The sample, its slices, and the one playing.
            const auto slices = juce::jmax (1, step.slices.load());
            const auto playing = step.playing.load();
            if (playing >= 0)
            {
                const auto s = step.slice[static_cast<std::size_t> (playing)].load() % slices;
                const auto w = inner.getWidth() / static_cast<float> (slices);
                g.setColour (hw::colour::amber.withAlpha (0.16f));
                g.fillRect (inner.withX (inner.getX() + w * static_cast<float> (s)).withWidth (w));
            }
            g.setColour (hw::colour::amber.withAlpha (0.85f));
            const auto mid = inner.getCentreY();
            for (std::size_t c = 0; c < outline.size(); ++c)
            {
                const auto h = juce::jmin (1.0f, outline[c] * 1.4f) * inner.getHeight() * 0.48f;
                g.fillRect (inner.getX() + static_cast<float> (c), mid - h, 1.0f, juce::jmax (1.0f, h * 2.0f));
            }
            for (int sl = 1; sl < slices; ++sl)
            {
                const auto x = inner.getX() + inner.getWidth() * static_cast<float> (sl) / static_cast<float> (slices);
                g.setColour (hw::colour::amber.withAlpha (sl % 4 == 0 ? 0.45f : 0.18f));
                g.fillRect (x, inner.getY(), 1.0f, inner.getHeight());
            }
            hw::glowText (g, juce::String (step.beats.load()) + (step.beats.load() == 1 ? " BEAT" : " BEATS") + "  ·  " + juce::String (slices) + " SLICES",
                          inner.removeFromTop (14.0f), hw::vfd (13.0f), hw::colour::amberMid, juce::Justification::topLeft);
        }
        else if (state == engine::Modular::Step::waiting)
            hw::glowText (g, "WAITING FOR THE BEAT", inner, hw::vfd (20.0f), hw::colour::amber, juce::Justification::centred);
        else if (state == engine::Modular::Step::grabbing)
        {
            const auto p = step.progress.load();
            g.setColour (hw::colour::amber.withAlpha (0.25f));
            g.fillRect (inner.withWidth (inner.getWidth() * p));
            hw::glowText (g, "GRABBING", inner, hw::vfd (22.0f), hw::colour::amber, juce::Justification::centred);
        }
        else
        {
            hw::glowText (g, "PRESS GRAB", inner.withTrimmedBottom (18.0f), hw::vfd (22.0f), hw::colour::amber, juce::Justification::centred);
            hw::glowText (g, "PLAY A TRACK, THEN GRAB 1, 2 OR 4 BEATS", inner.withTrimmedTop (34.0f), hw::vfd (13.0f), hw::colour::amberMid,
                          juce::Justification::centred);
        }
        hw::glass (g, m, 3.0f);

        // The step lights: the one playing lit, every fourth marked.
        const auto playing = step.playing.load();
        for (std::size_t i = 0; i < 16; ++i)
        {
            const auto pb = pads[i]->getBounds().toFloat();
            const auto lit = static_cast<int> (i) == playing;
            hw::led (g, { pb.getCentreX(), static_cast<float> (ledRow.getCentreY()) }, i % 4 == 0 ? 3.6f : 3.0f,
                     lit ? hw::colour::amber : hw::colour::amberDim, lit);
        }

        // Knob labels and readouts.
        for (const auto& [k, name, text] : { std::tuple { &level, "LEVEL", juce::String (juce::roundToInt (level.getValue() * 100)) + "%" },
                                             std::tuple { &swing, "SWING", juce::String (juce::roundToInt (swing.getValue() * 50)) + "%" } })
        {
            const auto kb = k->getBounds().toFloat();
            hw::silk (g, name, {}, kb.withHeight (14.0f).translated (0, -16.0f).expanded (20.0f, 0), juce::Justification::centred, 11.0f);
            hw::readout (g, text, kb.withX (kb.getRight() + 8.0f).withWidth (58.0f).withSizeKeepingCentre (58.0f, 22.0f), hw::vfd (17.0f));
        }
        g.endTransparencyLayer();
    }

    // The old panel sinking into the case, then sliding down into itself.
    if (introRunning && previous.isValid())
    {
        const auto sink = easeInOut (introProgress (0, recessLength));
        const auto slide = easeInOut (introProgress (slideStart, slideLength));
        if (slide < 1.0f)
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (previousArea);
            const auto full = previousArea.toFloat();
            const auto shown = full.withSizeKeepingCentre (full.getWidth() * (1.0f - 0.025f * sink), full.getHeight() * (1.0f - 0.05f * sink))
                                   .translated (0, slide * full.getHeight());
            g.drawImage (previous, shown, juce::RectanglePlacement::stretchToFit);
            g.setColour (juce::Colours::black.withAlpha (0.32f * sink + 0.25f * slide));
            g.fillRect (shown);
            g.setColour (juce::Colours::black.withAlpha (0.6f * sink));
            g.drawRect (full, 2.0f);
        }
    }
}

void StepRack::paintOverChildren (juce::Graphics& g)
{
    // Each step's number small in its corner, its slice big in the middle (a dash when it's off).
    for (std::size_t i = 0; i < 16; ++i)
    {
        const auto& pad = *pads[i];
        if (pad.getAlpha() < 0.05f)
            continue;
        const auto pb = pad.getBounds().toFloat().translated (0, pad.getTransform().getTranslationY());
        const auto area = pb.reduced (7.0f, 5.0f);
        const bool on = step.on[i].load();
        g.setFont (hw::brand (10.0f));
        g.setColour (hw::colour::silk.withAlpha (0.6f * pad.getAlpha()));
        g.drawText (juce::String (i + 1), area.reduced (3.0f, 2.0f), juce::Justification::topLeft, false);
        const auto text = on ? juce::String (step.slice[i].load() + 1) : juce::String ("-");
        g.setFont (hw::brand (22.0f));
        g.setColour (juce::Colours::black.withAlpha (0.45f * pad.getAlpha()));
        g.drawText (text, pb.translated (0, 1.5f), juce::Justification::centred, false);
        g.setColour (hw::colour::silk.withAlpha ((on ? 0.92f : 0.45f) * pad.getAlpha()));
        g.drawText (text, pb, juce::Justification::centred, false);
    }
}

} // namespace dss1::ui
