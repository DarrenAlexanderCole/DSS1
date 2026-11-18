#include "PrepPanel.h"
#include "Theme.h"

#include "core/SeratoTags.h"
#include "core/TestExport.h"

#include <cmath>

namespace dss1::ui
{

namespace
{
    constexpr double loopBeats[] { 1, 2, 4, 8, 16, 32 };

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

    // Transport: a CDJ-style CUE, play, and beat jumps.
    startButton.setTooltip ("Back to the start (Home)");
    startButton.onClick = [this] { session.deck().seek (0); };
    jumpBackButton.setTooltip ("Jump back by the beat-jump size ([)");
    jumpBackButton.onClick = [this] { jumpBeats (-jumpSizeBox.getSelectedId()); };
    jumpForwardButton.setTooltip ("Jump forward by the beat-jump size (])");
    jumpForwardButton.onClick = [this] { jumpBeats (jumpSizeBox.getSelectedId()); };
    for (const int beats : { 1, 2, 4, 8, 16, 32, 64 })
        jumpSizeBox.addItem (juce::String (beats) + (beats == 1 ? " beat" : " beats"), beats);
    jumpSizeBox.setSelectedId (settings->getIntValue ("jumpBeats", 16), juce::dontSendNotification);
    jumpSizeBox.onChange = [this] { settings->setValue ("jumpBeats", jumpSizeBox.getSelectedId()); };
    cueButton.setTooltip ("Paused: set the cue point here, or hold to hear it from the cue. Playing: back to the cue "
                          "point and stop. (Key Q)");
    cueButton.onStateChange = [this] {
        const bool down = cueButton.isDown();
        if (down != cueWasDown)
        {
            cueWasDown = down;
            cuePressed (down);
        }
    };
    playButton.setTooltip ("Play or pause (Space)");
    playButton.onClick = [this] {
        cuePreviewing = false;
        if (session.deck().isPlaying())
            session.deck().pause();
        else
            session.deck().play();
    };
    for (auto* b : { &startButton, &jumpBackButton, &jumpForwardButton, &cueButton, &playButton })
        addAndMakeVisible (b);
    addAndMakeVisible (jumpSizeBox);

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

    for (const auto beats : loopBeats)
        loopLengthBox.addItem (juce::String (beats, 0) + (beats < 1.5 ? " beat" : " beats"), static_cast<int> (beats));
    loopLengthBox.setSelectedId (settings->getIntValue ("loopBeats", 8), juce::dontSendNotification);
    loopLengthBox.onChange = [this] { settings->setValue ("loopBeats", loopLengthBox.getSelectedId()); };
    saveLoopButton.setTooltip ("Save a loop of this many beats from the playhead into the next free slot (key L)");
    saveLoopButton.onClick = [this] { saveLoop (snapped (playhead()), loopLengthBox.getSelectedId()); };
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
    addAndMakeVisible (loopLengthBox);

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
        cuePoint = -1;  // set from the track once it's loaded
    }
    if (session.state() == Session::State::loaded && cuePoint < 0)
    {
        // Like a CDJ: the cue starts at the first memory cue, or the start of the track.
        const auto* r = session.record();
        cuePoint = r != nullptr && ! r->memoryCues.empty() ? r->memoryCues.front().frame : 0;
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
    repaint (jumpForwardButton.getRight(), cueButton.getY(), 340, cueButton.getHeight());
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
        b.setColour (juce::TextButton::buttonColourId, set ? hotCueColour (*r, i) : Colours::panel);
        b.setColour (juce::TextButton::textColourOffId, set ? juce::Colours::white : Colours::dimText);
    }
    for (std::size_t i = 0; i < loopButtons.size(); ++i)
    {
        auto& b = *loopButtons[i];
        const bool set = ready && r->savedLoops[i].has_value();
        b.setEnabled (set);
        b.setColour (juce::TextButton::buttonColourId, set ? juce::Colour (0xff2fa8d8) : Colours::panel);
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
    deleteLoopButton.setButtonText (active ? "Delete Loop " + juce::String (*active + 1) : juce::String ("Delete Loop"));
    for (auto* b : { &startButton, &jumpBackButton, &jumpForwardButton, &cueButton, &playButton })
        b->setEnabled (ready);
    playButton.setButtonText (session.deck().isPlaying() && ! cuePreviewing ? "Pause" : "Play");
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
    repaint (0, 0, getWidth(), waveform.getY());
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
    repaint (0, 0, getWidth(), waveform.getY());
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

void PrepPanel::jumpBeats (double beats)
{
    const auto* r = session.record();
    if (r == nullptr || session.state() != Session::State::loaded)
        return;
    // Keeps the playhead's place within the beat, like beat jump on a DJ app.
    const auto now = static_cast<double> (playhead());
    const auto target = r->grid && ! r->grid->isEmpty() ? r->grid->frameAt (r->grid->beatAt (now) + beats)
                                                         : now + beats * 60.0 / r->info.bpm.value_or (120.0) * r->sampleRate;
    session.deck().seek (std::clamp<Frame> (static_cast<Frame> (std::llround (target)), 0, std::max<Frame> (0, r->lengthFrames - 1)));
}

void PrepPanel::cuePressed (bool down)
{
    if (session.state() != Session::State::loaded)
        return;
    auto& deck = session.deck();
    if (down)
    {
        if (deck.isPlaying() && ! cuePreviewing)
        {
            // Playing: back to the cue point, stopped.
            deck.pause();
            deck.seek (cuePoint);
            return;
        }
        const auto here = playhead();
        const auto rate = session.record()->sampleRate;
        if (std::abs (here - cuePoint) > static_cast<Frame> (0.005 * rate))
        {
            // Paused somewhere new: that's the cue point now.
            cuePoint = snapped (here);
            deck.seek (cuePoint);
            return;
        }
        // Paused on the cue point: play while it's held.
        cuePreviewing = true;
        deck.play();
        return;
    }
    if (cuePreviewing)
    {
        cuePreviewing = false;
        deck.pause();
        deck.seek (cuePoint);
    }
}

juce::String PrepPanel::positionText() const
{
    const auto* r = session.record();
    if (r == nullptr || session.state() != Session::State::loaded || r->sampleRate <= 0)
        return {};
    const auto pos = session.deck().position();
    juce::String text = formatTime (pos / r->sampleRate);
    if (r->grid && ! r->grid->isEmpty())
    {
        const auto beat = r->grid->nearestBeat (pos);
        const auto perBar = r->grid->beatsPerBarAt (pos);
        const auto bar = static_cast<long long> (std::floor (static_cast<double> (beat.index) / perBar));
        const auto inBar = static_cast<long long> (beat.index) - bar * static_cast<long long> (perBar);
        text << "   Bar " << (bar + 1) << "." << (inBar + 1);
    }
    return text;
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
    menu.addItem ("Save " + loopLengthBox.getText() + " Loop From Here", [this, at] { saveLoop (at, loopLengthBox.getSelectedId()); });
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
    g.fillAll (Colours::background);
    auto area = getLocalBounds().reduced (16, 10);
    auto header = area.removeFromTop (44);
    header.removeFromRight (560);  // controls

    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    const auto title = session.record() != nullptr ? session.title() : juce::String ("Prep");
    g.drawText (title, header.removeFromTop (24), juce::Justification::centredLeft, true);
    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (session.details(), header, juce::Justification::centredLeft, true);

    // A line of notes about the target, or the last action's message.
    auto notesArea = area.removeFromTop (20);
    const auto notes = targetNotes();
    g.setFont (juce::FontOptions (13.0f));
    if (message.isNotEmpty())
    {
        g.setColour (Colours::text.withAlpha (0.85f));
        g.drawText (message, notesArea, juce::Justification::centredLeft, true);
    }
    else if (! notes.isEmpty())
    {
        g.setColour (Colours::accent.brighter (0.4f));
        g.drawText (notes.joinIntoString ("   |   "), notesArea, juce::Justification::centredLeft, true);
    }

    // Labels for the control rows.
    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (12.0f));
    for (const auto& [y, text] : { std::pair { cueButton.getY(), "TRANSPORT" }, std::pair { hotButtons[0]->getY(), "HOT CUES" }, std::pair { addMemoryButton.getY(), "CUES & LOOPS" },
                                    std::pair { downbeatButton.getY(), "GRID" } })
        g.drawText (text, 16, y, 90, 28, juce::Justification::centredLeft);

    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 16.0f, juce::Font::plain));
    g.drawText (positionText(), jumpForwardButton.getRight() + 24, cueButton.getY(), 300, cueButton.getHeight(), juce::Justification::centredLeft);
}

void PrepPanel::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    auto header = area.removeFromTop (44).withSizeKeepingCentre (getWidth() - 32, 28);
    wholeTrack.setBounds (header.removeFromRight (100));
    header.removeFromRight (4);
    zoomIn.setBounds (header.removeFromRight (32));
    header.removeFromRight (4);
    zoomOut.setBounds (header.removeFromRight (32));
    header.removeFromRight (16);
    snapButton.setBounds (header.removeFromRight (130));
    header.removeFromRight (8);
    targetBox.setBounds (header.removeFromRight (220));

    area.removeFromTop (22);  // notes line

    constexpr int rowHeight = 30, gap = 8, labelWidth = 100;
    auto controls = area.removeFromBottom (4 * (rowHeight + gap) + 6);
    area.removeFromBottom (10);
    waveform.setBounds (area);

    auto row = controls.removeFromTop (rowHeight + 6);
    row.removeFromLeft (labelWidth);
    cueButton.setBounds (row.removeFromLeft (80));
    row.removeFromLeft (6);
    playButton.setBounds (row.removeFromLeft (80));
    row.removeFromLeft (20);
    startButton.setBounds (row.removeFromLeft (40));
    row.removeFromLeft (6);
    jumpBackButton.setBounds (row.removeFromLeft (44));
    row.removeFromLeft (4);
    jumpSizeBox.setBounds (row.removeFromLeft (100).reduced (0, 3));
    row.removeFromLeft (4);
    jumpForwardButton.setBounds (row.removeFromLeft (44));

    controls.removeFromTop (gap);
    row = controls.removeFromTop (rowHeight);
    row.removeFromLeft (labelWidth);
    const auto hotWidth = std::min (80, (row.getWidth() - 7 * 6) / 8);
    for (auto& b : hotButtons)
    {
        b->setBounds (row.removeFromLeft (hotWidth));
        row.removeFromLeft (6);
    }

    controls.removeFromTop (gap);
    row = controls.removeFromTop (rowHeight);
    row.removeFromLeft (labelWidth);
    addMemoryButton.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (4);
    prevMemoryButton.setBounds (row.removeFromLeft (30));
    row.removeFromLeft (2);
    nextMemoryButton.setBounds (row.removeFromLeft (30));
    row.removeFromLeft (20);
    loopLengthBox.setBounds (row.removeFromLeft (100));
    row.removeFromLeft (4);
    saveLoopButton.setBounds (row.removeFromLeft (90));
    row.removeFromLeft (12);
    loopInButton.setBounds (row.removeFromLeft (70));
    row.removeFromLeft (4);
    loopOutButton.setBounds (row.removeFromLeft (76));
    row.removeFromLeft (4);
    exitLoopButton.setBounds (row.removeFromLeft (76));
    row.removeFromLeft (4);
    deleteLoopButton.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (12);
    for (auto& b : loopButtons)
    {
        b->setBounds (row.removeFromLeft (30));
        row.removeFromLeft (3);
    }

    controls.removeFromTop (gap);
    row = controls.removeFromTop (rowHeight);
    row.removeFromLeft (labelWidth);
    bpmLabel.setBounds (row.removeFromLeft (36));
    bpmEditor.setBounds (row.removeFromLeft (74));
    row.removeFromLeft (12);
    downbeatButton.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (4);
    nudgeEarlier.setBounds (row.removeFromLeft (80));
    row.removeFromLeft (2);
    nudgeLater.setBounds (row.removeFromLeft (80));
    row.removeFromLeft (12);
    halveButton.setBounds (row.removeFromLeft (40));
    row.removeFromLeft (2);
    doubleButton.setBounds (row.removeFromLeft (40));
    row.removeFromLeft (12);
    tempoChangeButton.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (12);
    clearGridButton.setBounds (row.removeFromLeft (100));
}

bool PrepPanel::keyPressed (const juce::KeyPress& key)
{
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    const bool shift = key.getModifiers().isShiftDown();
    if (key == juce::KeyPress::spaceKey)
    {
        cuePreviewing = false;
        if (session.deck().isPlaying())
            session.deck().pause();
        else
            session.deck().play();
        return true;
    }
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
        return false;
    if (c == 'q')
    {
        // A key press can't be held like the button, so Q acts as a tap: set the cue, or return to it.
        cuePressed (true);
        cuePressed (false);
        return true;
    }
    if (c == '[' || c == ']')
    {
        jumpBeats ((c == '[' ? -1.0 : 1.0) * jumpSizeBox.getSelectedId());
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
        saveLoop (snapped (playhead()), loopLengthBox.getSelectedId());
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
