#include "PrepPanel.h"
#include "Theme.h"

#include "core/SeratoTags.h"
#include "core/TestExport.h"

#include <cmath>

namespace dss1::ui
{

namespace
{

    juce::String plural (std::size_t n, const char* one, const char* many)
    {
        return juce::String (n) + " " + (n == 1 ? one : many);
    }
}

PrepPanel::PrepPanel (Session& s) : session (s), waveform (s)
{
    juce::PropertiesFile::Options options;
    options.applicationName = "DSS1";
    options.filenameSuffix = ".settings";
    options.folderName = "DSS1";
    options.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (options);

    waveform.snap = [this] (Frame f) { return snapped (f); };
    waveform.addBackgroundActions = [this] (juce::PopupMenu& m, Frame at) { addBackgroundActions (m, at); };
    addAndMakeVisible (waveform);

    targetBox.addItem ("Prepare for Rekordbox", static_cast<int> (Target::rekordbox));
    targetBox.addItem ("Prepare for Serato", static_cast<int> (Target::serato));
    targetBox.setSelectedId (settings->getIntValue ("prepTarget", static_cast<int> (Target::rekordbox)), juce::dontSendNotification);
    targetBox.onChange = [this] {
        settings->setValue ("prepTarget", targetBox.getSelectedId());
        repaint();
    };
    addAndMakeVisible (targetBox);

    snapButton.setToggleState (settings->getBoolValue ("snapToGrid", false), juce::dontSendNotification);
    snapButton.setTooltip ("New and moved cues land on the nearest beat");
    snapButton.onClick = [this] { settings->setValue ("snapToGrid", snapButton.getToggleState()); };
    addAndMakeVisible (snapButton);

    zoomOut.onClick = [this] { waveform.zoomBy (2.0); };
    zoomIn.onClick = [this] { waveform.zoomBy (0.5); };
    wholeTrack.onClick = [this] { waveform.showWholeTrack(); };
    for (auto* b : { &zoomOut, &zoomIn, &wholeTrack })
        addAndMakeVisible (b);

    // Beat-jump size for the header's << and >> keys, as a knob.
    jumpKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    jumpKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    jumpKnob.setRange (0, 6, 1);
    hwScale (jumpKnob, "1|2|4|8|16|32|64");
    jumpKnob.setValue (std::log2 (settings->getIntValue ("jumpBeats", 16)), juce::dontSendNotification);
    jumpKnob.setTooltip ("Beat-jump size for << and >> ([ and ] keys)");
    jumpKnob.onValueChange = [this] {
        session.jumpSize = 1 << static_cast<int> (jumpKnob.getValue());
        settings->setValue ("jumpBeats", session.jumpSize);
        repaint();
    };
    session.jumpSize = 1 << static_cast<int> (jumpKnob.getValue());
    addAndMakeVisible (jumpKnob);
    session.snap = [this] (Frame f) { return snapped (f); };

    for (std::size_t i = 0; i < hotButtons.size(); ++i)
    {
        auto& b = hotButtons[i];
        b = std::make_unique<juce::TextButton> (juce::String::charToString (TrackRecord::hotCueLetter (i)));
        b->setTooltip ("Click: set hot cue (or jump to it).  Shift-click: delete.  Keys A-H do the same.");
        b->onClick = [this, i] { hotCuePressed (i, juce::ModifierKeys::currentModifiers.isShiftDown()); };
        addAndMakeVisible (*b);
    }
    for (std::size_t i = 0; i < loopButtons.size(); ++i)
    {
        auto& b = loopButtons[i];
        b = std::make_unique<juce::TextButton> (juce::String (i + 1));
        b->setTooltip ("Saved loop: click to play it, Shift-click to delete");
        b->onClick = [this, i] {
            if (juce::ModifierKeys::currentModifiers.isShiftDown())
                deleteLoop (i);
            else
                playSavedLoop (i);
        };
        addAndMakeVisible (*b);
    }

    addMemoryButton.setTooltip ("Add a memory cue at the playhead (key M)");
    addMemoryButton.onClick = [this] { addMemoryCue (snapped (playhead())); };
    prevMemoryButton.setTooltip ("Previous memory cue");
    prevMemoryButton.onClick = [this] { jumpMemory (-1); };
    nextMemoryButton.setTooltip ("Next memory cue");
    nextMemoryButton.onClick = [this] { jumpMemory (1); };

    loopKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    loopKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    loopKnob.setRange (0, 5, 1);
    hwScale (loopKnob, "1|2|4|8|16|32");
    loopKnob.setValue (std::log2 (settings->getIntValue ("loopBeats", 8)), juce::dontSendNotification);
    loopKnob.setTooltip ("Length of Save Loop (key L)");
    loopKnob.onValueChange = [this] {
        settings->setValue ("loopBeats", loopLength());
        repaint();
    };
    saveLoopButton.setTooltip ("Save a loop of this many beats from the playhead into the next free slot (key L)");
    saveLoopButton.onClick = [this] { saveLoop (snapped (playhead()), loopLength()); };
    loopInButton.onClick = [this] { loopIn(); };
    loopOutButton.onClick = [this] { loopOut(); };
    exitLoopButton.onClick = [this] { session.deck().clearLoop(); };
    deleteLoopButton.setTooltip ("Delete the saved loop that's playing, so you can set a better one");
    deleteLoopButton.onClick = [this] {
        if (const auto slot = activeLoopSlot())
            deleteLoop (*slot);
    };

    for (auto* b : { &addMemoryButton, &prevMemoryButton, &nextMemoryButton, &saveLoopButton, &loopInButton, &loopOutButton, &exitLoopButton, &deleteLoopButton })
        addAndMakeVisible (b);
    addAndMakeVisible (loopKnob);

    bpmLabel.setText ("BPM", juce::dontSendNotification);
    bpmLabel.setColour (juce::Label::textColourId, Colours::dimText);
    addAndMakeVisible (bpmLabel);
    bpmEditor.setInputRestrictions (7, "0123456789.");
    bpmEditor.setJustification (juce::Justification::centred);
    bpmEditor.setTooltip ("Tempo at the playhead. Type a new one and press Return.");
    bpmEditor.onReturnKey = [this] {
        setBpm (bpmEditor.getText().getDoubleValue());
        grabKeyboardFocus();
    };
    bpmEditor.onEscapeKey = [this] {
        refresh();
        grabKeyboardFocus();
    };
    addAndMakeVisible (bpmEditor);

    downbeatButton.setTooltip ("Make the playhead beat 1 of a bar. Right-click the waveform for Beat 1 Here at any spot.");
    downbeatButton.onClick = [this] { setDownbeat (playhead()); };
    nudgeEarlier.setTooltip ("Move the grid 2 ms earlier (Shift: 0.5 ms)");
    nudgeEarlier.onClick = [this] { nudgeGrid (juce::ModifierKeys::currentModifiers.isShiftDown() ? -0.5 : -2.0); };
    nudgeLater.setTooltip ("Move the grid 2 ms later (Shift: 0.5 ms)");
    nudgeLater.onClick = [this] { nudgeGrid (juce::ModifierKeys::currentModifiers.isShiftDown() ? 0.5 : 2.0); };
    halveButton.setTooltip ("Half the tempo");
    halveButton.onClick = [this] { scaleTempo (0.5); };
    doubleButton.setTooltip ("Double the tempo");
    doubleButton.onClick = [this] { scaleTempo (2.0); };
    tempoChangeButton.setTooltip ("Start a new tempo at the playhead, for tracks that speed up or slow down");
    tempoChangeButton.onClick = [this] { addTempoChange (snapped (playhead())); };
    clearGridButton.onClick = [this] { clearGrid(); };
    for (auto* b : { &downbeatButton, &nudgeEarlier, &nudgeLater, &halveButton, &doubleButton, &tempoChangeButton, &clearGridButton })
        addAndMakeVisible (b);

    // Hardware styling and key caps.
    addMemoryButton.setButtonText ("+ MEMORY");
    prevMemoryButton.setButtonText ("< MEM");
    nextMemoryButton.setButtonText ("MEM >");
    saveLoopButton.setButtonText ("SAVE LOOP");
    loopInButton.setButtonText ("LOOP IN");
    loopOutButton.setButtonText ("LOOP OUT");
    exitLoopButton.setButtonText ("EXIT LOOP");
    downbeatButton.setButtonText ("BEAT 1");
    nudgeEarlier.setButtonText ("< NUDGE");
    nudgeLater.setButtonText ("NUDGE >");
    tempoChangeButton.setButtonText ("TEMPO CHANGE");
    clearGridButton.setButtonText ("CLEAR");
    halveButton.setButtonText ("/2");
    doubleButton.setButtonText ("X2");
    wholeTrack.setButtonText ("WHOLE");
    for (auto* b : { &addMemoryButton, &prevMemoryButton, &nextMemoryButton, &saveLoopButton, &loopInButton, &loopOutButton,
                     &exitLoopButton, &deleteLoopButton })
        hwStyle (*b, KeyStyle::cream);
    snapButton.setButtonText ("SNAP");
    bpmEditor.setFont (hw::vfd (34.0f));
    bpmEditor.setIndents (12, 6);
    bpmLabel.setVisible (false);

    session.addChangeListener (this);
    setWantsKeyboardFocus (true);
    refresh();
    startTimerHz (20);
}

PrepPanel::~PrepPanel()
{
    session.removeChangeListener (this);
    stopTimer();
}

void PrepPanel::visibilityChanged()
{
    if (isShowing())
        grabKeyboardFocus();
}

void PrepPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (session.state() == Session::State::loading)
    {
        pendingLoopIn.reset();
    }
    refresh();
}

void PrepPanel::timerCallback()
{
    // The tempo shown is the one at the playhead (it changes on a variable grid), and the loop buttons follow
    // the deck.
    if (message.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime > 6000)
        message.clear();
    if (! bpmEditor.hasKeyboardFocus (true))
        refresh();
}

void PrepPanel::refresh()
{
    const auto* r = session.record();
    const bool ready = r != nullptr && session.state() == Session::State::loaded;

    for (std::size_t i = 0; i < hotButtons.size(); ++i)
    {
        auto& b = *hotButtons[i];
        const bool set = ready && r->hotCues[i].has_value();
        b.setEnabled (ready);
        hwPad (b, set ? hotCueColour (*r, i) : juce::Colours::grey, set);
    }
    for (std::size_t i = 0; i < loopButtons.size(); ++i)
    {
        auto& b = *loopButtons[i];
        const bool set = ready && r->savedLoops[i].has_value();
        b.setEnabled (set);
        hwPad (b, hw::colour::loopBlue, set);
    }
    for (auto* b : { &addMemoryButton, &prevMemoryButton, &nextMemoryButton, &saveLoopButton, &loopInButton, &loopOutButton,
                     &downbeatButton, &tempoChangeButton })
        b->setEnabled (ready);
    const bool hasGrid = ready && r->grid && ! r->grid->isEmpty();
    for (auto* b : { &nudgeEarlier, &nudgeLater, &halveButton, &doubleButton, &clearGridButton })
        b->setEnabled (hasGrid);
    exitLoopButton.setEnabled (session.deck().isLooping());
    const auto active = ready ? activeLoopSlot() : std::nullopt;
    deleteLoopButton.setEnabled (active.has_value());
    deleteLoopButton.setButtonText (active ? "DELETE LOOP " + juce::String (*active + 1) : juce::String ("DELETE LOOP"));
    loopOutButton.setEnabled (ready && pendingLoopIn.has_value());
    loopInButton.setToggleState (pendingLoopIn.has_value(), juce::dontSendNotification);
    bpmEditor.setEnabled (ready);

    if (! bpmEditor.hasKeyboardFocus (true))
    {
        if (hasGrid)
            bpmEditor.setText (juce::String (r->grid->bpmAt (static_cast<double> (playhead())), 2), false);
        else if (ready && r->info.bpm)
            bpmEditor.setText (juce::String (*r->info.bpm, 2), false);
        else
            bpmEditor.setText ({}, false);
    }
    repaint (messageArea);
}

int PrepPanel::loopLength() const
{
    return 1 << static_cast<int> (loopKnob.getValue());
}

Frame PrepPanel::playhead() const
{
    return static_cast<Frame> (std::llround (session.deck().position()));
}

Frame PrepPanel::snapped (Frame f) const
{
    const auto* r = session.record();
    if (! snapButton.getToggleState() || r == nullptr || ! r->grid || r->grid->isEmpty())
        return f;
    return static_cast<Frame> (std::llround (r->grid->nearestBeat (static_cast<double> (f)).frame));
}

bool PrepPanel::edit (const std::function<void (TrackRecord&)>& change)
{
    if (session.record() == nullptr || session.state() != Session::State::loaded)
        return false;
    auto r = *session.record();
    change (r);
    session.update (r);
    return true;
}

void PrepPanel::say (const juce::String& m)
{
    message = m;
    messageTime = juce::Time::getMillisecondCounter();
    repaint (messageArea);
}

// --- Cues and loops -----------------------------------------------------------------------------------------

void PrepPanel::hotCuePressed (std::size_t slot, bool remove)
{
    const auto* r = session.record();
    if (r == nullptr || session.state() != Session::State::loaded)
        return;
    const auto letter = juce::String::charToString (TrackRecord::hotCueLetter (slot));
    if (remove)
    {
        if (r->hotCues[slot])
        {
            edit ([slot] (TrackRecord& rec) { rec.hotCues[slot].reset(); });
            say ("Deleted hot cue " + letter + ".");
        }
        return;
    }
    if (r->hotCues[slot])
    {
        session.deck().seek (r->hotCues[slot]->frame);
        session.deck().play();
        return;
    }
    const auto at = snapped (playhead());
    edit ([slot, at] (TrackRecord& rec) { rec.hotCues[slot] = HotCue { at, std::nullopt, {}, std::nullopt }; });
    say ("Hot cue " + letter + " at " + juce::String (formatTime (static_cast<double> (at) / r->sampleRate)) + ".");
}

void PrepPanel::addMemoryCue (Frame at)
{
    if (edit ([at] (TrackRecord& r) { r.addMemoryCue (MemoryCue { at, {}, std::nullopt }); }))
        say ("Memory cue at " + juce::String (formatTime (static_cast<double> (at) / session.record()->sampleRate)) + ".");
}

void PrepPanel::jumpMemory (int direction)
{
    const auto* r = session.record();
    if (r == nullptr || r->memoryCues.empty())
        return;
    // A small margin, so "previous" from just after a cue goes to the one before it.
    const auto now = playhead();
    const auto margin = static_cast<Frame> (0.25 * r->sampleRate);
    std::optional<Frame> target;
    for (const auto& c : r->memoryCues)
    {
        if (direction > 0 && c.frame > now + 1 && ! target)
            target = c.frame;
        if (direction < 0 && c.frame < now - margin)
            target = c.frame;
    }
    if (target)
        session.deck().seek (*target);
}

void PrepPanel::saveLoop (Frame start, double beats)
{
    const auto* r = session.record();
    if (r == nullptr)
        return;
    Frame end;
    if (r->grid && ! r->grid->isEmpty())
        end = static_cast<Frame> (std::llround (r->grid->frameAt (r->grid->beatAt (static_cast<double> (start)) + beats)));
    else
        end = start + static_cast<Frame> (std::llround (beats * 60.0 / r->info.bpm.value_or (120.0) * r->sampleRate));
    end = std::min (end, r->lengthFrames);
    if (end <= start)
        return;

    std::optional<std::size_t> slot;
    for (std::size_t i = 0; i < r->savedLoops.size() && ! slot; ++i)
        if (! r->savedLoops[i])
            slot = i;
    if (! slot)
    {
        say ("All 8 loop slots are used. Shift-click a loop number to free one.");
        return;
    }
    edit ([&] (TrackRecord& rec) { rec.savedLoops[*slot] = SavedLoop { start, end, {}, std::nullopt, false }; });
    session.deck().setLoop (start, end);
    say ("Saved loop " + juce::String (*slot + 1) + " (" + juce::String (beats, 0) + " beats). It's playing; Exit Loop to carry on.");
}

void PrepPanel::loopIn()
{
    pendingLoopIn = snapped (playhead());
    say ("Loop in set. Play on, then Loop Out where the loop should end.");
    refresh();
}

void PrepPanel::loopOut()
{
    const auto* r = session.record();
    if (! pendingLoopIn || r == nullptr)
        return;
    const auto start = *pendingLoopIn;
    const auto end = snapped (playhead());
    pendingLoopIn.reset();
    if (end <= start)
    {
        say ("Loop Out has to come after Loop In.");
        refresh();
        return;
    }
    const auto beats = r->grid && ! r->grid->isEmpty()
                           ? r->grid->beatAt (static_cast<double> (end)) - r->grid->beatAt (static_cast<double> (start))
                           : 0.0;
    std::optional<std::size_t> slot;
    for (std::size_t i = 0; i < r->savedLoops.size() && ! slot; ++i)
        if (! r->savedLoops[i])
            slot = i;
    if (! slot)
    {
        say ("All 8 loop slots are used. Shift-click a loop number to free one.");
        refresh();
        return;
    }
    edit ([&] (TrackRecord& rec) { rec.savedLoops[*slot] = SavedLoop { start, end, {}, std::nullopt, false }; });
    session.deck().setLoop (start, end);
    session.deck().seek (start);
    say ("Saved loop " + juce::String (*slot + 1) + (beats > 0 ? " (" + juce::String (beats, 2) + " beats)." : juce::String (".")));
}

void PrepPanel::playSavedLoop (std::size_t slot)
{
    const auto* r = session.record();
    if (r == nullptr || ! r->savedLoops[slot])
        return;
    session.deck().setLoop (r->savedLoops[slot]->start, r->savedLoops[slot]->end);
    session.deck().seek (r->savedLoops[slot]->start);
    session.deck().play();
}

std::optional<std::size_t> PrepPanel::activeLoopSlot() const
{
    const auto* r = session.record();
    if (r == nullptr)
        return std::nullopt;
    for (std::size_t i = 0; i < r->savedLoops.size(); ++i)
        if (r->savedLoops[i] && deckIsPlayingLoop (session.deck(), *r->savedLoops[i]))
            return i;
    return std::nullopt;
}

void PrepPanel::deleteLoop (std::size_t slot)
{
    const auto* r = session.record();
    if (r == nullptr || ! r->savedLoops[slot])
        return;
    if (deckIsPlayingLoop (session.deck(), *r->savedLoops[slot]))
        session.deck().clearLoop();
    edit ([slot] (TrackRecord& rec) { rec.savedLoops[slot].reset(); });
    say ("Deleted loop " + juce::String (slot + 1) + ". Set a new one with Save Loop or Loop In / Loop Out.");
}

void PrepPanel::stepBeats (double beats)
{
    const auto* r = session.record();
    if (r == nullptr || session.state() != Session::State::loaded)
        return;
    const auto now = static_cast<double> (playhead());
    double target;
    if (r->grid && ! r->grid->isEmpty())
    {
        // From between beats, the first step lands on the nearest beat in that direction.
        const auto beat = r->grid->beatAt (now);
        const auto base = beats > 0 ? std::floor (beat + 1e-6) : std::ceil (beat - 1e-6);
        target = r->grid->frameAt (base + beats);
    }
    else
    {
        target = now + beats * 60.0 / r->info.bpm.value_or (120.0) * r->sampleRate;
    }
    session.deck().seek (std::clamp<Frame> (static_cast<Frame> (std::llround (target)), 0, std::max<Frame> (0, r->lengthFrames - 1)));
}

// --- Grid ---------------------------------------------------------------------------------------------------

void PrepPanel::setDownbeat (Frame at)
{
    const auto* r = session.record();
    if (r == nullptr)
        return;
    edit ([at] (TrackRecord& rec) {
        if (rec.grid && ! rec.grid->isEmpty())
            rec.grid = rec.grid->withDownbeatAt (at);
        else
            rec.grid = BeatGrid::constant (rec.sampleRate, rec.info.bpm.value_or (120.0), at);
    });
    say ("Beat 1 is at the playhead now." + juce::String (r->grid ? "" : " Check the tempo: it came from the analysis."));
}

void PrepPanel::nudgeGrid (double milliseconds)
{
    edit ([milliseconds] (TrackRecord& rec) {
        if (rec.grid && ! rec.grid->isEmpty())
            rec.grid = rec.grid->shifted (static_cast<Frame> (std::llround (milliseconds / 1000.0 * rec.sampleRate)));
    });
}

void PrepPanel::scaleTempo (double factor)
{
    edit ([factor] (TrackRecord& rec) {
        if (! rec.grid || rec.grid->isEmpty())
            return;
        rec.grid = rec.grid->withTempoScaled (factor);
        if (rec.grid->isConstantTempo())
            rec.info.bpm = rec.grid->anchors().front().bpm;
    });
}

void PrepPanel::setBpm (double bpm)
{
    if (bpm < 40.0 || bpm > 300.0)
    {
        say ("Type a tempo between 40 and 300 BPM.");
        refresh();
        return;
    }
    const auto at = static_cast<double> (playhead());
    edit ([bpm, at] (TrackRecord& rec) {
        if (rec.grid && ! rec.grid->isEmpty())
            rec.grid = rec.grid->withBpm (bpm, at);
        else
            rec.grid = BeatGrid::constant (rec.sampleRate, bpm, 0);
        if (rec.grid->isConstantTempo())
            rec.info.bpm = bpm;
    });
    refresh();
}

void PrepPanel::addTempoChange (Frame at)
{
    const auto* r = session.record();
    if (r == nullptr || ! r->grid || r->grid->isEmpty())
    {
        say ("Set beat 1 first (Downbeat Here), then add tempo changes.");
        return;
    }
    edit ([at] (TrackRecord& rec) {
        const auto& g = *rec.grid;
        const auto beat = g.nearestBeat (static_cast<double> (at));
        TempoAnchor a;
        a.frame = at;
        a.bpm = g.bpmAt (static_cast<double> (at));
        a.beatsPerBar = g.beatsPerBarAt (static_cast<double> (at));
        a.beatInBar = beat.beatInBar;
        rec.grid = g.withAnchor (a);
    });
    say ("New tempo starts at the playhead. Type its BPM in the box (with the playhead after it).");
}

void PrepPanel::removeTempoChangeNear (Frame at)
{
    const auto* r = session.record();
    if (r == nullptr || ! r->grid || r->grid->anchors().size() < 2)
        return;
    const auto& anchors = r->grid->anchors();
    std::size_t nearest = 0;
    for (std::size_t i = 1; i < anchors.size(); ++i)
        if (std::abs (anchors[i].frame - at) < std::abs (anchors[nearest].frame - at))
            nearest = i;
    edit ([nearest] (TrackRecord& rec) { rec.grid = rec.grid->withoutAnchor (nearest); });
}

void PrepPanel::clearGrid()
{
    edit ([] (TrackRecord& rec) { rec.grid.reset(); });
    say ("Grid cleared. Downbeat Here starts a new one.");
}

void PrepPanel::addBackgroundActions (juce::PopupMenu& menu, Frame at)
{
    const auto* r = session.record();
    if (r == nullptr || session.state() != Session::State::loaded)
        return;
    juce::PopupMenu hot;
    for (std::size_t i = 0; i < r->hotCues.size(); ++i)
        hot.addItem (juce::String ("Hot Cue ") + TrackRecord::hotCueLetter (i) + (r->hotCues[i] ? "  (move here)" : ""), [this, i, at] {
            edit ([i, at] (TrackRecord& rec) {
                if (rec.hotCues[i])
                    rec.hotCues[i]->frame = at;
                else
                    rec.hotCues[i] = HotCue { at, std::nullopt, {}, std::nullopt };
            });
        });
    menu.addSubMenu ("Set Hot Cue Here", hot);
    menu.addItem ("Add Memory Cue Here", [this, at] { addMemoryCue (at); });
    menu.addItem ("Save " + juce::String (loopLength()) + "-Beat Loop From Here", [this, at] { saveLoop (at, loopLength()); });
    menu.addSeparator();
    menu.addItem ("Beat 1 Here", [this, at] { setDownbeat (at); });
    if (r->grid && ! r->grid->isEmpty())
        menu.addItem ("Tempo Change Here", [this, at] { addTempoChange (at); });
    if (r->grid && r->grid->anchors().size() > 1)
        menu.addItem ("Remove Nearest Tempo Change", [this, at] { removeTempoChangeNear (at); });
}

// --- Target -------------------------------------------------------------------------------------------------

juce::StringArray PrepPanel::targetNotes() const
{
    juce::StringArray notes;
    const auto* r = session.record();
    if (r == nullptr || session.state() != Session::State::loaded)
        return notes;
    if (targetBox.getSelectedId() == static_cast<int> (Target::serato))
    {
        for (const auto& n : serato::exportTrack (*r).notes)
            notes.add (juce::String::fromUTF8 (n.c_str()));
        if (r->grid && ! r->grid->isEmpty())
            notes.add ("The grid stays in DSS1: Serato grid export is off unless you turn it on in Deliver");
    }
    else
    {
        std::size_t hotLoops = 0;
        for (const auto& c : r->hotCues)
            if (c && c->loopEnd)
                ++hotLoops;
        std::size_t loops = 0;
        for (const auto& l : r->savedLoops)
            if (l)
                ++loops;
        if (loops > 0)
            notes.add (plural (loops, "saved loop goes", "saved loops go") + " to Rekordbox as memory loops");
        if (r->memoryCues.size() > 10)
            notes.add ("CDJs show the first 10 memory cues; Rekordbox keeps all " + juce::String (r->memoryCues.size()));
        if (! r->grid || r->grid->isEmpty())
            notes.add ("No grid: Rekordbox will use its own analysis");
        juce::ignoreUnused (hotLoops);
    }
    for (const auto& p : r->problems())
        notes.add (juce::String::fromUTF8 (p.c_str()));
    return notes;
}

// --- Layout -------------------------------------------------------------------------------------------------

void PrepPanel::paint (juce::Graphics& g)
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

    const auto label = [&] (juce::Component& c, const juce::String& text, const juce::String& sub, int width = 400) {
        hw::silk (g, text, sub, c.getBounds().toFloat().withWidth (static_cast<float> (width)).withHeight (16).translated (0, -21));
    };
    hw::silk (g, "WAVEFORM", "GRID / CUES / LOOPS", juce::Rectangle<float> (waveform.getX(), messageArea.getY() + 2.0f, 360, 16));
    label (targetBox, "TARGET", {}, 120);
    label (zoomOut, "ZOOM", {}, 120);
    label (*hotButtons[0], "HOT CUES", "A-H   CLICK: SET OR JUMP   SHIFT-CLICK: DELETE");
    label (addMemoryButton, "CUES", "MEMORY / LOOPS");
    label (*loopButtons[0], "SAVED LOOPS", "1-8   CLICK: PLAY   SHIFT-CLICK: DELETE");
    label (bpmEditor, "GRID", "BPM AT PLAYHEAD");
    label (loopKnob, "LOOP", "BEATS", 120);
    label (jumpKnob, "JUMP", "BEATS", 120);

    // Message / target notes on a small display, right of the unit's title.
    const auto notesArea = messageArea.toFloat().withTrimmedLeft (370);
    hw::recess (g, notesArea.reduced (0, 1));
    const auto notes = targetNotes();
    if (message.isNotEmpty())
        hw::glowText (g, message.toUpperCase(), notesArea.reduced (10, 0), hw::vfd (20.0f));
    else if (! notes.isEmpty())
        hw::glowText (g, notes.joinIntoString ("   |   ").toUpperCase(), notesArea.reduced (10, 0), hw::vfd (20.0f), hw::colour::amberMid);
    hw::glass (g, notesArea.reduced (0, 1));

    hw::recess (g, waveform.getBounds().toFloat());

    // Knob readouts.
    for (const auto& [knob, value] : { std::pair<juce::Slider*, int> { &loopKnob, loopLength() }, { &jumpKnob, session.jumpSize } })
        hw::glowText (g, juce::String (value) + (value == 1 ? " BEAT" : " BEATS"),
                      knob->getBounds().toFloat().withY (static_cast<float> (knob->getBottom()) - 4.0f).withHeight (22), hw::vfd (22.0f),
                      hw::colour::amber, juce::Justification::centred);
}

void PrepPanel::paintOverChildren (juce::Graphics& g)
{
    hw::glass (g, waveform.getBounds().toFloat());
}

void PrepPanel::resized()
{
    auto all = getLocalBounds();
    controlUnit = all.removeFromBottom (juce::jmin (290, all.getHeight() / 2));
    waveUnit = all;

    // --- Waveform unit ---
    auto area = waveUnit.reduced (hw::RackUnit::earWidth, 0).reduced (30, 0);
    area.removeFromTop (30);
    area.removeFromBottom (22);
    auto top = area.removeFromTop (40);
    wholeTrack.setBounds (top.removeFromRight (90).reduced (0, 2));
    top.removeFromRight (6);
    zoomIn.setBounds (top.removeFromRight (44).reduced (0, 2));
    top.removeFromRight (6);
    zoomOut.setBounds (top.removeFromRight (44).reduced (0, 2));
    top.removeFromRight (20);
    snapButton.setBounds (top.removeFromRight (100));
    top.removeFromRight (12);
    targetBox.setBounds (top.removeFromRight (190));
    top.removeFromRight (20);
    messageArea = top.withTrimmedTop (6).withTrimmedBottom (2);
    area.removeFromTop (14);
    waveform.setBounds (area.reduced (4));

    // --- Control unit ---
    area = controlUnit.reduced (hw::RackUnit::earWidth, 0).reduced (30, 0);
    area.removeFromTop (40);
    area.removeFromBottom (24);
    auto knobs = area.removeFromRight (250);
    area.removeFromRight (24);
    auto grid = area.removeFromRight (juce::jmin (380, area.getWidth() / 3));
    area.removeFromRight (24);

    // Hot cues, then the cue & loop keys, then the saved loop slots.
    auto row = area.removeFromTop (58);
    const auto padW = (row.getWidth() - 7 * 8) / 8;
    for (auto& b : hotButtons)
    {
        b->setBounds (row.removeFromLeft (padW));
        row.removeFromLeft (8);
    }
    area.removeFromTop (30);
    row = area.removeFromTop (42);
    const auto keyW = (row.getWidth() - 7 * 6) / 8;
    for (auto* b : { &addMemoryButton, &prevMemoryButton, &nextMemoryButton, &saveLoopButton, &loopInButton, &loopOutButton, &exitLoopButton, &deleteLoopButton })
    {
        b->setBounds (row.removeFromLeft (keyW));
        row.removeFromLeft (6);
    }
    area.removeFromTop (30);
    row = area.removeFromTop (36);
    for (auto& b : loopButtons)
    {
        b->setBounds (row.removeFromLeft (44));
        row.removeFromLeft (6);
    }

    // Grid: BPM display, then two rows of keys.
    bpmEditor.setBounds (grid.removeFromTop (50));
    grid.removeFromTop (12);
    auto g1 = grid.removeFromTop (42);
    const auto gw = (g1.getWidth() - 3 * 6) / 4;
    for (auto* b : { &downbeatButton, &nudgeEarlier, &nudgeLater, &halveButton })
    {
        b->setBounds (g1.removeFromLeft (gw));
        g1.removeFromLeft (6);
    }
    grid.removeFromTop (10);
    auto g2 = grid.removeFromTop (42);
    for (auto* b : { &doubleButton, &tempoChangeButton, &clearGridButton })
    {
        b->setBounds (g2.removeFromLeft (b == &tempoChangeButton ? gw * 2 + 6 : gw));
        g2.removeFromLeft (6);
    }

    // Knobs.
    knobs.removeFromBottom (24);
    loopKnob.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2).withSizeKeepingCentre (120, 120));
    jumpKnob.setBounds (knobs.withSizeKeepingCentre (120, 120));
}

bool PrepPanel::keyPressed (const juce::KeyPress& key)
{
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    const bool shift = key.getModifiers().isShiftDown();
    if (key == juce::KeyPress::spaceKey)
    {
        session.playPressed();
        return true;
    }
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
        return false;
    if (c == 'q')
    {
        // A key press can't be held like the button, so Q acts as a tap: set the cue, or return to it.
        session.cuePressed (true);
        session.cuePressed (false);
        return true;
    }
    if (c == '[' || c == ']')
    {
        session.jumpBeats ((c == '[' ? -1.0 : 1.0) * session.jumpSize);
        return true;
    }
    if (c == '=' || c == '+' || c == '-')
    {
        waveform.zoomBy (c == '-' ? 2.0 : 0.5);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::homeKey)
    {
        session.deck().seek (0);
        return true;
    }
    if (c >= 'a' && c <= 'h')
    {
        hotCuePressed (static_cast<std::size_t> (c - 'a'), shift);
        return true;
    }
    if (c == 'm')
    {
        addMemoryCue (snapped (playhead()));
        return true;
    }
    if (c == 'l')
    {
        saveLoop (snapped (playhead()), loopLength());
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
    {
        const auto sign = key.getKeyCode() == juce::KeyPress::leftKey ? -1.0 : 1.0;
        stepBeats (sign * (shift ? 4.0 : 1.0));
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey)
    {
        jumpMemory (key.getKeyCode() == juce::KeyPress::upKey ? -1 : 1);
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        session.deck().clearLoop();
        return true;
    }
    return false;
}

} // namespace dss1::ui
