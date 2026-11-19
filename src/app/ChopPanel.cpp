#include "ChopPanel.h"
#include "ChopView.h"
#include "Hardware.h"
#include "Settings.h"
#include "Theme.h"

#include "core/SeratoTags.h"
#include "engine/Slicing.h"

#include <map>
#include <thread>

namespace dss1::ui
{

namespace
{
    // SLICE: MM (manual) at 6 o'clock, then 1/4 … 16 beats round the dial.
    constexpr double lengths[] { 0, 0.25, 0.5, 1, 2, 4, 8, 16 };
    const char* const lengthNames[] { "MM", "1/4", "1/2", "1", "2", "4", "8", "16" };
    // Which pad sits where: rows from the top are 13–16, 9–12, 5–8, 1–4 (like an MPC).
    constexpr int padAt (int row, int col) { return (3 - row) * 4 + col; }

    juce::Colour bankColour (int b)
    {
        const auto c = serato::defaultCueColour (b);
        return juce::Colour (c.red, c.green, c.blue);
    }

    juce::String safeName (juce::String s)
    {
        return juce::File::createLegalFileName (s.trim()).substring (0, 60);
    }

    juce::String clock (double seconds)
    {
        const auto ms = juce::roundToInt (juce::jmax (0.0, seconds) * 1000.0);
        return juce::String (ms / 60000) + ":" + juce::String ((ms / 1000) % 60).paddedLeft ('0', 2) + "." + juce::String (ms % 1000).paddedLeft ('0', 3);
    }

    juce::String keyName (int pad)
    {
        return juce::String::charToString (static_cast<juce::juce_wchar> (ChopPanel::padKeys[pad])).toUpperCase();
    }
}

bool ChopBank::empty() const noexcept
{
    return std::none_of (marks.begin(), marks.end(), [] (const auto& m) { return m.has_value(); });
}

ChopPanel::ChopPanel (Session& s) : session (s), sampler (s.output().sampler())
{
    settings = &appSettings();

    view = std::make_unique<ChopView> (*this);
    addAndMakeVisible (*view);

    for (int b = 0; b < 8; ++b)
    {
        auto& k = bankKeys[static_cast<std::size_t> (b)];
        k.setButtonText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + b)));
        k.setTooltip ("Bank " + k.getButtonText() + ": chops one track or sound file into 16 pads");
        k.onClick = [this, b] { selectBank (b); };
        addAndMakeVisible (k);
    }
    for (int p = 0; p < 16; ++p)
    {
        auto& pad = pads[static_cast<std::size_t> (p)];
        pad.setButtonText (juce::String (p + 1));
        pad.setTooltip ("Key " + keyName (p) + ". Empty: marks the playhead. Then plays its slice. Right-click for more.");
        pad.addMouseListener (this, false);  // played from mouseDown/mouseUp, so HOLD works with the mouse
        addAndMakeVisible (pad);
    }

    // Waveform keys.
    snap = static_cast<Snap> (juce::jlimit (0, 2, settings->getIntValue ("chopSnap", 2)));
    snapKey.setTooltip ("Where markers land when you place or drag them: anywhere, on the beat grid, or on the nearest zero crossing (no clicks)");
    snapKey.onClick = [this] { cycleSnap(); };
    zoomOutKey.onClick = [this] { view->zoomBy (2.0); };
    zoomInKey.onClick = [this] { view->zoomBy (0.5); };
    sliceZoomKey.setTooltip ("Zoom to the chosen pad's slice");
    sliceZoomKey.onClick = [this] {
        if (const auto& m = current().marks[static_cast<std::size_t> (selected)]; m)
            view->showRange (static_cast<double> (*m), static_cast<double> (sliceEnd (bank, selected).value_or (*m)));
    };
    wholeKey.onClick = [this] { view->showWhole(); };
    for (auto* k : { &snapKey, &zoomOutKey, &zoomInKey, &sliceZoomKey, &wholeKey })
        addAndMakeVisible (k);
    hwStyle (snapKey, KeyStyle::cream);

    // Modes.
    hold = settings->getBoolValue ("padHold", false);
    sampler.setChoke (settings->getBoolValue ("padChoke", true));
    for (auto* k : { &chopKey, &playKey })
        hwStyle (*k, KeyStyle::cream);
    chopKey.setTooltip ("CHOP: a pad press moves its mark to the playhead; a click on the waveform moves the chosen pad's mark there");
    playKey.setTooltip ("PLAY: pads play their slices (empty pads still mark); click a slice to hear it");
    chopKey.onClick = [this] { setMode (true); };
    playKey.onClick = [this] { setMode (false); };
    holdKey.setTooltip ("HOLD: a slice plays only while its pad or key is held. Off: one-shot, it plays to its end.");
    holdKey.setClickingTogglesState (true);
    holdKey.setToggleState (hold, juce::dontSendNotification);
    holdKey.onClick = [this] {
        hold = holdKey.getToggleState();
        settings->setValue ("padHold", hold);
        say (hold ? "HOLD: SLICES PLAY WHILE HELD" : "ONE-SHOT: SLICES PLAY TO THEIR END");
    };
    chokeKey.setTooltip ("CHOKE: a pad cuts the other pads of its bank, so slices play one at a time");
    chokeKey.setClickingTogglesState (true);
    chokeKey.setToggleState (sampler.chokes(), juce::dontSendNotification);
    chokeKey.onClick = [this] {
        sampler.setChoke (chokeKey.getToggleState());
        settings->setValue ("padChoke", chokeKey.getToggleState());
        say (chokeKey.getToggleState() ? "CHOKE ON: ONE SLICE AT A TIME" : "CHOKE OFF: SLICES LAYER");
    };

    // Chopping.
    hwStyle (equalKey, KeyStyle::cue);
    equalKey.setTooltip ("16 equal slices: the loop that's playing, or 16 x SLICE beats from the playhead (a sound file: all of it)");
    equalKey.onClick = [this] { chopEqual(); };
    hwStyle (transientKey, KeyStyle::cue);
    transientKey.setTooltip ("A slice at each of the 16 strongest hits: in the loop that's playing, or in what's on screen");
    transientKey.onClick = [this] { chopTransients(); };
    undoKey.setTooltip ("Undo the last change to the marks or pad settings (Cmd+Z)");
    undoKey.onClick = [this] { undo(); };
    nudgeLeft.setTooltip ("Nudge the chosen marker earlier (Left arrow; Shift: further)");
    nudgeRight.setTooltip ("Nudge the chosen marker later (Right arrow; Shift: further)");
    nudgeLeft.onClick = [this] { nudge (-1, juce::ModifierKeys::currentModifiers.isShiftDown()); };
    nudgeRight.onClick = [this] { nudge (1, juce::ModifierKeys::currentModifiers.isShiftDown()); };

    // A detented selector all the way round: MM (manual) at 6 o'clock, then 1/4 … 16 beats.
    sliceKnob.setRange (0, 7, 1);
    sliceKnob.setRotaryParameters (juce::MathConstants<float>::pi, juce::MathConstants<float>::pi * 2.75f, true);
    sliceKnob.setValue (settings->getIntValue ("sliceLength", settings->getIntValue ("sliceBeats", 2) + 1), juce::dontSendNotification);
    previousSliceSetting = sliceKnob.getValue();
    sliceKnob.setTooltip ("Beats per slice for EQUAL 16, and how long the last slice plays. MM (manual): you set the end with the END marker, "
                          "and EQUAL 16 chops what's on screen.");
    hwScale (sliceKnob, "MM|1/4|1/2|1|2|4|8|16");
    sliceKnob.onValueChange = [this] {
        if (manualLength() && previousSliceSetting >= 0.5)
        {
            // Into MM: each bank's last slice keeps the length it had, now as an END marker you can move.
            for (auto& b : bankData)
                if (b.audio != nullptr && ! b.empty())
                {
                    Frame last = 0;
                    for (const auto& m : b.marks)
                        if (m)
                            last = juce::jmax (last, *m);
                    if (! b.chopEnd || *b.chopEnd <= last)
                    {
                        const auto perBeat = b.framesPerBeat > 0 ? b.framesPerBeat : b.audio->info.sampleRate * 0.5;
                        b.chopEnd = juce::jmin (static_cast<Frame> (b.audio->channels.front().size()),
                                                last + static_cast<Frame> (lengths[juce::jlimit (1, 7, static_cast<int> (previousSliceSetting))] * perBeat));
                    }
                }
            saveKit();
        }
        previousSliceSetting = sliceKnob.getValue();
        settings->setValue ("sliceLength", static_cast<int> (sliceKnob.getValue()));
        for (int b = 0; b < 8; ++b)
            updateRegions (b);
        say (manualLength() ? "MM: DRAG THE END MARKER TO SET WHERE THE LAST SLICE ENDS" : juce::String ("LAST SLICE: ") + lengthNames[static_cast<int> (sliceKnob.getValue())] + " BEAT");
        repaint();
    };

    loadKey.setTooltip ("Chop a sound file in this bank instead of the track on the deck");
    loadKey.onClick = [this] { loadFile(); };
    hwStyle (saveKey, KeyStyle::cue);
    saveKey.setTooltip ("Write this bank's slices (with their pad settings) as WAVs into the samples folder (Bank A … Bank H)");
    saveKey.onClick = [this] { saveBank(); };
    clearKey.setTooltip ("Clear the chosen pad's mark");
    clearKey.onClick = [this] { clearPad (selected); };
    clearBankKey.setTooltip ("Clear every mark in this bank, to chop something else");
    clearBankKey.onClick = [this] { clearBank(); };
    stopKey.onClick = [this] {
        sampler.stopAll();
        session.output().chopPreview().stop();
    };
    showSavedKey.setTooltip ("Open the samples folder in Finder, to see what SAVE BANK has written");
    showSavedKey.onClick = [this] {
        const auto folder = samplesFolder();
        folder.createDirectory();
        const auto bankFolder = folder.getChildFile ("Bank " + slotName (slotOf (0)).substring (0, 1));
        (bankFolder.isDirectory() ? bankFolder : folder).startAsProcess();
    };
    hwStyle (folderKey, KeyStyle::cream);
    folderKey.setTooltip ("Choose the folder SAVE BANK writes to");
    folderKey.onClick = [this] { chooseFolder(); };

    // The chosen pad's settings.
    const auto setupKnob = [this] (juce::Slider& k, double lo, double hi, double step, double def, const juce::String& scale, const juce::String& tip) {
        k.setRange (lo, hi, step);
        k.setValue (def, juce::dontSendNotification);
        k.setDoubleClickReturnValue (true, def);
        k.setTooltip (tip);
        hwScale (k, scale);
        k.onDragStart = [this] { rememberForUndo(); };
        k.onValueChange = [this] {
            auto& st = current().settings[static_cast<std::size_t> (selected)];
            st.gainDb = static_cast<float> (gainKnob.getValue());
            st.semitones = static_cast<float> (tuneKnob.getValue());
            st.attackMs = static_cast<float> (attackKnob.getValue());
            st.fadeMs = static_cast<float> (fadeKnob.getValue());
            pushSettings (bank, selected);
            saveKit();
            repaint (padSettingsArea);
        };
        addAndMakeVisible (k);
    };
    setupKnob (gainKnob, -24, 12, 0.5, 0, "-24|0|+12", "Pad level in dB (double-click: 0)");
    tuneKnob.setSkewFactor (1.0);
    setupKnob (tuneKnob, -24, 24, 1, 0, "-24|0|+24", "Pad tune in semitones: plays faster and higher, or slower and lower (double-click: 0)");
    setupKnob (attackKnob, 0, 500, 1, 0, "0|500", "Fade in, ms (double-click: 0)");
    setupKnob (fadeKnob, 0, 1000, 1, 0, "0|1000", "Fade out before the end of the slice, ms (double-click: 0)");
    attackKnob.setSkewFactorFromMidPoint (60);
    fadeKnob.setSkewFactorFromMidPoint (120);
    reverseKey.setClickingTogglesState (true);
    reverseKey.setTooltip ("Play the chosen pad's slice backwards");
    reverseKey.onClick = [this] {
        rememberForUndo();
        current().settings[static_cast<std::size_t> (selected)].reverse = reverseKey.getToggleState();
        pushSettings (bank, selected);
        saveKit();
    };

    for (auto* k : { &chopKey, &playKey, &holdKey, &chokeKey, &equalKey, &transientKey, &undoKey, &nudgeLeft, &nudgeRight, &loadKey, &saveKey,
                     &clearKey, &clearBankKey, &stopKey, &folderKey, &reverseKey, &showSavedKey, &midiKey })
        addAndMakeVisible (k);
    addAndMakeVisible (sliceKnob);

    // MIDI: pads from any controller (Akai MPD / MPK pads send notes 36–51 out of the box), and play.
    {
        const auto stored = juce::StringArray::fromTokens (settings->getValue ("midiPadNotes"), ",", {});
        for (int p = 0; p < 16; ++p)
            padNotes[static_cast<std::size_t> (p)] = stored.size() == 16 ? stored[p].getIntValue() : 36 + p;
        playController = settings->getIntValue ("midiPlayCC", -1);
        playNote = settings->getIntValue ("midiPlayNote", -1);
    }
    midi.onNoteOn = [this] (int note, float velocity) { midiNoteOn (note, velocity); };
    midi.onNoteOff = [this] (int note) { midiNoteOff (note); };
    midi.onControl = [this] (int cc, int value) { midiControl (cc, value); };
    midi.onTransport = [this] (bool play) { midiPlay (play); };
    midi.onActivity = [this] {
        midiSeenAt = juce::Time::getMillisecondCounter();
        repaint (midiArea);
    };
    hwStyle (midiKey, KeyStyle::cream);
    midiKey.setTooltip ("MIDI LEARN: hit a controller pad for each DSS1 pad in turn (click a DSS1 pad to choose which), and press the controller's "
                        "play button to use it for play. Right-click for presets.");
    midiKey.onClick = [this] {
        if (juce::ModifierKeys::currentModifiers.isPopupMenu())
            return;
        setLearning (! learning);
    };
    midiKey.addMouseListener (this, false);

    cycleSnap();
    cycleSnap();
    cycleSnap();  // (sets the label for the stored choice)
    setMode (false);
    selectBank (0);
    restoreKit();
    message = "PRESS A PAD (OR ITS KEY) TO MARK A SLICE";
    startTimerHz (30);
}

ChopPanel::~ChopPanel()
{
    session.output().chopPreview().setTake (nullptr);
}

juce::File ChopPanel::samplesFolder() const
{
    const auto chosen = settings->getValue ("samplesFolder");
    if (chosen.isNotEmpty() && juce::File::isAbsolutePath (chosen))
        return juce::File (chosen);
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DSS1 Samples");
}

juce::String ChopPanel::slotName (int slot) const
{
    return juce::String::charToString (static_cast<juce::juce_wchar> ('A' + slot / 16)) + juce::String (slot % 16 + 1).paddedLeft ('0', 2);
}

bool ChopPanel::sourceIsOnDeck (const ChopBank& b) const
{
    const auto* record = session.record();
    return record != nullptr && b.source == juce::File (juce::String::fromUTF8 (record->identity.path.c_str()));
}

const io::DecodedAudio* ChopPanel::shownAudio() const
{
    if (const auto& b = current(); b.audio != nullptr)
        return b.audio.get();
    return std::as_const (session).audio().get();
}

juce::String ChopPanel::describe (int pad) const
{
    const auto& b = current();
    const auto head = "PAD " + juce::String (pad + 1) + " [" + keyName (pad) + "]  ";
    const auto& m = b.marks[static_cast<std::size_t> (pad)];
    if (! m || b.audio == nullptr)
        return head + "EMPTY: PRESS TO MARK";
    const auto rate = b.audio->info.sampleRate;
    const auto end = sliceEnd (bank, pad).value_or (*m);
    return head + clock (static_cast<double> (*m) / rate) + "  " + juce::String (static_cast<double> (end - *m) / rate, 3) + " S";
}

void ChopPanel::say (const juce::String& text)
{
    message = text;
    messageTime = juce::Time::getMillisecondCounter();
    repaint (messageArea);
    repaint (readoutArea);
}

void ChopPanel::setMode (bool chop)
{
    chopMode = chop;
    chopKey.setToggleState (chop, juce::dontSendNotification);
    playKey.setToggleState (! chop, juce::dontSendNotification);
    say (chop ? "CHOP: PADS MOVE TO THE PLAYHEAD, CLICK THE WAVEFORM TO PLACE" : "PLAY: PADS PLAY THEIR SLICES, CLICK A SLICE TO HEAR IT");
}

void ChopPanel::cycleSnap()
{
    snap = static_cast<Snap> ((static_cast<int> (snap) + 1) % 3);
    settings->setValue ("chopSnap", static_cast<int> (snap));
    snapKey.setButtonText (snap == Snap::off ? "SNAP: OFF" : snap == Snap::beat ? "SNAP: BEAT" : "SNAP: ZERO");
}

Frame ChopPanel::snapped (Frame at) const
{
    const auto* audio = shownAudio();
    if (audio == nullptr)
        return at;
    if (snap == Snap::beat)
    {
        const auto& b = current();
        if (const auto* record = session.record(); record != nullptr && record->grid && ! record->grid->isEmpty() && (b.audio == nullptr || sourceIsOnDeck (b)))
            return static_cast<Frame> (record->grid->frameAt (std::round (record->grid->beatAt (static_cast<double> (at)))));
        return at;
    }
    if (snap == Snap::zero)
        return engine::nearestZeroCrossing (*audio, at, static_cast<std::int64_t> (audio->info.sampleRate * 0.005));
    return at;
}

void ChopPanel::selectBank (int b)
{
    bank = juce::jlimit (0, 7, b);
    for (int i = 0; i < 8; ++i)
        bankKeys[static_cast<std::size_t> (i)].setToggleState (i == bank, juce::dontSendNotification);
    session.output().chopPreview().stop();
    const auto& cb = current();
    if (cb.audio != nullptr)
        say ("BANK " + slotName (slotOf (0)).substring (0, 1) + ": " + cb.source.getFileNameWithoutExtension().toUpperCase());
    view->reset();
    showPadSettings();
    refreshPads();
    repaint();
}

void ChopPanel::selectPad (int pad)
{
    selected = pad;
    showPadSettings();
    refreshPads();
    repaint();
}

void ChopPanel::showPadSettings()
{
    const auto& st = current().settings[static_cast<std::size_t> (selected)];
    gainKnob.setValue (st.gainDb, juce::dontSendNotification);
    tuneKnob.setValue (st.semitones, juce::dontSendNotification);
    attackKnob.setValue (st.attackMs, juce::dontSendNotification);
    fadeKnob.setValue (st.fadeMs, juce::dontSendNotification);
    reverseKey.setToggleState (st.reverse, juce::dontSendNotification);
    repaint (padSettingsArea);
}

void ChopPanel::refreshPads()
{
    const auto& b = current();
    for (int p = 0; p < 16; ++p)
    {
        const bool marked = b.marks[static_cast<std::size_t> (p)].has_value() && b.audio != nullptr;
        const bool on = sampler.isPlaying (slotOf (p));
        auto colour = bankColour (bank);
        if (! marked)
            colour = colour.withMultipliedBrightness (0.35f);
        hwPad (pads[static_cast<std::size_t> (p)], on ? colour.brighter (0.7f) : colour, marked || p == selected || on);
        wasPlaying[static_cast<std::size_t> (p)] = on;
    }
}

void ChopPanel::timerCallback()
{
    bool changed = false;
    for (int p = 0; p < 16; ++p)
        changed |= sampler.isPlaying (slotOf (p)) != wasPlaying[static_cast<std::size_t> (p)];
    if (changed)
        refreshPads();
    sampler.collectGarbage();
    if (isShowing())
        view->tick();
    if (juce::Time::getMillisecondCounter() - midiSeenAt < 400)
        repaint (midiArea);
    if (message.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime > 3000)
    {
        message.clear();
        repaint (messageArea);
        repaint (readoutArea);
    }
}

//--- Keys and pads ---------------------------------------------------------------------------------------------

bool ChopPanel::handleKey (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() && key.getKeyCode() == 'Z')
    {
        undo();
        return true;
    }
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress (juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0))
    {
        nudge (-1, mods.isShiftDown());
        return true;
    }
    if (key == juce::KeyPress::rightKey || key == juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0))
    {
        nudge (1, mods.isShiftDown());
        return true;
    }
    if (key == juce::KeyPress::spaceKey)
    {
        // A sound file being chopped plays with Space; otherwise Space is the deck's (the caller handles it).
        const auto& b = current();
        if (b.audio != nullptr && ! sourceIsOnDeck (b))
        {
            toggleFilePreview();
            return true;
        }
        return false;
    }
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    for (int p = 0; p < 16; ++p)
        if (c == static_cast<juce::juce_wchar> (padKeys[p]))
        {
            // Holding a key repeats it; a pad fires once per press.
            if (! held[static_cast<std::size_t> (p)])
            {
                held[static_cast<std::size_t> (p)] = true;
                padDown (p);
            }
            return true;
        }
    return false;
}

void ChopPanel::keysChanged()
{
    for (int p = 0; p < 16; ++p)
        if (held[static_cast<std::size_t> (p)] && ! juce::KeyPress::isKeyCurrentlyDown (padKeys[p])
            && ! juce::KeyPress::isKeyCurrentlyDown (static_cast<int> (juce::CharacterFunctions::toUpperCase (static_cast<juce::juce_wchar> (padKeys[p])))))
        {
            held[static_cast<std::size_t> (p)] = false;
            padUp (p);
        }
}

void ChopPanel::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent == &midiKey && e.mods.isPopupMenu())
        return showMidiMenu();
    for (int p = 0; p < 16; ++p)
        if (e.eventComponent == &pads[static_cast<std::size_t> (p)])
        {
            if (e.mods.isPopupMenu())
                showPadMenu (p);
            else
            {
                mouseHeldPad = p;
                padDown (p);
            }
            return;
        }
}

void ChopPanel::mouseUp (const juce::MouseEvent&)
{
    if (mouseHeldPad >= 0)
        padUp (std::exchange (mouseHeldPad, -1));
}

void ChopPanel::padDown (int pad, float velocity)
{
    if (learning)
    {
        // Choosing which pad the next controller pad is learned for.
        learnTarget = pad;
        selectPad (pad);
        say ("LEARN: HIT A CONTROLLER PAD FOR PAD " + juce::String (pad + 1) + " [" + keyName (pad) + "]");
        return;
    }
    const auto& b = current();
    const bool marked = b.marks[static_cast<std::size_t> (pad)].has_value() && b.audio != nullptr;
    selected = pad;
    showPadSettings();
    if (! marked || chopMode)
        markAtPlayhead (pad);
    else
    {
        sampler.trigger (slotOf (pad), velocity);
        say (describe (pad));
    }
    refreshPads();
    repaint();
}

void ChopPanel::padUp (int pad)
{
    if (hold)
        sampler.release (slotOf (pad));
}

void ChopPanel::showPadMenu (int pad)
{
    selectPad (pad);
    const bool marked = current().marks[static_cast<std::size_t> (pad)].has_value();
    juce::PopupMenu menu;
    menu.addSectionHeader ("PAD " + juce::String (pad + 1) + "  [" + keyName (pad) + "]");
    menu.addItem (marked ? "Move Mark to the Playhead" : "Mark at the Playhead", [this, pad] { markAtPlayhead (pad); });
    menu.addItem ("Play", marked, false, [this, pad] { sampler.trigger (slotOf (pad)); });
    menu.addItem ("Zoom to Slice", marked, false, [this, pad] {
        view->showRange (static_cast<double> (*current().marks[static_cast<std::size_t> (pad)]), static_cast<double> (sliceEnd (bank, pad).value_or (0)));
    });
    menu.addItem ("Reset Pad Settings", current().settings[static_cast<std::size_t> (pad)] != engine::Sampler::PadSettings {}, false, [this, pad] {
        rememberForUndo();
        current().settings[static_cast<std::size_t> (pad)] = {};
        pushSettings (bank, pad);
        showPadSettings();
        saveKit();
    });
    menu.addSeparator();
    menu.addItem ("Clear Pad", marked, false, [this, pad] { clearPad (pad); });
    menu.addItem ("Clear Bank " + slotName (slotOf (pad)).substring (0, 1), ! current().empty(), false, [this] { clearBank(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

//--- Chopping --------------------------------------------------------------------------------------------------

bool ChopPanel::startBankFromDeck()
{
    // An empty bank takes the track on the deck.
    auto& b = current();
    const auto loaded = std::as_const (session).audio();
    const auto* record = session.record();
    if (loaded == nullptr || record == nullptr || loaded->channels.empty())
    {
        say ("LOAD A TRACK FIRST (DOUBLE-CLICK ONE IN CATALOG)");
        return false;
    }
    const juce::File track (juce::String::fromUTF8 (record->identity.path.c_str()));
    if (b.audio != nullptr && ! b.empty() && b.source != track)
    {
        say ("BANK " + slotName (slotOf (0)).substring (0, 1) + " CHOPS " + b.source.getFileNameWithoutExtension().toUpperCase().substring (0, 24)
             + ": USE AN EMPTY BANK, OR CLEAR BANK");
        return false;
    }
    if (b.audio == nullptr || b.source != track)
    {
        // Slices of one track in any bank share one copy of it.
        std::shared_ptr<const io::DecodedAudio> audio = loaded;
        for (const auto& other : bankData)
            if (other.audio != nullptr && other.source == track)
                audio = other.audio;
        const auto keepSettings = b.settings;
        b = {};
        b.settings = keepSettings;
        b.source = track;
        b.audio = audio;
    }
    const auto rate = b.audio->info.sampleRate;
    b.framesPerBeat = 60.0 / record->info.bpm.value_or (120.0) * rate;
    if (record->grid && ! record->grid->isEmpty())
    {
        const auto beat = record->grid->beatAt (session.deck().position());
        b.framesPerBeat = record->grid->frameAt (beat + 1) - record->grid->frameAt (beat);
    }
    return true;
}

void ChopPanel::markAtPlayhead (int pad)
{
    auto& b = current();
    if (b.audio != nullptr && ! sourceIsOnDeck (b))
    {
        // A sound file: mark where Space is playing it (or its cursor).
        auto& preview = session.output().chopPreview();
        const auto len = static_cast<double> (b.audio->channels.front().size());
        const auto at = preview.isPlaying() ? static_cast<Frame> (preview.progress() * len) : previewCursor;
        setMark (pad, snapped (at));
        return;
    }
    if (! startBankFromDeck())
        return;
    // Tapping along: the playhead as heard, snapped like a placed marker.
    setMark (pad, snapped (static_cast<Frame> (session.deck().position())));
}

void ChopPanel::setMark (int pad, Frame at, bool remember)
{
    auto& b = current();
    if (b.audio == nullptr)
        return;
    if (remember)
        rememberForUndo();
    const auto total = static_cast<Frame> (b.audio->channels.front().size());
    b.marks[static_cast<std::size_t> (pad)] = juce::jlimit<Frame> (0, juce::jmax<Frame> (0, total - 1), at);
    selected = pad;
    for (int p = 0; p < 16; ++p)
        if (b.marks[static_cast<std::size_t> (p)] && sampler.sample (slotOf (p)) != b.audio)
            sampler.setSample (slotOf (p), b.audio);
    pushSettings (bank, pad);
    updateRegions (bank);
    saveKit();
    say (describe (pad));
    showPadSettings();
    refreshPads();
    repaint();
}

void ChopPanel::nudge (int direction, bool bigStep)
{
    auto& b = current();
    auto& m = b.marks[static_cast<std::size_t> (selected)];
    if (! m || b.audio == nullptr)
        return;
    rememberForUndo();
    const auto rate = b.audio->info.sampleRate;
    auto at = *m + static_cast<Frame> (direction * rate * (bigStep ? 0.010 : 0.001));
    if (snap == Snap::zero)
    {
        // To the next zero crossing that way.
        auto z = engine::nearestZeroCrossing (*b.audio, at, static_cast<std::int64_t> (rate * 0.005));
        if ((direction > 0 && z <= *m) || (direction < 0 && z >= *m))
            z = at;
        at = z;
    }
    const auto total = static_cast<Frame> (b.audio->channels.front().size());
    m = juce::jlimit<Frame> (0, total - 1, at);
    updateRegions (bank);
    saveKit();
    say (describe (selected));
}

std::optional<Frame> ChopPanel::sliceEnd (int bankIndex, int pad) const
{
    const auto& b = bankData[static_cast<std::size_t> (bankIndex)];
    const auto& m = b.marks[static_cast<std::size_t> (pad)];
    if (! m || b.audio == nullptr || b.audio->channels.empty())
        return std::nullopt;
    const auto total = static_cast<Frame> (b.audio->channels.front().size());
    std::optional<Frame> next;
    for (const auto& other : b.marks)
        if (other && *other > *m)
            next = juce::jmin (next.value_or (*other), *other);
    if (next)
        return *next;
    if (b.chopEnd && *b.chopEnd > *m)
        return juce::jmin (total, *b.chopEnd);
    // MM with no END yet: a beat, until you drag the END marker.
    const auto perBeat = b.framesPerBeat > 0 ? b.framesPerBeat : b.audio->info.sampleRate * 0.5;
    return juce::jmin (total, *m + static_cast<Frame> (sliceBeats().value_or (1.0) * perBeat));
}

std::optional<double> ChopPanel::sliceBeats() const
{
    if (manualLength())
        return std::nullopt;
    return lengths[juce::jlimit (1, 7, static_cast<int> (sliceKnob.getValue()))];
}

void ChopPanel::setEndMarker (Frame at)
{
    auto& b = current();
    if (b.audio == nullptr)
        return;
    Frame last = 0;
    for (const auto& m : b.marks)
        if (m)
            last = juce::jmax (last, *m);
    b.chopEnd = juce::jlimit<Frame> (last + 1, static_cast<Frame> (b.audio->channels.front().size()), at);
    updateRegions (bank);
}

void ChopPanel::updateRegions (int bankIndex)
{
    const auto& b = bankData[static_cast<std::size_t> (bankIndex)];
    for (int p = 0; p < 16; ++p)
        if (const auto& m = b.marks[static_cast<std::size_t> (p)]; m && b.audio != nullptr)
            sampler.setRegion (bankIndex * 16 + p, *m, sliceEnd (bankIndex, p).value_or (*m));
    if (bankIndex == bank)
    {
        view->repaint();
        repaint (readoutArea);
    }
}

void ChopPanel::pushSettings (int bankIndex, int pad)
{
    sampler.setPadSettings (bankIndex * 16 + pad, bankData[static_cast<std::size_t> (bankIndex)].settings[static_cast<std::size_t> (pad)]);
}

void ChopPanel::clearPad (int pad)
{
    rememberForUndo();
    current().marks[static_cast<std::size_t> (pad)].reset();
    sampler.setSample (slotOf (pad), nullptr);
    updateRegions (bank);
    saveKit();
    say ("CLEARED PAD " + juce::String (pad + 1) + ": NEXT PRESS MARKS IT");
    refreshPads();
    repaint();
}

void ChopPanel::clearBank()
{
    rememberForUndo();
    sampler.stopAll();
    session.output().chopPreview().stop();
    current() = {};
    for (int p = 0; p < 16; ++p)
    {
        sampler.setSample (slotOf (p), nullptr);
        pushSettings (bank, p);
    }
    saveKit();
    say ("CLEARED BANK " + slotName (slotOf (0)).substring (0, 1) + ": READY TO CHOP");
    view->reset();
    showPadSettings();
    refreshPads();
    repaint();
}

void ChopPanel::applyChop (Frame from, Frame to, const std::vector<Frame>& marks, const juce::String& what)
{
    rememberForUndo();
    auto& b = current();
    b.marks = {};
    for (std::size_t p = 0; p < std::min<std::size_t> (16, marks.size()); ++p)
        b.marks[p] = marks[p];
    b.chopEnd = to;
    for (int p = 0; p < 16; ++p)
    {
        sampler.setSample (slotOf (p), b.marks[static_cast<std::size_t> (p)] ? b.audio : nullptr);
        pushSettings (bank, p);
    }
    updateRegions (bank);
    saveKit();
    selected = 0;
    say (what);
    view->showRange (static_cast<double> (from), static_cast<double> (to));
    showPadSettings();
    refreshPads();
    repaint();
}

void ChopPanel::chopEqual()
{
    const auto chop = [this] {
        auto& b = current();
        Frame from = 0, to = 0;
        if (b.audio != nullptr && ! sourceIsOnDeck (b))
        {
            to = static_cast<Frame> (b.audio->channels.front().size());  // a sound file: all of it (MM: what's on screen)
            if (manualLength())
            {
                const auto [v0, v1] = view->visibleRange();
                from = juce::jlimit<Frame> (0, to - 1, static_cast<Frame> (v0));
                to = juce::jlimit<Frame> (from + 1, to, static_cast<Frame> (v1));
            }
        }
        else
        {
            if (! startBankFromDeck())
                return;
            auto& deck = session.deck();
            const auto total = static_cast<Frame> (current().audio->channels.front().size());
            if (deck.isLooping())
            {
                from = static_cast<Frame> (deck.loopStartFrame());
                to = static_cast<Frame> (deck.loopEndFrame());
            }
            else if (manualLength())
            {
                // MM: what's on screen.
                const auto [v0, v1] = view->visibleRange();
                from = static_cast<Frame> (juce::jmax (0.0, v0));
                to = static_cast<Frame> (v1);
            }
            else
            {
                const auto* record = session.record();
                from = static_cast<Frame> (deck.position());
                if (record != nullptr && record->grid && ! record->grid->isEmpty())
                    from = static_cast<Frame> (record->grid->frameAt (std::round (record->grid->beatAt (static_cast<double> (from)))));
                to = from + static_cast<Frame> (16 * sliceBeats().value_or (1.0) * current().framesPerBeat);
            }
            from = juce::jlimit<Frame> (0, total - 1, from);
            to = juce::jlimit<Frame> (from + 1, total, to);
        }
        std::vector<Frame> marks;
        for (int p = 0; p < 16; ++p)
            marks.push_back (from + (to - from) * p / 16);
        applyChop (from, to, marks, "16 EQUAL SLICES: " + juce::String (static_cast<double> (to - from) / current().audio->info.sampleRate / 16.0, 3) + " S EACH");
    };
    if (current().empty())
        return chop();
    const auto options = juce::MessageBoxOptions()
                             .withIconType (juce::MessageBoxIconType::QuestionIcon)
                             .withTitle ("Chop bank " + slotName (slotOf (0)).substring (0, 1) + " into 16 equal slices?")
                             .withMessage ("Its marks are replaced (UNDO brings them back).")
                             .withButton ("Chop", 1)
                             .withButton ("Cancel", 0)
                             .withAssociatedComponent (this);
    messageBox = juce::AlertWindow::showScopedAsync (options, [weak = juce::WeakReference<ChopPanel> (this), chop] (int result) {
        if (result == 1 && weak != nullptr)
            chop();
    });
}

void ChopPanel::chopTransients()
{
    auto& b = current();
    // The deck's track (an empty bank takes it), or the sound file this bank chops.
    if ((b.audio == nullptr || sourceIsOnDeck (b)) && ! startBankFromDeck())
        return;
    auto& cb = current();
    const auto total = static_cast<Frame> (cb.audio->channels.front().size());
    Frame from = 0, to = total;
    auto& deck = session.deck();
    if (sourceIsOnDeck (cb) && deck.isLooping())
    {
        from = static_cast<Frame> (deck.loopStartFrame());
        to = static_cast<Frame> (deck.loopEndFrame());
    }
    else
    {
        // What's on screen.
        const auto [v0, v1] = view->visibleRange();
        from = juce::jlimit<Frame> (0, total, static_cast<Frame> (v0));
        to = juce::jlimit<Frame> (from, total, static_cast<Frame> (v1));
    }
    const auto run = [this, from, to] {
        auto& bk = current();
        auto found = engine::findTransients (*bk.audio, from, to, 16);
        if (found.empty())
            return say ("NO CLEAR HITS THERE: TRY EQUAL 16, OR TAP THE PADS");
        applyChop (found.front(), to, found, juce::String (static_cast<int> (found.size())) + " SLICES, ONE AT EACH HIT");
    };
    if (cb.empty())
        return run();
    const auto options = juce::MessageBoxOptions()
                             .withIconType (juce::MessageBoxIconType::QuestionIcon)
                             .withTitle ("Chop bank " + slotName (slotOf (0)).substring (0, 1) + " at its transients?")
                             .withMessage ("Its marks are replaced (UNDO brings them back).")
                             .withButton ("Chop", 1)
                             .withButton ("Cancel", 0)
                             .withAssociatedComponent (this);
    messageBox = juce::AlertWindow::showScopedAsync (options, [weak = juce::WeakReference<ChopPanel> (this), run] (int result) {
        if (result == 1 && weak != nullptr)
            run();
    });
}

void ChopPanel::rememberForUndo()
{
    undoStack.emplace_back (bank, current());
    if (undoStack.size() > 64)
        undoStack.erase (undoStack.begin());
}

void ChopPanel::undo()
{
    if (undoStack.empty())
        return say ("NOTHING TO UNDO");
    auto [b, data] = std::move (undoStack.back());
    undoStack.pop_back();
    if (b != bank)
        selectBank (b);
    current() = std::move (data);
    for (int p = 0; p < 16; ++p)
    {
        sampler.setSample (slotOf (p), current().marks[static_cast<std::size_t> (p)] ? current().audio : nullptr);
        pushSettings (bank, p);
    }
    updateRegions (bank);
    saveKit();
    say ("UNDONE");
    showPadSettings();
    refreshPads();
    repaint();
}

void ChopPanel::toggleFilePreview()
{
    auto& preview = session.output().chopPreview();
    const auto& b = current();
    if (b.audio == nullptr)
        return;
    if (preview.isPlaying())
    {
        // Pause where it is: Space again carries on from there.
        previewCursor = static_cast<Frame> (preview.progress() * static_cast<double> (b.audio->channels.front().size()));
        preview.stop();
        return;
    }
    preview.setTake (b.audio);
    if (session.deck().isPlaying())
        session.deck().pause();
    preview.play (static_cast<double> (previewCursor) / juce::jmax (1.0, static_cast<double> (b.audio->channels.front().size())));
}

void ChopPanel::loadFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Chop a sound file in bank " + slotName (slotOf (0)).substring (0, 1), samplesFolder(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        if (const auto file = fc.getResult(); file.existsAsFile())
            chopFile (file);
    });
}

void ChopPanel::chopFile (const juce::File& file)
{
    const auto target = bank;
    say ("LOADING " + file.getFileName().toUpperCase() + "...");
    std::thread ([weak = juce::WeakReference<ChopPanel> (this), target, file] {
        std::shared_ptr<const io::DecodedAudio> audio;
        juce::String problem;
        try
        {
            audio = std::make_shared<io::DecodedAudio> (io::decode (file.getFullPathName().toStdString()));
        }
        catch (const std::exception& e)
        {
            problem = e.what();
        }
        juce::MessageManager::callAsync ([weak, target, file, audio, problem] {
            auto* self = weak.get();
            if (self == nullptr)
                return;
            if (audio == nullptr || audio->channels.empty() || audio->channels.front().empty())
                return self->say ("COULDN'T OPEN IT: " + problem.toUpperCase());
            self->undoStack.emplace_back (target, self->bankData[static_cast<std::size_t> (target)]);
            self->sampler.stopAll();
            self->session.output().chopPreview().stop();
            auto& b = self->bankData[static_cast<std::size_t> (target)];
            b = {};
            b.source = file;
            b.audio = audio;
            b.framesPerBeat = audio->info.sampleRate * 0.5;
            for (int p = 0; p < 16; ++p)
            {
                self->sampler.setSample (target * 16 + p, nullptr);
                self->pushSettings (target, p);
            }
            self->previewCursor = 0;
            self->saveKit();
            self->selectBank (target);
            self->say ("CHOPPING " + file.getFileNameWithoutExtension().toUpperCase() + ": SPACE PLAYS IT. TAP PADS, EQUAL 16 OR TRANSIENTS");
        });
    }).detach();
}

io::DecodedAudio ChopPanel::renderSlice (const ChopBank& b, int bankIndex, int pad) const
{
    // The slice as the pad plays it: tune (resampled), reverse, gain, attack and fade.
    io::DecodedAudio out;
    const auto& source = *b.audio;
    out.info = source.info;
    const auto& st = b.settings[static_cast<std::size_t> (pad)];
    const auto from = static_cast<double> (*b.marks[static_cast<std::size_t> (pad)]);
    const auto to = static_cast<double> (juce::jmax (*b.marks[static_cast<std::size_t> (pad)] + 1, sliceEnd (bankIndex, pad).value_or (0)));
    const auto ratio = std::pow (2.0, static_cast<double> (st.semitones) / 12.0);
    const auto frames = static_cast<std::size_t> ((to - from) / ratio);
    const auto gain = std::pow (10.0f, st.gainDb / 20.0f);
    const auto rate = source.info.sampleRate;
    const auto attack = juce::jmax (rate * 0.002, rate * st.attackMs / 1000.0) / ratio;
    const auto fade = juce::jmax (rate * 0.002, rate * st.fadeMs / 1000.0) / ratio;
    const auto length = source.channels.front().size();
    for (const auto& ch : source.channels)
    {
        std::vector<float> dest (frames);
        for (std::size_t i = 0; i < frames; ++i)
        {
            const auto pos = st.reverse ? to - 1.001 - static_cast<double> (i) * ratio : from + static_cast<double> (i) * ratio;
            const auto p = static_cast<std::size_t> (juce::jmax (0.0, pos));
            if (p + 1 >= length)
                break;
            const auto frac = static_cast<float> (pos - static_cast<double> (p));
            const auto env = juce::jmin (1.0, static_cast<double> (i) / juce::jmax (1.0, attack)) * juce::jmin (1.0, static_cast<double> (frames - 1 - i) / juce::jmax (1.0, fade));
            dest[i] = (ch[p] + (ch[p + 1] - ch[p]) * frac) * gain * static_cast<float> (env);
        }
        out.channels.push_back (std::move (dest));
    }
    out.info.lengthFrames = static_cast<decltype (out.info.lengthFrames)> (frames);
    return out;
}

void ChopPanel::saveBank()
{
    const auto& b = current();
    const auto letter = slotName (slotOf (0)).substring (0, 1);
    const auto folder = samplesFolder().getChildFile ("Bank " + letter);
    int written = 0;
    juce::String problem;
    for (int p = 0; p < 16; ++p)
    {
        if (! b.marks[static_cast<std::size_t> (p)] || b.audio == nullptr)
            continue;
        const auto slice = renderSlice (b, bank, p);
        // A01 Title.wav: saving the bank again replaces what DSS1 saved for that pad before.
        const auto file = folder.getChildFile (slotName (slotOf (p)) + " " + safeName (b.source.getFileNameWithoutExtension()) + ".wav");
        folder.createDirectory();
        try
        {
            io::writeWav (file.getFullPathName().toStdString(), slice);
            ++written;
        }
        catch (const std::exception& e)
        {
            problem = e.what();
        }
    }
    if (problem.isNotEmpty())
        say ("COULDN'T SAVE: " + problem.toUpperCase());
    else if (written == 0)
        say ("NO SLICES IN BANK " + letter + " TO SAVE");
    else
        say ("SAVED " + juce::String (written) + " SLICE" + (written == 1 ? "" : "S") + " TO BANK " + letter);
}

//--- Keeping the kit -------------------------------------------------------------------------------------------

void ChopPanel::saveKit()
{
    // One line per bank: source <TAB> frames per beat <TAB> chop end (-1) <TAB> 16 marks, comma-separated (blank =
    // none) <TAB> 16 pad settings "gain:tune:reverse:attack:fade", comma-separated.
    juce::StringArray lines;
    for (const auto& b : bankData)
    {
        if (b.audio == nullptr)
        {
            lines.add ({});
            continue;
        }
        juce::StringArray marks, padTexts;
        for (const auto& m : b.marks)
            marks.add (m ? juce::String (*m) : juce::String());
        for (const auto& st : b.settings)
            padTexts.add (juce::String (st.gainDb) + ":" + juce::String (st.semitones) + ":" + (st.reverse ? "1" : "0") + ":" + juce::String (st.attackMs) + ":"
                      + juce::String (st.fadeMs));
        lines.add (b.source.getFullPathName() + "\t" + juce::String (b.framesPerBeat) + "\t" + juce::String (b.chopEnd.value_or (-1)) + "\t"
                   + marks.joinIntoString (",") + "\t" + padTexts.joinIntoString (","));
    }
    settings->setValue ("sliceKit", lines.joinIntoString ("\n"));
}

void ChopPanel::restoreKit()
{
    const auto lines = juce::StringArray::fromLines (settings->getValue ("sliceKit"));
    struct Entry
    {
        int bank;
        ChopBank data;
    };
    std::vector<Entry> entries;
    for (int i = 0; i < juce::jmin (8, lines.size()); ++i)
    {
        const auto parts = juce::StringArray::fromTokens (lines[i], "\t", {});
        if (parts.size() < 4 || ! juce::File::isAbsolutePath (parts[0]) || ! juce::File (parts[0]).existsAsFile())
            continue;
        ChopBank b;
        b.source = juce::File (parts[0]);
        b.framesPerBeat = parts[1].getDoubleValue();
        if (const auto e = parts[2].getLargeIntValue(); e >= 0)
            b.chopEnd = e;
        const auto marks = juce::StringArray::fromTokens (parts[3], ",", {});
        for (int p = 0; p < juce::jmin (16, marks.size()); ++p)
            if (marks[p].isNotEmpty())
                b.marks[static_cast<std::size_t> (p)] = marks[p].getLargeIntValue();
        if (parts.size() >= 5)  // pad settings (from test.23)
        {
            const auto padTexts = juce::StringArray::fromTokens (parts[4], ",", {});
            for (int p = 0; p < juce::jmin (16, padTexts.size()); ++p)
            {
                const auto v = juce::StringArray::fromTokens (padTexts[p], ":", {});
                if (v.size() < 5)
                    continue;
                auto& st = b.settings[static_cast<std::size_t> (p)];
                st.gainDb = v[0].getFloatValue();
                st.semitones = v[1].getFloatValue();
                st.reverse = v[2] == "1";
                st.attackMs = v[3].getFloatValue();
                st.fadeMs = v[4].getFloatValue();
            }
        }
        entries.push_back ({ i, std::move (b) });
    }
    if (entries.empty())
        return;
    // Decode each file once (banks chopping one track share it), off the message thread.
    std::thread ([weak = juce::WeakReference<ChopPanel> (this), entries]() mutable {
        std::map<juce::String, std::shared_ptr<const io::DecodedAudio>> decoded;
        for (auto& e : entries)
        {
            const auto path = e.data.source.getFullPathName();
            if (decoded.count (path) == 0)
            {
                try
                {
                    decoded[path] = std::make_shared<io::DecodedAudio> (io::decode (path.toStdString()));
                }
                catch (const std::exception&)
                {
                    decoded[path] = nullptr;
                }
            }
            e.data.audio = decoded[path];
        }
        juce::MessageManager::callAsync ([weak, entries] {
            auto* self = weak.get();
            if (self == nullptr)
                return;
            for (const auto& e : entries)
            {
                auto& b = self->bankData[static_cast<std::size_t> (e.bank)];
                if (e.data.audio == nullptr || e.data.audio->channels.empty() || b.audio != nullptr)
                    continue;  // unreadable, or already chopped since launch
                b = e.data;
                for (int p = 0; p < 16; ++p)
                {
                    if (b.marks[static_cast<std::size_t> (p)])
                        self->sampler.setSample (e.bank * 16 + p, b.audio);
                    self->pushSettings (e.bank, p);
                }
                self->updateRegions (e.bank);
            }
            self->view->reset();
            self->showPadSettings();
            self->refreshPads();
            self->repaint();
        });
    }).detach();
}

void ChopPanel::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the folder your samples are saved to", samplesFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
        const auto dir = fc.getResult();
        if (! dir.isDirectory())
            return;
        settings->setValue ("samplesFolder", dir.getFullPathName());
        say ("SAVE BANK WRITES TO " + dir.getFileName().toUpperCase());
        repaint();
    });
}

//--- MIDI ------------------------------------------------------------------------------------------------------

void ChopPanel::midiNoteOn (int note, float velocity)
{
    if (learning)
    {
        // This controller pad now plays DSS1 pad learnTarget; then on to the next.
        for (auto& n : padNotes)
            if (n == note)
                n = -1;
        if (note == playNote)
            playNote = -1;
        padNotes[static_cast<std::size_t> (learnTarget)] = note;
        saveMidiMap();
        const auto learned = learnTarget;
        learnTarget = (learnTarget + 1) % 16;
        selectPad (learnTarget);
        say ("NOTE " + juce::String (note) + " PLAYS PAD " + juce::String (learned + 1) + ". NEXT: PAD " + juce::String (learnTarget + 1));
        return;
    }
    if (note == playNote)
        return midiPlay (std::nullopt);
    for (int p = 0; p < 16; ++p)
        if (padNotes[static_cast<std::size_t> (p)] == note)
            return padDown (p, juce::jlimit (0.05f, 1.0f, velocity));
}

void ChopPanel::midiNoteOff (int note)
{
    for (int p = 0; p < 16; ++p)
        if (padNotes[static_cast<std::size_t> (p)] == note)
            return padUp (p);
}

void ChopPanel::midiControl (int controller, int value)
{
    if (learning)
    {
        if (value < 64)
            return;  // learn on the press
        playController = controller;
        playNote = -1;
        saveMidiMap();
        say ("CONTROL " + juce::String (controller) + " IS NOW PLAY / PAUSE");
        return;
    }
    if (controller == playController && value >= 64)
        midiPlay (std::nullopt);
}

void ChopPanel::midiPlay (std::optional<bool> play)
{
    // Like Space: a sound file being chopped (on this screen), otherwise the deck.
    const auto& b = current();
    if (isShowing() && b.audio != nullptr && ! sourceIsOnDeck (b))
    {
        if (! play || *play != session.output().chopPreview().isPlaying())
            toggleFilePreview();
        return;
    }
    if (! play || *play != session.deck().isPlaying())
        session.playPressed();
}

void ChopPanel::setLearning (bool on)
{
    learning = on;
    midiKey.setToggleState (on, juce::dontSendNotification);
    if (on)
    {
        learnTarget = selected;
        const auto devices = midi.deviceNames();
        say (devices.isEmpty() ? juce::String ("NO MIDI CONTROLLER FOUND: PLUG ONE IN")
                               : "LEARN: HIT A CONTROLLER PAD FOR PAD " + juce::String (learnTarget + 1) + ", OR ITS PLAY BUTTON");
    }
    else
        say ("MIDI LEARN DONE");
    repaint (midiArea);
}

void ChopPanel::showMidiMenu()
{
    juce::PopupMenu menu;
    const auto devices = midi.deviceNames();
    menu.addSectionHeader (devices.isEmpty() ? juce::String ("NO MIDI CONTROLLERS") : "MIDI: " + devices.joinIntoString (", ").substring (0, 60));
    const auto preset = [this] (int first, const juce::String& what) {
        for (int p = 0; p < 16; ++p)
            padNotes[static_cast<std::size_t> (p)] = first + p;
        saveMidiMap();
        say ("PADS: " + what);
    };
    menu.addItem ("Akai MPD / MPK Pads (Notes 36-51)", [preset] { preset (36, "AKAI MPD / MPK, NOTES 36-51"); });
    menu.addItem ("Akai MPC Pads (Notes 37-52)", [preset] { preset (37, "AKAI MPC, NOTES 37-52"); });
    menu.addSeparator();
    menu.addItem ("Forget the Learned Play Button", playController >= 0 || playNote >= 0, false, [this] {
        playController = playNote = -1;
        saveMidiMap();
        say ("PLAY: MIDI START / STOP AND MMC ONLY");
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (midiKey));
}

void ChopPanel::saveMidiMap()
{
    juce::StringArray notes;
    for (const auto n : padNotes)
        notes.add (juce::String (n));
    settings->setValue ("midiPadNotes", notes.joinIntoString (","));
    settings->setValue ("midiPlayCC", playController);
    settings->setValue ("midiPlayNote", playNote);
}

//--- Drawing ---------------------------------------------------------------------------------------------------

void ChopPanel::paint (juce::Graphics& g)
{
    const auto ear = static_cast<float> (hw::RackUnit::earWidth);
    for (const auto& [unit, slots] : { std::pair { waveUnit, 4 }, std::pair { controlUnit, 3 } })
    {
        const auto b = unit.toFloat();
        hw::faceplate (g, b.reduced (ear, 0));
        hw::rackEar (g, b.withWidth (ear), slots);
        hw::rackEar (g, b.withLeft (b.getRight() - ear), slots);
        g.setColour (juce::Colours::black);
        g.fillRect (b.withTop (b.getBottom() - 1.0f));
    }
    const auto label = [&] (const juce::Component& c, const juce::String& text, const juce::String& sub, float width = 300) {
        hw::silk (g, text, sub, c.getBounds().toFloat().withWidth (width).withHeight (16).translated (0, -20));
    };

    // Waveform unit: title and messages.
    const auto& b = current();
    const auto source = b.audio != nullptr ? b.source.getFileNameWithoutExtension().toUpperCase()
                                           : juce::String (session.record() != nullptr ? "THE TRACK ON THE DECK" : "NOTHING LOADED");
    hw::silk (g, "CHOP", "BANK " + slotName (slotOf (0)).substring (0, 1) + "  " + source.substring (0, 40),
              juce::Rectangle<float> (view->getX(), messageArea.getY() + 2.0f, 420, 16));
    hw::recess (g, messageArea.toFloat().withTrimmedLeft (430).reduced (0, 1));
    const bool fresh = message.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime < 3000;
    hw::glowText (g, fresh ? message : describe (selected), messageArea.toFloat().withTrimmedLeft (440).reduced (6, 0), hw::vfd (20.0f),
                  fresh ? hw::colour::amberMid : hw::colour::amber);
    hw::glass (g, messageArea.toFloat().withTrimmedLeft (430).reduced (0, 1));
    label (zoomOutKey, "ZOOM", {}, 120);
    label (snapKey, "SNAP", {}, 120);
    {
        // MIDI: a light that blinks with what arrives, and the controller's name.
        const auto devices = midi.deviceNames();
        const bool recent = juce::Time::getMillisecondCounter() - midiSeenAt < 120;
        auto r = midiArea.toFloat();
        hw::led (g, { r.getX() + 4.0f, r.getY() + 8.0f }, 3.5f, learning ? hw::colour::red : hw::colour::playGreen, recent || learning);
        hw::silk (g, "MIDI", devices.isEmpty() ? juce::String ("NONE") : devices[0].toUpperCase().substring (0, 18), r.withTrimmedLeft (12).withHeight (16));
    }

    // Control unit labels.
    label (bankKeys[0], "BANKS", "A-H");
    label (chopKey, "MODE", "TAP PADS TO CHOP");
    label (equalKey, "CHOP", "EQUAL / AT THE HITS");
    label (nudgeLeft, "NUDGE", "ARROWS", 120);
    label (loadKey, "SAMPLE", "FILE / SAVE / CLEAR");
    hw::silk (g, "SLICE", manualLength() ? juce::String ("MANUAL") : lengthNames[juce::jlimit (0, 7, static_cast<int> (sliceKnob.getValue()))] + juce::String (" BEAT"),
              sliceKnob.getBounds().toFloat().withHeight (14).translated (0, -16), juce::Justification::centred, 11.0f);

    // The chosen pad's settings.
    hw::silk (g, "PAD " + juce::String (selected + 1) + " [" + keyName (selected) + "]", "SETTINGS", padSettingsArea.toFloat().withHeight (16).translated (0, -20));
    const auto knobLabel = [&] (const juce::Slider& k, const juce::String& name, const juce::String& value) {
        hw::silk (g, name, {}, k.getBounds().toFloat().withHeight (14).translated (0, -16), juce::Justification::centred, 11.0f);
        hw::glowText (g, value, k.getBounds().toFloat().withY (static_cast<float> (k.getBottom()) - 2).withHeight (20), hw::vfd (19.0f), hw::colour::amber,
                      juce::Justification::centred);
    };
    const auto& st = b.settings[static_cast<std::size_t> (selected)];
    knobLabel (gainKnob, "GAIN", (st.gainDb > 0 ? "+" : "") + juce::String (st.gainDb, 1) + " DB");
    knobLabel (tuneKnob, "TUNE", (st.semitones > 0 ? "+" : "") + juce::String (juce::roundToInt (st.semitones)) + " ST");
    knobLabel (attackKnob, "ATTACK", juce::String (juce::roundToInt (st.attackMs)) + " MS");
    knobLabel (fadeKnob, "FADE", juce::String (juce::roundToInt (st.fadeMs)) + " MS");

    // Where SAVE BANK writes.
    hw::recess (g, folderArea.toFloat());
    auto path = samplesFolder().getFullPathName();
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    if (path.startsWith (home))
        path = "~" + path.substring (home.length());
    auto f = folderArea.toFloat().reduced (10, 0);
    hw::silk (g, "SAVE TO", {}, f.removeFromLeft (58), juce::Justification::centredLeft, 10.0f);
    hw::glowText (g, path, f, hw::vfd (19.0f));
    hw::glass (g, folderArea.toFloat());
}

void ChopPanel::paintOverChildren (juce::Graphics& g)
{
    // Each pad's key, in the corner, like the legend on a drum machine; the chosen pad outlined.
    const auto& b = current();
    for (int p = 0; p < 16; ++p)
    {
        const auto area = pads[static_cast<std::size_t> (p)].getBounds().toFloat().reduced (7, 5);
        const bool marked = b.marks[static_cast<std::size_t> (p)].has_value() && b.audio != nullptr;
        g.setFont (hw::brand (13.0f));
        g.setColour (marked ? juce::Colours::white.withAlpha (0.85f) : hw::colour::silkDim.withAlpha (0.8f));
        g.drawText (keyName (p), area, juce::Justification::topLeft, false);
        if (p == selected)
        {
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.drawRoundedRectangle (pads[static_cast<std::size_t> (p)].getBounds().toFloat().reduced (1.5f), 4.0f, 1.5f);
        }
        if (b.settings[static_cast<std::size_t> (p)] != engine::Sampler::PadSettings {})
            hw::led (g, { area.getRight() - 2.0f, area.getY() + 5.0f }, 3.0f, hw::colour::amber, true);
    }
}

void ChopPanel::resized()
{
    auto all = getLocalBounds();
    controlUnit = all.removeFromBottom (juce::jmin (340, all.getHeight() * 11 / 20));
    waveUnit = all;

    // --- Waveform unit ---
    auto area = waveUnit.reduced (hw::RackUnit::earWidth, 0).reduced (30, 0);
    area.removeFromTop (26);
    area.removeFromBottom (18);
    auto top = area.removeFromTop (36);
    wholeKey.setBounds (top.removeFromRight (80).reduced (0, 2));
    top.removeFromRight (6);
    sliceZoomKey.setBounds (top.removeFromRight (80).reduced (0, 2));
    top.removeFromRight (6);
    zoomInKey.setBounds (top.removeFromRight (44).reduced (0, 2));
    top.removeFromRight (6);
    zoomOutKey.setBounds (top.removeFromRight (44).reduced (0, 2));
    top.removeFromRight (20);
    snapKey.setBounds (top.removeFromRight (120).reduced (0, 2));
    top.removeFromRight (20);
    midiKey.setBounds (top.removeFromRight (120).reduced (0, 2));
    midiArea = midiKey.getBounds().withY (midiKey.getY() - 22).withHeight (18).withWidth (200).translated (-80, 0);
    top.removeFromRight (20);
    messageArea = top.withTrimmedTop (4).withTrimmedBottom (2);
    area.removeFromTop (12);
    view->setBounds (area);

    // --- Control unit: pads | modes and chopping | pad settings ---
    area = controlUnit.reduced (hw::RackUnit::earWidth, 0).reduced (30, 0);
    area.removeFromTop (34);
    area.removeFromBottom (18);

    auto padColumn = area.removeFromLeft (juce::jmin (330, area.getWidth() / 3));
    auto banks = padColumn.removeFromTop (30);
    const auto bw = (banks.getWidth() - 7 * 3) / 8;
    for (auto& k : bankKeys)
    {
        k.setBounds (banks.removeFromLeft (bw));
        banks.removeFromLeft (3);
    }
    padColumn.removeFromTop (8);
    const auto size = juce::jmin ((padColumn.getHeight() - 3 * 6) / 4, (padColumn.getWidth() - 3 * 6) / 4);
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            pads[static_cast<std::size_t> (padAt (row, col))].setBounds (padColumn.getX() + col * (size + 6), padColumn.getY() + row * (size + 6), size, size);
    area.removeFromLeft (30);

    auto settingsColumn = area.removeFromRight (juce::jmin (420, area.getWidth() * 2 / 5));
    area.removeFromRight (30);

    // Middle: mode keys; chop keys with nudge; slice knob with sample keys; where samples go.
    auto middle = area;
    auto modes = middle.removeFromTop (32);
    const auto mw = (modes.getWidth() - 3 * 6) / 4;
    for (auto* k : { &chopKey, &playKey, &holdKey, &chokeKey })
    {
        k->setBounds (modes.removeFromLeft (mw));
        modes.removeFromLeft (6);
    }
    middle.removeFromTop (30);
    auto chops = middle.removeFromTop (34);
    auto nudgeArea = chops.removeFromRight (100);
    nudgeLeft.setBounds (nudgeArea.removeFromLeft (47));
    nudgeRight.setBounds (nudgeArea.withTrimmedLeft (6));
    chops.removeFromRight (16);
    const auto cw = (chops.getWidth() - 2 * 6) / 3;
    for (auto* k : { &equalKey, &transientKey, &undoKey })
    {
        k->setBounds (chops.removeFromLeft (cw));
        chops.removeFromLeft (6);
    }
    middle.removeFromTop (30);
    auto folderRow = middle.removeFromBottom (28);
    folderKey.setBounds (folderRow.removeFromRight (96));
    folderRow.removeFromRight (6);
    folderArea = folderRow;
    middle.removeFromBottom (10);
    auto block = middle;
    {
        auto knobCell = block.removeFromLeft (96).withTrimmedTop (12);
        const auto ks = juce::jmin (knobCell.getWidth(), knobCell.getHeight() + 10, 96);
        sliceKnob.setBounds (knobCell.withSizeKeepingCentre (ks, ks));
    }
    block.removeFromLeft (8);
    const auto kw = (block.getWidth() - 2 * 6) / 3;
    auto r1 = block.removeFromTop (32);
    block.removeFromTop (6);
    auto r2 = block.removeFromTop (32);
    for (auto* k : { &loadKey, &saveKey, &stopKey })
    {
        k->setBounds (r1.removeFromLeft (kw));
        r1.removeFromLeft (6);
    }
    for (auto* k : { &clearKey, &clearBankKey, &showSavedKey })
    {
        k->setBounds (r2.removeFromLeft (kw));
        r2.removeFromLeft (6);
    }
    readoutArea = {};

    // Right: the chosen pad's knobs and REVERSE.
    padSettingsArea = settingsColumn;
    auto knobs = settingsColumn.withTrimmedTop (22);
    auto rev = knobs.removeFromBottom (34);
    reverseKey.setBounds (rev.withSizeKeepingCentre (juce::jmin (160, rev.getWidth()), 34));
    knobs.removeFromBottom (24);
    const auto kwid = knobs.getWidth() / 4;
    for (auto* k : { &gainKnob, &tuneKnob, &attackKnob, &fadeKnob })
        k->setBounds (knobs.removeFromLeft (kwid).withSizeKeepingCentre (juce::jmin (kwid - 10, 84), juce::jmin (kwid - 10, 84)));
}

} // namespace dss1::ui
