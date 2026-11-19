#include "DesignPanel.h"
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

DesignPanel::DesignPanel (Session& s, Crate& c) : session (s), crate (c), fx (s.effects()), pad (s.effects()), sampler (s)
{
    addAndMakeVisible (sampler);
    settings = &appSettings();

    addKnob ("FREQ", -1.0, 1.0, 0.005, 0.0, "LP||||||0||||||HP", [this] (double v) { fx.filter = static_cast<float> (v); }, filterReadout);
    addKnob ("TIME", 0, 5, 1, 3, "1/8|1/4|1/2|3/4|1|2",
             [this] (double v) { fx.echoBeats = echoSteps[juce::jlimit (0, 5, juce::roundToInt (v))]; },
             [] (double v) { return juce::String (echoNames[juce::jlimit (0, 5, juce::roundToInt (v))]) + " BEAT"; });
    addKnob ("FEEDBACK", 0.0, 0.95, 0.01, 0.45, "0||||||MAX", [this] (double v) { fx.echoFeedback = static_cast<float> (v); }, percent);
    addKnob ("ECHO MIX", 0.0, 1.0, 0.01, 0.35, "0|||||10", [this] (double v) { fx.echoMix = static_cast<float> (v); }, percent);
    addKnob ("SIZE", 0.0, 1.0, 0.01, 0.6, "S|||||L", [this] (double v) { fx.reverbSize = static_cast<float> (v); }, percent);
    addKnob ("VERB MIX", 0.0, 1.0, 0.01, 0.3, "0|||||10", [this] (double v) { fx.reverbMix = static_cast<float> (v); }, percent);
    addKnob ("BITS", 2.0, 16.0, 1.0, 8.0, "2||||||16", [this] (double v) { fx.crushBits = static_cast<float> (v); },
             [] (double v) { return juce::String (juce::roundToInt (v)) + " BIT"; });
    addKnob ("RATE", 1.0, 32.0, 1.0, 4.0, "1|||||32", [this] (double v) { fx.crushRate = static_cast<float> (v); },
             [] (double v) { return "/" + juce::String (juce::roundToInt (v)); });
    addKnob ("CRUSH MIX", 0.0, 1.0, 0.01, 1.0, "0|||||10", [this] (double v) { fx.crushMix = static_cast<float> (v); }, percent);

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
    toggle (crushOn, fx.crushOn);

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
        for (auto* b : { &filterOn, &echoOn, &reverbOn, &crushOn })
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
    for (auto* b : { &playTakeButton, &saveTakeButton, &discardButton })
        addAndMakeVisible (b);
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

void DesignPanel::timerCallback()
{
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
    for (const auto& r : { filterArea, echoArea, reverbArea, crushArea })
    {
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);
    }

    // Knob names above, readouts below.
    for (const auto& k : knobs)
    {
        const auto b = k->slider.getBounds().toFloat();
        hw::silk (g, k->name, {}, b.withHeight (16).translated (0, -18), juce::Justification::centred);
        hw::glowText (g, k->readout (k->slider.getValue()), b.withY (b.getBottom() - 2).withHeight (22), hw::vfd (21.0f), hw::colour::amber,
                      juce::Justification::centred);
    }

    hw::silk (g, "XY PAD", "X FILTER   Y ECHO", padArea.toFloat().withHeight (16).translated (0, -22));
    hw::recess (g, padArea.toFloat());

    hw::silk (g, "RECORDER", "WHAT YOU HEAR, WITH EFFECTS", recorderArea.toFloat().withHeight (16).translated (0, -22));
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
    hw::glowText (g, status, t.removeFromTop (38), hw::vfd (40.0f), recording ? juce::Colour (0xffff4a3a) : hw::colour::amber);
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
        const auto mid = area.getCentreY();
        g.setColour (recording ? juce::Colour (0xffff6a4a) : hw::colour::amber);
        for (int x = 0; x < columns; ++x)
        {
            const auto from = static_cast<std::size_t> (x * perColumn);
            if (from >= takePeaks.size())
                break;
            const auto to = juce::jmin (takePeaks.size(), static_cast<std::size_t> ((x + 1) * perColumn) + 1);
            float peak = 0;
            for (auto i = from; i < to; ++i)
                peak = juce::jmax (peak, takePeaks[i]);
            const auto h = juce::jmin (1.0f, peak) * area.getHeight() * 0.48f;
            g.fillRect (area.getX() + static_cast<float> (x), mid - h, 1.0f, juce::jmax (1.0f, h * 2));
        }
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
    effectsUnit = all.removeFromTop (juce::jmin (232, all.getHeight() * 2 / 5));
    lowerUnit = all;

    // Effects: four bays sized by how many knobs they hold (1, 3, 2, 3).
    auto area = effectsUnit.reduced (hw::RackUnit::earWidth, 0).reduced (30, 0);
    area.removeFromTop (38);
    area.removeFromBottom (16);
    const int gap = 14;
    const auto slot = (area.getWidth() - 3 * gap) / 9;
    filterArea = area.removeFromLeft (slot);
    area.removeFromLeft (gap);
    echoArea = area.removeFromLeft (slot * 3);
    area.removeFromLeft (gap);
    reverbArea = area.removeFromLeft (slot * 2);
    area.removeFromLeft (gap);
    crushArea = area;

    const auto layBay = [&] (juce::Rectangle<int> bay, juce::ToggleButton& on, std::initializer_list<int> knobIndices) {
        auto inner = bay.reduced (10, 10);
        on.setBounds (inner.removeFromTop (34).removeFromLeft (130));
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
    layBay (echoArea, echoOn, { 1, 2, 3 });
    layBay (reverbArea, reverbOn, { 4, 5 });
    layBay (crushArea, crushOn, { 6, 7, 8 });

    // Lower unit: the XY pad, the sampler, the recorder.
    area = lowerUnit.reduced (hw::RackUnit::earWidth, 0).reduced (30, 0);
    area.removeFromTop (36);
    area.removeFromBottom (20);
    auto padColumn = area.removeFromLeft (juce::jmin (210, area.getWidth() / 6));
    area.removeFromLeft (30);
    auto samplerColumn = area.removeFromLeft (juce::jmin (800, area.getWidth() - 30 - 330));
    area.removeFromLeft (30);

    auto padRow = padColumn.removeFromBottom (44);
    padColumn.removeFromBottom (10);
    padArea = padColumn.withSizeKeepingCentre (padColumn.getWidth(), juce::jmin (padColumn.getHeight(), padColumn.getWidth())).withY (padColumn.getY()).reduced (4);
    pad.setBounds (padArea);
    holdButton.setBounds (padRow.removeFromLeft (110));
    resetFxButton.setBounds (padRow.removeFromRight (110));

    sampler.setBounds (samplerColumn.withTrimmedTop (-22));

    recorderArea = area;
    auto rec = area;
    auto keys = rec.removeFromTop (46);
    const auto kw = (keys.getWidth() - 3 * 8) / 4;
    for (auto* b : { &recordButton, &playTakeButton, &saveTakeButton, &discardButton })
    {
        b->setBounds (keys.removeFromLeft (kw));
        keys.removeFromLeft (8);
    }
    rec.removeFromTop (12);
    timeArea = rec.removeFromTop (96).reduced (4);
    auto bottom = rec.removeFromBottom (40);
    revealButton.setBounds (bottom.removeFromRight (130).withTrimmedTop (6));
    rec.removeFromTop (28);
    waveArea = rec.reduced (4);
}

} // namespace dss1::ui
