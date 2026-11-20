#include "DesignPanel.h"
#include "Scope.h"
#include "Hardware.h"
#include "Settings.h"
#include "Theme.h"

#include <cmath>

#include <thread>

namespace dss1::ui
{

namespace
{
    constexpr float echoSteps[] { 0.125f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f };
    const char* const echoNames[] { "1/8", "1/4", "1/2", "3/4", "1", "2" };

    juce::String filterReadout (double amount)
    {
        const auto closeness = std::abs (amount);
        if (closeness < 0.02)
            return "OPEN";
        const auto hz = amount < 0 ? 20000.0 * std::pow (60.0 / 20000.0, closeness) : 20.0 * std::pow (9000.0 / 20.0, closeness);
        const auto text = hz >= 1000 ? juce::String (hz / 1000.0, 1) + "K" : juce::String (juce::roundToInt (hz));
        return (amount < 0 ? "LP " : "HP ") + text;
    }

    juce::String percent (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; }
}

// --- XY pad -------------------------------------------------------------------------------------------------

void XyPad::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (hw::colour::glassBlack);
    g.fillRoundedRectangle (b, 4.0f);

    // Grid and axes.
    g.setColour (hw::colour::amber.withAlpha (0.07f));
    for (int i = 1; i < 8; ++i)
    {
        g.fillRect (b.getX() + b.getWidth() * static_cast<float> (i) / 8.0f, b.getY(), 1.0f, b.getHeight());
        g.fillRect (b.getX(), b.getY() + b.getHeight() * static_cast<float> (i) / 8.0f, b.getWidth(), 1.0f);
    }
    g.setColour (hw::colour::amber.withAlpha (0.25f));
    g.fillRect (b.getCentreX() - 0.5f, b.getY(), 1.0f, b.getHeight());
    hw::glowText (g, "LP", b.reduced (10).removeFromBottom (24), hw::vfd (20.0f), hw::colour::amberMid);
    hw::glowText (g, "HP", b.reduced (10).removeFromBottom (24), hw::vfd (20.0f), hw::colour::amberMid, juce::Justification::centredRight);
    hw::glowText (g, "FILTER", b.reduced (10).removeFromBottom (24), hw::vfd (20.0f), hw::colour::amberDim, juce::Justification::centred);
    hw::glowText (g, "ECHO", b.reduced (10).removeFromTop (24), hw::vfd (20.0f), hw::colour::amberMid);

    if (position)
    {
        const auto p = juce::Point<float> (b.getX() + position->x * b.getWidth(), b.getY() + (1.0f - position->y) * b.getHeight());
        g.setColour (hw::colour::amber.withAlpha (0.35f));
        g.drawLine (b.getX(), p.y, b.getRight(), p.y, 1.0f);
        g.drawLine (p.x, b.getY(), p.x, b.getBottom(), 1.0f);
        for (const auto& [r, a] : { std::pair { 22.0f, 0.12f }, { 14.0f, 0.25f } })
        {
            g.setColour (hw::colour::amber.withAlpha (a));
            g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (p));
        }
        g.setColour (juce::Colour (0xffffd9a8));
        g.fillEllipse (juce::Rectangle<float> (12, 12).withCentre (p));
    }
}

void XyPad::apply (juce::Point<float> p)
{
    if (! saved)
        saved = Saved { effects.filter.load(), effects.filterOn.load(), effects.echoMix.load(), effects.echoOn.load() };
    position = juce::Point<float> (juce::jlimit (0.0f, 1.0f, p.x), juce::jlimit (0.0f, 1.0f, p.y));
    effects.filter = position->x * 2.0f - 1.0f;
    effects.filterOn = true;
    effects.echoMix = position->y;
    effects.echoOn = position->y > 0.02f;
    repaint();
    if (onChange)
        onChange();
}

void XyPad::letGo()
{
    hold = false;
    release();
}

void XyPad::release()
{
    if (hold || ! saved)
        return;
    effects.filter = saved->filter;
    effects.filterOn = saved->filterOn;
    effects.echoMix = saved->echoMix;
    effects.echoOn = saved->echoOn;
    saved.reset();
    position.reset();
    repaint();
    if (onChange)
        onChange();
}

void XyPad::mouseDown (const juce::MouseEvent& e)
{
    apply ({ e.position.x / static_cast<float> (getWidth()), 1.0f - e.position.y / static_cast<float> (getHeight()) });
}

void XyPad::mouseDrag (const juce::MouseEvent& e)
{
    mouseDown (e);
}

void XyPad::mouseUp (const juce::MouseEvent&)
{
    release();
}

// --- Panel --------------------------------------------------------------------------------------------------

juce::File DesignPanel::recordingsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DSS1 Recordings");
}

DesignPanel::Knob& DesignPanel::addKnob (const juce::String& name, double min, double max, double step, double value,
                                         const juce::String& scale, std::function<void (double)> set,
                                         std::function<juce::String (double)> readout)
{
    auto k = std::make_unique<Knob>();
    k->name = name;
    k->readout = std::move (readout);
    k->slider.setRange (min, max, step);
    const auto key = "fx." + name;
    k->slider.setValue (settings->getDoubleValue (key, value), juce::dontSendNotification);
    k->slider.setDoubleClickReturnValue (true, value);
    hwScale (k->slider, scale);
    k->slider.onValueChange = [this, slider = &k->slider, set, key] {
        set (slider->getValue());
        settings->setValue (key, slider->getValue());
        repaint();
    };
    set (k->slider.getValue());
    addAndMakeVisible (k->slider);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

DesignPanel::DesignPanel (Session& s, Crate& c) : session (s), crate (c), fx (s.effects()), pad (s.effects()), scope (s.output())
{
    settings = &appSettings();

    addKnob ("FREQ", -1.0, 1.0, 0.005, 0.0, "LP||||||0||||||HP", [this] (double v) { fx.filter = static_cast<float> (v); }, filterReadout);
    addKnob ("TIME", 0, 5, 1, 3, "1/8|1/4|1/2|3/4|1|2",
             [this] (double v) { fx.echoBeats = echoSteps[juce::jlimit (0, 5, juce::roundToInt (v))]; },
             [] (double v) { return juce::String (echoNames[juce::jlimit (0, 5, juce::roundToInt (v))]) + " BEAT"; });
    addKnob ("FEEDBACK", 0.0, 0.95, 0.01, 0.45, "0||||||MAX", [this] (double v) { fx.echoFeedback = static_cast<float> (v); }, percent);
    addKnob ("ECHO MIX", 0.0, 1.0, 0.01, 0.35, "0|||||10", [this] (double v) { fx.echoMix = static_cast<float> (v); }, percent);
    addKnob ("SIZE", 0.0, 1.0, 0.01, 0.6, "S|||||L", [this] (double v) { fx.reverbSize = static_cast<float> (v); }, percent);
    addKnob ("VERB MIX", 0.0, 1.0, 0.01, 0.3, "0|||||10", [this] (double v) { fx.reverbMix = static_cast<float> (v); }, percent);
    // AMOUNT shows the gain reduction while the compressor is on.
    addKnob ("AMOUNT", 0.0, 1.0, 0.01, 0.4, "0|||||10", [this] (double v) { fx.compAmount = static_cast<float> (v); },
             [this] (double v) {
                 if (fx.compOn.load())
                     return juce::String (shownReduction, 1) + " DB";
                 return percent (v);
             });
    addKnob ("COMP MIX", 0.0, 1.0, 0.01, 1.0, "DRY||||||WET", [this] (double v) { fx.compMix = static_cast<float> (v); }, percent);
    // The opto's envelope, and the 3-band EQ in front of it.
    const auto ms = [] (double v) { return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " S" : juce::String (juce::roundToInt (v)) + " MS"; };
    addKnob ("ATTACK", 1.0, 200.0, 0.5, 10.0, "1||||||200", [this] (double v) { fx.compAttackMs = static_cast<float> (v); }, ms);
    addKnob ("RELEASE", 40.0, 3000.0, 1.0, 400.0, "40||||||3S", [this] (double v) { fx.compReleaseMs = static_cast<float> (v); }, ms);
    const auto db = [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " DB"; };
    addKnob ("LOW", -12.0, 12.0, 0.5, 0.0, "-12||||||0||||||+12", [this] (double v) { fx.eqLowDb = static_cast<float> (v); }, db);
    addKnob ("MID", -12.0, 12.0, 0.5, 0.0, "-12||||||0||||||+12", [this] (double v) { fx.eqMidDb = static_cast<float> (v); }, db);
    addKnob ("HIGH", -12.0, 12.0, 0.5, 0.0, "-12||||||0||||||+12", [this] (double v) { fx.eqHighDb = static_cast<float> (v); }, db);
    knobs[8]->slider.setSkewFactorFromMidPoint (20.0);
    knobs[9]->slider.setSkewFactorFromMidPoint (400.0);

    // Effects start off each time DSS1 opens; their knob settings are remembered.
    const auto toggle = [this] (juce::ToggleButton& b, std::atomic<bool>& flag) {
        b.setToggleState (false, juce::dontSendNotification);
        flag = false;
        b.onClick = [&b, &flag] { flag = b.getToggleState(); };
        addAndMakeVisible (b);
    };
    toggle (filterOn, fx.filterOn);
    toggle (echoOn, fx.echoOn);
    toggle (reverbOn, fx.reverbOn);
    toggle (compOn, fx.compOn);

    // Compressor type and switches (remembered, unlike on/off).
    const char* typeNames[] { "OPTO", "VCA", "TUBE", "FET" };
    const char* typeTips[] { "Opto: slow and smooth, lets go gently (LA-2A style)", "VCA: clean and punchy, glues a mix (SSL bus style)",
                             "Tube: vari-mu, warm, squeezes harder the harder you push (Fairchild style)", "FET: fast and aggressive, with bite (1176 style)" };
    for (int t = 0; t < 4; ++t)
    {
        auto& k = compTypeKeys[static_cast<std::size_t> (t)];
        k.setButtonText (typeNames[t]);
        k.setTooltip (typeTips[t]);
        hwStyle (k, KeyStyle::cream);
        k.onClick = [this, t] { selectCompType (t); };
        addAndMakeVisible (k);
    }
    // The compressor is an opto now (with its own attack and release), so the type keys and switches are gone.
    selectCompType (static_cast<int> (engine::Effects::CompType::opto));
    for (auto& k : compTypeKeys)
        k.setVisible (false);
    const auto sw = [this] (juce::ToggleButton& b, std::atomic<bool>& flag, const char* key, const char* on, const char* off, const char* tip) {
        b.getProperties().set ("onText", on);
        b.getProperties().set ("offText", off);
        b.setTooltip (tip);
        b.setToggleState (settings->getBoolValue (key, false), juce::dontSendNotification);
        flag = b.getToggleState();
        b.onClick = [this, &b, &flag, key] {
            flag = b.getToggleState();
            settings->setValue (key, b.getToggleState());
        };
        addAndMakeVisible (b);
    };
    sw (attackSwitch, fx.compFastAttack, "fx.compFastAttack", "FAST", "SLOW", "Attack: fast catches every transient, slow lets the punch through");
    sw (releaseSwitch, fx.compFastRelease, "fx.compFastRelease", "FAST", "SLOW", "Release: fast pumps and breathes, slow stays smooth");
    sw (ratioSwitch, fx.compHighRatio, "fx.compHighRatio", "HIGH", "LOW", "Ratio: high is close to limiting");
    sw (hpfSwitch, fx.compSidechainHpf, "fx.compSidechainHpf", "ON", "OFF", "Sidechain high-pass: the compressor ignores the low end, so the kick doesn't pump the mix");
    sw (crushSwitch, fx.crushOn, "fx.crush", "ON", "OFF", "CRUSH: 12-bit grit at a third of the rate, like an old drum sampler (with COMP on)");
    sw (driveSwitch, fx.compDrive, "fx.compDrive", "ON", "OFF", "DRIVE: pushes the sound hard into a saturator before the squeeze: grit, then fuzz (with COMP on)");
    sw (punchSwitch, fx.compPunch, "fx.compPunch", "ON", "OFF", "PUNCH: a transient shaper after the squeeze, so every hit jumps out (with COMP on)");
    sw (squashSwitch, fx.compSquash, "fx.compSquash", "ON", "OFF", "SQUASH: the compressor becomes a brick, 20:1 and very fast: pumping, breathing, loud (with COMP on)");
    sw (pingPongSwitch, fx.echoPingPong, "fx.echoPingPong", "ON", "OFF", "PING (ping pong): the repeats bounce left and right");
    sw (tapeSwitch, fx.echoTape, "fx.echoTape", "ON", "OFF", "TAPE: worn tape echo, wobbling, dark and saturated; turn FEEDBACK up for a dub howl");
    sw (duckSwitch, fx.echoDuck, "fx.echoDuck", "ON", "OFF", "DUCK: the repeats stay down while the music plays and bloom in the gaps");
    sw (freezeSwitch, fx.echoFreeze, "fx.echoFreeze", "ON", "OFF", "FREEZE: stop taking in new sound and loop what's in the echo until you turn it off");
    sw (gatedSwitch, fx.reverbGated, "fx.reverbGated", "ON", "OFF", "GATE: a big room that slams shut when each hit ends (the 80s gated snare)");
    sw (shimmerSwitch, fx.reverbShimmer, "fx.reverbShimmer", "ON", "OFF", "HALO (shimmer): the tail rises an octave into a glowing halo");
    // FREEZE always starts off: a frozen loop left on from last time would be a surprise.
    freezeSwitch.setToggleState (false, juce::dontSendNotification);
    fx.echoFreeze = false;
    for (auto* b : { &attackSwitch, &releaseSwitch, &ratioSwitch, &hpfSwitch })
    {
        b->setToggleState (false, juce::dontSendNotification);
        b->setVisible (false);
    }
    fx.compFastAttack = fx.compFastRelease = fx.compHighRatio = fx.compSidechainHpf = false;

    pad.onChange = [this] { syncFromEffects(); };
    addAndMakeVisible (pad);
    holdButton.setTooltip ("Keep the pad's effect when you let go");
    holdButton.onClick = [this] {
        pad.hold = holdButton.getToggleState();
        if (! pad.hold)
            pad.letGo();  // turning HOLD off lets go of the pad
    };
    addAndMakeVisible (holdButton);
    resetFxButton.setTooltip ("Turn every effect off");
    resetFxButton.onClick = [this] {
        for (auto* b : { &filterOn, &echoOn, &reverbOn, &compOn, &crushSwitch, &driveSwitch, &punchSwitch, &squashSwitch, &pingPongSwitch, &tapeSwitch,
                         &duckSwitch, &freezeSwitch, &gatedSwitch, &shimmerSwitch })
            b->setToggleState (false, juce::sendNotificationSync);
        fx.reset();
    };
    hwStyle (resetFxButton, KeyStyle::cream);
    addAndMakeVisible (resetFxButton);

    hwStyle (recordButton, KeyStyle::rec);
    recordButton.setTooltip ("Record what you hear, with effects. Recording again throws away a take you haven't saved.");
    recordButton.onClick = [this] { toggleRecording(); };
    addAndMakeVisible (recordButton);
    hwStyle (playTakeButton, KeyStyle::play);
    playTakeButton.setTooltip ("Play the take back on its own (the deck pauses). Click the waveform to play from there.");
    playTakeButton.onClick = [this] { toggleTakePlayback(); };
    hwStyle (saveTakeButton, KeyStyle::cue);
    saveTakeButton.setTooltip ("Keep the take: saved to Music > DSS1 Recordings and added to Catalog");
    saveTakeButton.onClick = [this] { saveTake(); };
    discardButton.setTooltip ("Throw the take away");
    discardButton.onClick = [this] { discardTake(); };
    hwStyle (toChopButton, KeyStyle::cream);
    toChopButton.setTooltip ("Slice the take on the CHOP stage (it's saved first)");
    toChopButton.onClick = [this] {
        if (pendingTake.existsAsFile())
            saveTake();
        if (savedTake.existsAsFile() && onSendToChop)
        {
            session.output().takePlayer().stop();
            onSendToChop (savedTake);
        }
    };
    for (auto* b : { &playTakeButton, &saveTakeButton, &discardButton, &toChopButton })
        addAndMakeVisible (b);
    // Input pad for the recorder: turns down what's recorded (not what you hear), for hot sources.
    const char* padNames[] { "-10", "-20", "-50" };
    for (int i = 0; i < 3; ++i)
    {
        auto& padSwitch = padSwitches[static_cast<std::size_t> (i)];
        padSwitch.setButtonText (padNames[i]);
        padSwitch.getProperties().set ("onText", "DB PAD");
        padSwitch.getProperties().set ("offText", "OFF");
        padSwitch.setTooltip (juce::String ("Record ") + padNames[i] + " dB quieter (what you hear doesn't change, unless MONITOR is on)");
        padSwitch.onClick = [this, i] { setRecordPad (padSwitches[static_cast<std::size_t> (i)].getToggleState() ? i : -1); };
        addAndMakeVisible (padSwitch);
    }
    monitorSwitch.getProperties().set ("onText", "ON");
    monitorSwitch.getProperties().set ("offText", "OFF");
    monitorSwitch.setTooltip ("MONITOR: hear exactly what's being recorded, with the input pad, so you know how the take will sound");
    monitorSwitch.onClick = [this] { session.output().setMonitorRecording (monitorSwitch.getToggleState()); };
    addAndMakeVisible (monitorSwitch);
    setRecordPad (settings->getIntValue ("recordPad", -1));
    addAndMakeVisible (scope);
    hwStyle (revealButton, KeyStyle::cream);
    revealButton.onClick = [] {
        recordingsFolder().createDirectory();
        recordingsFolder().revealToUser();
    };
    addAndMakeVisible (revealButton);

    updateRecorderKeys();
    startTimerHz (30);
}

DesignPanel::~DesignPanel()
{
    // Quitting never loses a take: one that wasn't saved or thrown away is kept in DSS1 Recordings > Unsaved Takes.
    auto& audio = session.output();
    audio.takePlayer().setTake (nullptr);
    if (audio.isRecording())
        pendingTake = audio.stopRecording();
    if (pendingTake.existsAsFile())
    {
        unsavedFolder().createDirectory();
        pendingTake.moveFileTo (unsavedFolder().getNonexistentChildFile (pendingTake.getFileNameWithoutExtension(), ".wav", false));
    }
}

juce::File DesignPanel::unsavedFolder()
{
    return recordingsFolder().getChildFile ("Unsaved Takes");
}

void DesignPanel::updateRecorderKeys()
{
    const auto& audio = session.output();
    const bool recording = audio.isRecording();
    const bool hasTake = ! recording && const_cast<AudioEngine&> (audio).takePlayer().hasTake();
    playTakeButton.setEnabled (hasTake);
    playTakeButton.setButtonText (hasTake && const_cast<AudioEngine&> (audio).takePlayer().isPlaying() ? "STOP" : "PLAY");
    saveTakeButton.setEnabled (! recording && pendingTake.existsAsFile());
    toChopButton.setEnabled (! recording && (pendingTake.existsAsFile() || savedTake.existsAsFile()));
    discardButton.setEnabled (! recording && pendingTake.existsAsFile());
}

void DesignPanel::toggleTakePlayback()
{
    auto& player = session.output().takePlayer();
    if (player.isPlaying())
        player.stop();
    else if (player.hasTake())
    {
        if (session.deck().isPlaying())
            session.deck().pause();
        player.play (player.progress());
    }
    updateRecorderKeys();
    repaint();
}

void DesignPanel::saveTake()
{
    if (! pendingTake.existsAsFile())
        return;
    const auto target = recordingsFolder().getNonexistentChildFile (pendingTake.getFileNameWithoutExtension(), ".wav", false);
    recordingsFolder().createDirectory();
    if (! pendingTake.moveFileTo (target))
    {
        recorderMessage = "COULDN'T SAVE TO DSS1 RECORDINGS";
    }
    else
    {
        crate.add ({ target });
        savedTake = target;
        lastTake = target.getFileName();
        recorderMessage = "SAVED, AND ADDED TO CATALOG";
        pendingTake = juce::File();
        takeSaved = true;
    }
    updateRecorderKeys();
    repaint();
}

void DesignPanel::discardTake()
{
    session.output().takePlayer().setTake (nullptr);
    pendingTake.deleteFile();
    pendingTake = juce::File();
    takePeaks.clear();
    lastTake.clear();
    recorderMessage = "TAKE THROWN AWAY";
    updateRecorderKeys();
    repaint();
}

void DesignPanel::mouseDown (const juce::MouseEvent& e)
{
    // Click the take's waveform to play from there.
    auto& player = session.output().takePlayer();
    if (! waveArea.contains (e.getPosition()) || session.output().isRecording() || ! player.hasTake() || takePeaks.empty())
        return;
    const auto area = waveArea.toFloat().reduced (6, 6);
    const auto blocksPerSecond = session.output().sampleRate() / 512.0;
    const auto shown = juce::jmax (static_cast<double> (takePeaks.size()), 10.0 * blocksPerSecond);
    const auto drawnWidth = area.getWidth() * static_cast<float> (static_cast<double> (takePeaks.size()) / shown);
    if (drawnWidth <= 0)
        return;
    if (session.deck().isPlaying())
        session.deck().pause();
    player.play (juce::jlimit (0.0, 1.0, static_cast<double> ((static_cast<float> (e.x) - area.getX()) / drawnWidth)));
    updateRecorderKeys();
    repaint();
}

void DesignPanel::syncFromEffects()
{
    // The pad changes the filter and echo; show it on the switches.
    filterOn.setToggleState (fx.filterOn.load(), juce::dontSendNotification);
    echoOn.setToggleState (fx.echoOn.load(), juce::dontSendNotification);
    repaint();
}

void DesignPanel::toggleRecording()
{
    auto& audio = session.output();
    if (audio.isRecording())
    {
        const auto file = audio.stopRecording();
        recordButton.setToggleState (false, juce::dontSendNotification);
        recordButton.setButtonText ("REC");
        if (file.existsAsFile())
        {
            pendingTake = file;
            takeSaved = false;
            lastTake = file.getFileName();
            recorderMessage = "PLAY IT BACK, THEN SAVE OR DISCARD";
            // Load it for playback, off the message thread.
            std::thread ([weak = juce::WeakReference<DesignPanel> (this), file] {
                std::shared_ptr<const io::DecodedAudio> take;
                try
                {
                    take = std::make_shared<io::DecodedAudio> (io::decode (file.getFullPathName().toStdString()));
                }
                catch (const std::exception&)
                {
                }
                juce::MessageManager::callAsync ([weak, file, take] {
                    if (auto* self = weak.get())
                        if (take != nullptr && self->pendingTake == file && ! self->session.output().isRecording())
                        {
                            self->session.output().takePlayer().setTake (take);
                            self->updateRecorderKeys();
                        }
                });
            }).detach();
        }
    }
    else
    {
        // Recording again throws away a take that wasn't saved.
        audio.takePlayer().setTake (nullptr);
        if (pendingTake.existsAsFile())
            pendingTake.deleteFile();
        pendingTake = juce::File();
        const auto name = "DSS1 Take " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S") + ".wav";
        const auto takes = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("DSS1 Takes");
        const auto problem = audio.startRecording (takes.getChildFile (name));
        if (problem.isNotEmpty())
        {
            recorderMessage = problem.toUpperCase();
        }
        else
        {
            takePeaks.clear();
            recordButton.setToggleState (true, juce::dontSendNotification);
            recordButton.setButtonText ("STOP");
            recorderMessage = "RECORDING WHAT YOU HEAR";
            lastTake = name;
        }
    }
    updateRecorderKeys();
    repaint();
}

void DesignPanel::setRecordPad (int which)
{
    constexpr float dbs[] { -10.0f, -20.0f, -50.0f };
    which = juce::jlimit (-1, 2, which);
    for (int i = 0; i < 3; ++i)
        padSwitches[static_cast<std::size_t> (i)].setToggleState (i == which, juce::dontSendNotification);
    session.output().setRecordPad (which >= 0 ? dbs[which] : 0.0f);
    settings->setValue ("recordPad", which);
}

void DesignPanel::selectCompType (int type)
{
    type = juce::jlimit (0, 3, type);
    fx.compType = type;
    settings->setValue ("fx.compType", type);
    for (int t = 0; t < 4; ++t)
        compTypeKeys[static_cast<std::size_t> (t)].setToggleState (t == type, juce::dontSendNotification);
}

void DesignPanel::timerCallback()
{
    // The gain-reduction readout (with a little ballistics, like a meter).
    if (fx.compOn.load() || std::abs (shownReduction) > 0.0f)
    {
        const auto gr = fx.gainReduction.load();
        shownReduction = gr < shownReduction ? gr : shownReduction + (gr - shownReduction) * 0.25f;
        if (std::abs (shownReduction) < 0.05f && ! fx.compOn.load())
            shownReduction = 0.0f;
        repaint (compArea);
    }
    for (const auto& m : compMonitors)
        repaint (m);  // they follow the knobs and the live gain reduction
    {
        auto& player = session.output().takePlayer();
        const bool playingTake = player.isPlaying();
        if (playingTake || playingTake != wasTakePlaying)
        {
            if (playingTake != wasTakePlaying)
                updateRecorderKeys();
            wasTakePlaying = playingTake;
            repaint (waveArea.expanded (6));
            repaint (timeArea);
        }
    }
    if (session.output().isRecording())
    {
        session.output().drainRecordedPeaks (takePeaks);
        repaint (timeArea.expanded (6));
        repaint (waveArea.expanded (6));
    }
}

void DesignPanel::paintCompMonitors (juce::Graphics& g)
{
    const auto ink = scope::monitorInk();
    const auto on = fx.compOn.load();
    const auto amount = juce::jlimit (0.0f, 1.0f, fx.compAmount.load());
    const auto thresholdDb = -36.0f * amount;
    constexpr float ratio = 3.0f, kneeDb = 10.0f;  // the opto's character (engine::Effects)
    const auto outFor = [&] (float inDb) {
        // Soft-knee static curve.
        const auto over = inDb - thresholdDb;
        if (over <= -kneeDb * 0.5f)
            return inDb;
        if (over >= kneeDb * 0.5f)
            return thresholdDb + over / ratio;
        const auto x = over + kneeDb * 0.5f;
        return inDb + (1.0f / ratio - 1.0f) * x * x / (2.0f * kneeDb);
    };

    // 1. EQ: the response of the low shelf (120 Hz), bell (1 kHz) and high shelf (6 kHz), 20 Hz … 20 kHz, ±12 dB.
    if (const auto area = compMonitors[0].toFloat(); ! area.isEmpty())
    {
        const auto plot = scope::monitorBegin (g, area);
        scope::monitorGrid (g, plot, 3, 4);
        const auto lo = fx.eqLowDb.load(), mid = fx.eqMidDb.load(), hi = fx.eqHighDb.load();
        const auto sig = [] (float x) { return 1.0f / (1.0f + std::exp (-2.4f * x)); };
        const auto dbAt = [&] (float hz) {
            const auto o = std::log2 (hz);
            return lo * sig (std::log2 (120.0f) - o) + mid * std::exp (-std::pow (o - std::log2 (1000.0f), 2.0f) / (2.0f * 0.85f * 0.85f))
                   + hi * sig (o - std::log2 (6000.0f));
        };
        const auto yFor = [&] (float db) { return plot.getCentreY() - juce::jlimit (-12.0f, 12.0f, db) / 12.0f * plot.getHeight() * 0.46f; };
        juce::Path curve;
        for (auto x = plot.getX(); x <= plot.getRight(); x += 1.0f)
        {
            const auto hz = 20.0f * std::pow (1000.0f, (x - plot.getX()) / plot.getWidth());
            const auto y = yFor (dbAt (hz));
            if (x <= plot.getX())
                curve.startNewSubPath (x, y);
            else
                curve.lineTo (x, y);
        }
        auto shade = curve;
        shade.lineTo (plot.getRight(), plot.getCentreY());
        shade.lineTo (plot.getX(), plot.getCentreY());
        shade.closeSubPath();
        g.setColour (ink.withAlpha (0.16f));
        g.fillPath (shade);
        scope::dashed (g, { plot.getX(), plot.getCentreY() }, { plot.getRight(), plot.getCentreY() }, 0.3f);
        scope::inked (g, curve);
        scope::monitorCaption (g, plot, "EQ");
        scope::monitorCaption (g, plot, "100   1K   10K", juce::Justification::bottomRight);
        scope::monitorEnd (g, area);
    }

    // 2. AMOUNT: the transfer curve (in → out), with a dot where the music is and the live gain reduction.
    if (const auto area = compMonitors[1].toFloat(); ! area.isEmpty())
    {
        const auto plot = scope::monitorBegin (g, area);
        scope::monitorGrid (g, plot, 4, 4);
        const auto x = [&] (float db) { return plot.getX() + (db + 48.0f) / 48.0f * plot.getWidth(); };
        const auto y = [&] (float db) { return plot.getBottom() - (juce::jlimit (-48.0f, 0.0f, db) + 48.0f) / 48.0f * plot.getHeight(); };
        scope::dashed (g, { plot.getX(), plot.getBottom() }, { plot.getRight(), plot.getY() }, 0.3f);
        juce::Path curve;
        for (float db = -48.0f; db <= 0.01f; db += 0.5f)
        {
            if (db <= -48.0f)
                curve.startNewSubPath (x (db), y (outFor (db)));
            else
                curve.lineTo (x (db), y (outFor (db)));
        }
        scope::inked (g, curve);
        // The threshold, and the gain reduction as a bar down the right edge.
        scope::dashed (g, { x (thresholdDb), plot.getY() }, { x (thresholdDb), plot.getBottom() }, 0.25f);
        const auto gr = on ? juce::jlimit (0.0f, 24.0f, -shownReduction) : 0.0f;
        if (gr > 0.05f)
        {
            const auto bar = juce::Rectangle<float> (plot.getRight() - 5.0f, plot.getY(), 4.0f, plot.getHeight() * gr / 24.0f);
            g.setColour (ink.withAlpha (0.75f));
            g.fillRect (bar);
            // Where on the curve the music sits: the input that gives this much reduction.
            float inDb = thresholdDb;
            for (float db = thresholdDb - kneeDb; db <= 0.0f; db += 0.25f)
                if (db - outFor (db) >= gr)
                {
                    inDb = db;
                    break;
                }
            g.fillEllipse (x (inDb) - 3.0f, y (outFor (inDb)) - 3.0f, 6.0f, 6.0f);
        }
        scope::monitorCaption (g, plot, on ? "GR " + juce::String (gr, 1) : juce::String ("3:1"));
        scope::monitorEnd (g, area);
    }

    // 3. ATTACK / RELEASE: how the squeeze grabs a hit and lets go (a gate of loud music, then quiet).
    if (const auto area = compMonitors[2].toFloat(); ! area.isEmpty())
    {
        const auto plot = scope::monitorBegin (g, area);
        scope::monitorGrid (g, plot, 6, 2);
        const auto attackMs = juce::jmax (0.5f, fx.compAttackMs.load()), releaseMs = juce::jmax (20.0f, fx.compReleaseMs.load());
        const auto hitMs = juce::jmax (120.0f, attackMs * 4.0f);
        const auto spanMs = hitMs + juce::jmin (releaseMs * 3.0f, 6000.0f) + 20.0f;
        const auto xFor = [&] (float ms) { return plot.getX() + ms / spanMs * plot.getWidth(); };
        // The hit (faint), then the gain reduction following it.
        g.setColour (ink.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (xFor (10.0f), plot.getY() + 4.0f, xFor (10.0f + hitMs) - xFor (10.0f), plot.getHeight() - 4.0f));
        juce::Path env;
        env.startNewSubPath (plot.getX(), plot.getBottom());
        for (auto px = plot.getX(); px <= plot.getRight(); px += 1.0f)
        {
            const auto ms = (px - plot.getX()) / plot.getWidth() * spanMs - 10.0f;
            float v = 0;
            if (ms > 0 && ms <= hitMs)
                v = 1.0f - std::exp (-ms / attackMs);
            else if (ms > hitMs)
                v = (1.0f - std::exp (-hitMs / attackMs)) * std::exp (-(ms - hitMs) / releaseMs);
            env.lineTo (px, plot.getBottom() - v * (plot.getHeight() - 6.0f));
        }
        scope::inked (g, env);
        scope::monitorCaption (g, plot, "ATTACK " + juce::String (juce::roundToInt (attackMs)) + "   RELEASE " + juce::String (juce::roundToInt (releaseMs)),
                               juce::Justification::topRight);
        scope::monitorEnd (g, area);
    }

    // 4. MIX: a drum hit as it comes out, the dry hit dashed behind it and the squeezed one blended in.
    if (const auto area = compMonitors[3].toFloat(); ! area.isEmpty())
    {
        const auto plot = scope::monitorBegin (g, area);
        const auto mix = juce::jlimit (0.0f, 1.0f, fx.compMix.load());
        const auto squeeze = 1.0f / (1.0f + 2.5f * amount);
        juce::Path dryTop, outShape;
        const auto mid = plot.getCentreY();
        std::vector<float> outH;
        for (auto px = plot.getX(); px <= plot.getRight(); px += 1.0f)
        {
            const auto t = (px - plot.getX()) / plot.getWidth();
            const auto dry = std::exp (-t * 5.0f) * 0.95f + 0.05f;
            const auto wet = juce::jmin (dry, 0.18f + (dry - 0.18f) * squeeze) * (1.0f + 0.6f * amount);
            const auto out = juce::jmin (1.0f, dry * (1.0f - mix) + wet * mix);
            if (px <= plot.getX())
                dryTop.startNewSubPath (px, mid - dry * plot.getHeight() * 0.48f);
            else
                dryTop.lineTo (px, mid - dry * plot.getHeight() * 0.48f);
            outH.push_back (out * plot.getHeight() * 0.48f);
        }
        outShape.startNewSubPath (plot.getX(), mid - outH.front());
        for (std::size_t i = 1; i < outH.size(); ++i)
            outShape.lineTo (plot.getX() + static_cast<float> (i), mid - outH[i]);
        for (auto i = outH.size(); i-- > 0;)
            outShape.lineTo (plot.getX() + static_cast<float> (i), mid + outH[i]);
        outShape.closeSubPath();
        g.setColour (ink.withAlpha (0.45f));
        g.fillPath (outShape);
        juce::Path d;
        const float dashes[] = { 3.0f, 2.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (d, dryTop, dashes, 2);
        g.setColour (ink.withAlpha (0.5f));
        g.fillPath (d);
        scope::monitorCaption (g, plot, "MIX", juce::Justification::topRight);
        scope::monitorEnd (g, area);
    }
}

void DesignPanel::paint (juce::Graphics& g)
{
    const auto ear = static_cast<float> (hw::RackUnit::earWidth);
    for (const auto& [unit, slots] : { std::pair { effectsUnit, 3 }, std::pair { lowerUnit, 3 } })
    {
        const auto b = unit.toFloat();
        hw::faceplate (g, b.reduced (ear, 0));
        hw::rackEar (g, b.withWidth (ear), slots);
        hw::rackEar (g, b.withLeft (b.getRight() - ear), slots);
        g.setColour (juce::Colours::black);
        g.fillRect (b.withTop (b.getBottom() - 1.0f));
    }

    hw::silk (g, "EFFECTS", "PLAY OVER EVERYTHING YOU HEAR", effectsUnit.reduced (hw::RackUnit::earWidth + 30, 0).withTop (effectsUnit.getY() + 14).withHeight (18).toFloat());

    // Module bays.
    for (const auto& r : { filterArea, echoArea, reverbArea, compArea })
    {
        hw::bay (g, r.toFloat());
    }

    // Knob names above, readouts below.
    for (const auto& k : knobs)
    {
        const auto b = k->slider.getBounds().toFloat();
        hw::silk (g, k->name, {}, b.withHeight (16).translated (0, -18), juce::Justification::centred);
        hw::readout (g, k->readout (k->slider.getValue()), b.withY (b.getBottom() + 1).withHeight (22), hw::vfd (20.0f));
    }

    paintCompMonitors (g);

    hw::silk (g, "XY PAD", "X FILTER   Y ECHO", padArea.toFloat().withHeight (16).translated (0, -22));
    hw::recess (g, padArea.toFloat());

    hw::silk (g, "RECORDER", "WHAT YOU HEAR, WITH EFFECTS", recorderArea.toFloat().withHeight (16).translated (0, -22));
    hw::silk (g, "INPUT PAD", "RECORDING ONLY", padSwitches[0].getBounds().toFloat().withWidth (260).withHeight (16).translated (0, -22));
    hw::recess (g, timeArea.toFloat());
    const bool recording = session.output().isRecording();
    const auto secs = session.output().recordedSeconds();
    const auto whole = static_cast<int> (secs);
    const auto clock = juce::String (whole / 60).paddedLeft ('0', 2) + ":" + juce::String (whole % 60).paddedLeft ('0', 2) + "."
                       + juce::String (static_cast<int> ((secs - whole) * 10));
    auto t = timeArea.toFloat().reduced (14, 8);
    auto& player = session.output().takePlayer();
    juce::String status = "READY";
    if (recording)
        status = "REC  " + clock;
    else if (player.hasTake())
    {
        const auto at = player.progress() * player.lengthSeconds(), len = player.lengthSeconds();
        const auto mmss = [] (double v) {
            const auto w = static_cast<int> (v);
            return juce::String (w / 60).paddedLeft ('0', 2) + ":" + juce::String (w % 60).paddedLeft ('0', 2);
        };
        status = (player.isPlaying() ? "PLAY " : (pendingTake.existsAsFile() ? "TAKE " : "SAVED ")) + mmss (at) + " / " + mmss (len);
    }
    {
        // As big as fits.
        auto size = 40.0f;
        while (size > 18.0f && juce::GlyphArrangement::getStringWidth (hw::vfd (size), status) > t.getWidth() - 30.0f)
            size -= 2.0f;
        hw::glowText (g, status, t.removeFromTop (38), hw::vfd (size), recording ? juce::Colour (0xffff4a3a) : hw::colour::amber);
    }
    hw::glowText (g, lastTake.isEmpty() ? "SAVED TAKES GO TO MUSIC > DSS1 RECORDINGS" : lastTake.toUpperCase(), t.removeFromTop (22), hw::vfd (20.0f),
                  hw::colour::amberMid);
    hw::glowText (g, recorderMessage, t.removeFromTop (22), hw::vfd (20.0f), hw::colour::amberDim);
    hw::glass (g, timeArea.toFloat());

    // The take's waveform, growing as it records (squeezed to fit once it's longer than the window).
    hw::silk (g, "TAKE", "WAVEFORM", waveArea.toFloat().withHeight (16).translated (0, -21));
    hw::recess (g, waveArea.toFloat());
    if (! takePeaks.empty())
    {
        const auto area = waveArea.toFloat().reduced (6, 6);
        const auto columns = juce::jmax (1, static_cast<int> (area.getWidth()));
        // Fixed scale of ~10 s per window width until the take is longer, then the whole take.
        const auto blocksPerSecond = session.output().sampleRate() / 512.0;
        const auto shown = juce::jmax (static_cast<double> (takePeaks.size()), 10.0 * blocksPerSecond);
        const auto perColumn = shown / columns;
        scope::graticule (g, area, 10, 4, hw::colour::amber);
        std::vector<scope::Span> cols;
        for (int x = 0; x < columns; ++x)
        {
            const auto from = static_cast<std::size_t> (x * perColumn);
            if (from >= takePeaks.size())
                break;
            const auto to = juce::jmin (takePeaks.size(), static_cast<std::size_t> ((x + 1) * perColumn) + 1);
            float peak = 0;
            for (auto i = from; i < to; ++i)
                peak = juce::jmax (peak, takePeaks[i]);
            cols.push_back ({ -juce::jmin (1.0f, peak), juce::jmin (1.0f, peak) });
        }
        scope::wave (g, area.withWidth (static_cast<float> (cols.size())), cols, 1.0f, recording ? juce::Colour (0xffff6a4a) : hw::colour::amber,
                     -1.0e9f, 0.48f);
        // The playback position.
        if (! recording && player.hasTake())
        {
            const auto x = area.getX() + static_cast<float> (player.progress() * static_cast<double> (takePeaks.size()) / perColumn);
            g.setColour (hw::colour::playGreen.withAlpha (player.isPlaying() ? 0.95f : 0.5f));
            g.fillRect (x - 1.0f, area.getY(), 2.0f, area.getHeight());
        }
    }
    else
    {
        hw::glowText (g, "PRESS REC: THE TAKE DRAWS HERE", waveArea.toFloat().reduced (12, 0), hw::vfd (20.0f), hw::colour::amberDim,
                      juce::Justification::centred);
    }
    hw::glass (g, waveArea.toFloat());
    if (recording)
        hw::led (g, { static_cast<float> (timeArea.getRight() - 18), static_cast<float> (timeArea.getY() + 18) }, 6.0f,
                 juce::Colour (0xffff3020), (juce::Time::getMillisecondCounter() / 500) % 2 == 0);
}

void DesignPanel::paintOverChildren (juce::Graphics& g)
{
    hw::glass (g, padArea.toFloat());
}

void DesignPanel::resized()
{
    auto all = getLocalBounds();
    effectsUnit = all.removeFromTop (juce::jmin (264, all.getHeight() * 9 / 20));
    lowerUnit = all;

    // Effects: four bays sized by how many knobs they hold (1, 3, 2, 3).
    auto area = effectsUnit.reduced (hw::RackUnit::earWidth, 0).reduced (30, 0);
    area.removeFromTop (38);
    area.removeFromBottom (16);
    const int gap = 14;
    const auto slot = (area.getWidth() - 3 * gap) / 13;
    filterArea = area.removeFromLeft (slot);
    area.removeFromLeft (gap);
    echoArea = area.removeFromLeft (slot * 3);
    area.removeFromLeft (gap);
    reverbArea = area.removeFromLeft (slot * 2);
    area.removeFromLeft (gap);
    compArea = area;

    const auto layBay = [&] (juce::Rectangle<int> bay, juce::ToggleButton& on, std::initializer_list<int> knobIndices,
                             std::initializer_list<juce::ToggleButton*> switches = {}) {
        auto inner = bay.reduced (10, 10);
        // A taller top row (the COMP bay's monitors live in it), so the knobs line up across every bay.
        auto topRow = inner.removeFromTop (66);
        on.setBounds (topRow.withHeight (34).removeFromLeft (switches.size() > 0 ? 104 : 130));
        // The bay's shaping switches beside its on switch, in pairs stacked two high, spread over the rest of the
        // top row.
        if (switches.size() > 0)
        {
            const auto block = topRow.withTrimmedLeft (104).withY (topRow.getY() - 2).withHeight (70);
            const auto cols = static_cast<int> ((switches.size() + 1) / 2);
            const auto cw = juce::jmin (100, block.getWidth() / cols);
            int i = 0;
            for (auto* s : switches)
            {
                s->setBounds (block.getX() + (i / 2) * cw, block.getY() + (i % 2) * 35, cw, 34);
                ++i;
            }
        }
        inner.removeFromTop (20);
        inner.removeFromBottom (22);
        const auto w = inner.getWidth() / static_cast<int> (knobIndices.size());
        for (const auto i : knobIndices)
        {
            const auto cell = inner.removeFromLeft (w);
            const auto size = juce::jmin (cell.getWidth(), cell.getHeight(), 130);
            knobs[static_cast<std::size_t> (i)]->slider.setBounds (cell.withSizeKeepingCentre (size, size));
        }
    };
    layBay (filterArea, filterOn, { 0 });
    layBay (echoArea, echoOn, { 1, 2, 3 }, { &pingPongSwitch, &tapeSwitch, &duckSwitch, &freezeSwitch });
    layBay (reverbArea, reverbOn, { 4, 5 }, { &gatedSwitch, &shimmerSwitch });
    layBay (compArea, compOn, { 10, 11, 12, 6, 8, 9, 7 }, { &driveSwitch, &squashSwitch, &punchSwitch, &crushSwitch });  // EQ, AMOUNT, ATTACK, RELEASE, MIX
    {
        // Monitors along the top of the COMP bay, centred as a group in the space right of its switch, with even
        // gaps: EQ, the transfer curve, the envelope and the mix (widths 3 : 1.4 : 2 : 1.2).
        const auto inner = compArea.reduced (10, 10);
        // Right of its switches (laid out by layBay), the monitors.
        const auto taken = crushSwitch.getRight() + 6 - inner.getX();
        const auto free = juce::Rectangle<int> (inner.getX() + taken, inner.getY(), inner.getWidth() - taken, 66);
        constexpr int monitorGap = 14, h = 58;
        const float weights[] { 3.0f, 1.4f, 2.0f, 1.2f };
        const auto margin = juce::jmax (12, free.getWidth() / 28);
        const auto usable = static_cast<float> (free.getWidth() - 2 * margin - 3 * monitorGap);
        auto x = static_cast<float> (free.getX() + margin);
        const auto y = free.getCentreY() - h / 2 + 2;
        for (int i = 0; i < 4; ++i)
        {
            const auto w = usable * weights[i] / 7.6f;
            compMonitors[static_cast<std::size_t> (i)] = juce::Rectangle<float> (x, static_cast<float> (y), w, static_cast<float> (h)).toNearestInt();
            x += w + static_cast<float> (monitorGap);
        }
    }

    // Lower unit: the XY pad, then the recorder (keys and clock, the take's waveform).
    area = lowerUnit.reduced (hw::RackUnit::earWidth, 0).reduced (30, 0);
    area.removeFromTop (36);
    area.removeFromBottom (20);
    auto padColumn = area.removeFromLeft (juce::jmin (300, area.getHeight() - 54, area.getWidth() / 4));
    area.removeFromLeft (40);

    auto padRow = padColumn.removeFromBottom (44);
    padColumn.removeFromBottom (10);
    padArea = padColumn.withSizeKeepingCentre (padColumn.getWidth(), juce::jmin (padColumn.getHeight(), padColumn.getWidth())).withY (padColumn.getY()).reduced (4);
    pad.setBounds (padArea);
    holdButton.setBounds (padRow.removeFromLeft (110));
    resetFxButton.setBounds (padRow.removeFromRight (110));

    recorderArea = area;
    auto rec = area;
    auto keys = rec.removeFromTop (46);
    revealButton.setBounds (keys.removeFromRight (130).reduced (0, 4));
    keys.removeFromRight (30);
    const auto kw = juce::jmin (130, (keys.getWidth() - 3 * 84 - 122 - 6 * 8) / 5);
    for (auto* b : { &recordButton, &playTakeButton, &saveTakeButton, &discardButton, &toChopButton })
    {
        b->setBounds (keys.removeFromLeft (kw));
        keys.removeFromLeft (8);
    }
    keys.removeFromLeft (10);
    for (auto& sw : padSwitches)
        sw.setBounds (keys.removeFromLeft (84));
    keys.removeFromLeft (12);
    monitorSwitch.setBounds (keys.removeFromLeft (110));
    rec.removeFromTop (16);
    timeArea = rec.removeFromLeft (juce::jmin (380, rec.getWidth() / 3)).reduced (4);
    scope.setBounds (timeArea.reduced (14, 10).withTrimmedTop (96));
    rec.removeFromLeft (24);
    waveArea = rec.withTrimmedTop (24).reduced (4);
}

} // namespace dss1::ui
