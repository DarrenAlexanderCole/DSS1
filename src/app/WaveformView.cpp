#include "WaveformView.h"
#include "Scope.h"
#include "Theme.h"
#include "Hardware.h"

#include "core/SeratoTags.h"

#include <cmath>

namespace dss1::ui
{

namespace
{
    constexpr float flagHeight = 18.0f;   // hot cue flags along the top
    constexpr float loopLane = 16.0f;     // saved loops along the bottom
    constexpr float grabDistance = 6.0f;

    juce::Colour toColour (const CueColour& c) { return juce::Colour (c.red, c.green, c.blue); }
}

bool deckIsPlayingLoop (engine::Deck& deck, const SavedLoop& loop)
{
    return deck.isLooping() && std::abs (deck.loopStartFrame() - static_cast<double> (loop.start)) < 2.0
           && std::abs (deck.loopEndFrame() - static_cast<double> (loop.end)) < 2.0;
}

juce::Colour hotCueColour (const TrackRecord& r, std::size_t slot)
{
    const auto& cue = r.hotCues[slot];
    return toColour (cue && cue->colour ? *cue->colour : serato::defaultCueColour (static_cast<int> (slot)));
}

WaveformView::WaveformView (Session& s) : session (s)
{
    session.addChangeListener (this);
    setWantsKeyboardFocus (false);
    startTimerHz (60);
}

WaveformView::~WaveformView()
{
    session.removeChangeListener (this);
    stopTimer();
}

void WaveformView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const auto state = session.state();
    if (state == Session::State::loaded && lastState != Session::State::loaded)
    {
        // A new track: start with about 16 beats (or 8 seconds) across the view, from the start.
        const auto* r = session.record();
        const auto rate = r != nullptr && r->sampleRate > 0 ? r->sampleRate : 44100.0;
        const auto seconds = r != nullptr && r->info.bpm ? 16.0 * 60.0 / *r->info.bpm : 8.0;
        framesPerPixel = seconds * rate / std::max (1, getWidth());
        targetFramesPerPixel = framesPerPixel;
        viewStart = -visibleFrames() / 3.0;
        following = true;
    }
    lastState = state;
    clampView();
    repaint();
}

void WaveformView::timerCallback()
{
    auto& deck = session.deck();
    if (! following && followAgainAt != 0 && juce::Time::getMillisecondCounter() >= followAgainAt && ! dragging)
    {
        following = true;
        followAgainAt = 0;
    }

    // Ease the zoom towards its target (in proportion, so zooming in and out feel the same), holding the
    // anchor spot still under the mouse.
    if (std::abs (std::log (targetFramesPerPixel / framesPerPixel)) > 0.002)
    {
        framesPerPixel *= std::pow (targetFramesPerPixel / framesPerPixel, 0.35);
        viewStart = zoomAnchorFrame - zoomAnchorX * framesPerPixel;
        clampView();
    }
    else if (std::abs (framesPerPixel - targetFramesPerPixel) > 1e-9)
    {
        framesPerPixel = targetFramesPerPixel;
        viewStart = zoomAnchorFrame - zoomAnchorX * framesPerPixel;
        clampView();
    }

    if (deck.isPlaying() && following && ! dragging && getWidth() > 0)
    {
        // Keep the playhead a third of the way across, so what's coming is in view.
        viewStart = deck.position() - visibleFrames() / 3.0;
        clampView();
    }
    repaint();
}

double WaveformView::visibleFrames() const
{
    return framesPerPixel * std::max (1, getWidth());
}

void WaveformView::clampView()
{
    const auto length = static_cast<double> (session.deck().length());
    if (length <= 0)
        return;  // nothing loaded: keep the zoom for the next track
    framesPerPixel = juce::jlimit (2.0, std::max (2.0, length / std::max (1, getWidth()) * 1.05), framesPerPixel);
    viewStart = juce::jlimit (-visibleFrames() / 3.0, std::max (0.0, length - visibleFrames() * 2.0 / 3.0), viewStart);
}

double WaveformView::maxFramesPerPixel() const
{
    const auto length = static_cast<double> (session.deck().length());
    return std::max (minFramesPerPixel(), length / std::max (1, getWidth()) * 1.05);
}

void WaveformView::zoomBy (double factor, std::optional<float> anchorX)
{
    auto& deck = session.deck();
    const auto playheadX = xForFrame (deck.position());
    const bool playheadInView = playheadX >= 0 && playheadX <= static_cast<float> (getWidth());
    float x;
    if (deck.isPlaying() && following && playheadInView)
        x = playheadX;  // while playing, the playhead stays put
    else if (anchorX)
        x = *anchorX;
    else
        x = playheadInView ? playheadX : static_cast<float> (getWidth()) / 2.0f;

    zoomAnchorX = x;
    zoomAnchorFrame = frameAtX (x);
    targetFramesPerPixel = juce::jlimit (minFramesPerPixel(), maxFramesPerPixel(), targetFramesPerPixel * factor);
}

void WaveformView::showWholeTrack()
{
    zoomAnchorX = 0;
    zoomAnchorFrame = 0;
    targetFramesPerPixel = maxFramesPerPixel();
    following = false;
    followAgainAt = juce::Time::getMillisecondCounter() + 1500;
}

double WaveformView::frameAtX (float x) const
{
    return viewStart + static_cast<double> (x) * framesPerPixel;
}

float WaveformView::xForFrame (double frame) const
{
    return static_cast<float> ((frame - viewStart) / framesPerPixel);
}

void WaveformView::resized()
{
    clampView();
}

// --- Painting -----------------------------------------------------------------------------------------------

void WaveformView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (hw::colour::glassBlack);
    g.fillRoundedRectangle (bounds, 4.0f);
    // A faint graticule behind the glass.
    scope::graticule (g, bounds.reduced (2), 16, 4, hw::colour::amber);

    const auto* record = session.record();
    if (record == nullptr || session.state() != Session::State::loaded)
    {
        hw::glowText (g, session.state() == Session::State::loading ? "LOADING..."
                         : session.state() == Session::State::failed ? "COULDN'T OPEN THIS TRACK: " + session.problem().toUpperCase()
                                                                       : "CHOOSE A TRACK IN CATALOG (DOUBLE-CLICK IT), THEN PREP IT HERE",
                      bounds, hw::vfd (26.0f), hw::colour::amberMid, juce::Justification::centred);
        return;
    }

    auto wave = bounds.withTrimmedTop (flagHeight + 2).withTrimmedBottom (loopLane + 2);
    paintGrid (g, wave, *record);
    paintWaveform (g, wave);
    paintMarkers (g, bounds, dragRecord ? *dragRecord : *record);

    // Playhead.
    const auto x = xForFrame (session.deck().position());
    g.setColour (Colours::accent);
    g.fillRect (x - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
}

void WaveformView::paintWaveform (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& peaks = session.peaks();
    if (peaks.empty())
        return;
    const auto scale = area.getHeight() * 0.48f;
    const auto blocks = static_cast<double> (peaks.high.size());
    const auto played = session.deck().position();

    std::vector<scope::Span> cols (static_cast<std::size_t> (juce::jmax (0, getWidth())));
    int first = -1, last = -1;
    for (int x = 0; x < getWidth(); ++x)
    {
        const auto from = frameAtX (static_cast<float> (x)) / peaks.blockSize;
        const auto to = frameAtX (static_cast<float> (x + 1)) / peaks.blockSize;
        if (to < 0 || from >= blocks)
            continue;
        auto b0 = static_cast<std::size_t> (std::max (0.0, std::floor (from)));
        auto b1 = static_cast<std::size_t> (std::min (blocks, std::max (std::ceil (to), static_cast<double> (b0 + 1))));
        float lo = 0, hi = 0;
        for (auto b = b0; b < b1; ++b)
        {
            lo = std::min (lo, peaks.low[b]);
            hi = std::max (hi, peaks.high[b]);
        }
        cols[static_cast<std::size_t> (x)] = { lo, hi };
        if (first < 0)
            first = x;
        last = x;
    }
    if (first < 0)
        return;
    // Only the part of the window that has audio, so the shape starts and ends where the track does.
    std::vector<scope::Span> shown (cols.begin() + first, cols.begin() + last + 1);
    scope::wave (g, juce::Rectangle<float> (static_cast<float> (first), area.getY(), static_cast<float> (last - first + 1), area.getHeight()),
                 shown, 1.0f, hw::colour::amber, xForFrame (played), scale / area.getHeight());
}

void WaveformView::paintGrid (juce::Graphics& g, juce::Rectangle<float> area, const TrackRecord& r) const
{
    if (! r.grid || r.grid->isEmpty())
        return;
    const auto& grid = *r.grid;
    const auto pixelsPerBeat = grid.framesPerBeatAt (viewStart) / framesPerPixel;
    const bool showBeats = pixelsPerBeat >= 5.0;
    const bool showBars = pixelsPerBeat * grid.beatsPerBarAt (viewStart) >= 5.0;
    if (! showBars)
        return;

    g.setFont (juce::FontOptions (11.0f));
    for (const auto& beat : grid.beatsBetween (std::max (0.0, viewStart), viewStart + visibleFrames()))
    {
        if (! beat.isDownbeat() && ! showBeats)
            continue;
        const auto x = xForFrame (beat.frame);
        if (beat.isDownbeat())
        {
            g.setColour (Colours::downbeat);
            g.fillRect (x - 0.5f, area.getY(), 1.5f, area.getHeight());
            const auto bar = static_cast<std::int64_t> (std::floor (static_cast<double> (beat.index) / grid.beatsPerBarAt (beat.frame))) + 1;
            if (pixelsPerBeat * grid.beatsPerBarAt (beat.frame) >= 28.0)
            {
                g.setColour (hw::colour::amberMid);
                g.setFont (hw::vfd (16.0f));
                g.drawText (juce::String (bar), juce::Rectangle<float> (x + 3, area.getY() + 1, 40, 14), juce::Justification::left);
            }
        }
        else
        {
            g.setColour (Colours::beat);
            g.fillRect (x, area.getY(), 1.0f, area.getHeight());
        }
    }

    // Tempo changes on a variable grid.
    if (grid.anchors().size() > 1)
        for (const auto& a : grid.anchors())
        {
            const auto x = xForFrame (static_cast<double> (a.frame));
            if (x < -60 || x > static_cast<float> (getWidth()))
                continue;
            g.setColour (Colours::tempoMark);
            g.fillRect (x - 1.0f, area.getY(), 2.0f, area.getHeight());
            g.drawText (juce::String (a.bpm, 2), juce::Rectangle<float> (x + 3, area.getBottom() - 14, 60, 12), juce::Justification::left);
        }
}

void WaveformView::paintMarkers (juce::Graphics& g, juce::Rectangle<float> area, const TrackRecord& r) const
{
    const auto top = area.getY();
    const auto bottom = area.getBottom();

    // Saved loops: a band along the bottom, and a light wash over the waveform.
    for (std::size_t i = 0; i < r.savedLoops.size(); ++i)
    {
        const auto& loop = r.savedLoops[i];
        if (! loop)
            continue;
        const auto x0 = xForFrame (static_cast<double> (loop->start)), x1 = xForFrame (static_cast<double> (loop->end));
        if (x1 < 0 || x0 > static_cast<float> (getWidth()))
            continue;
        const auto colour = loop->colour ? toColour (*loop->colour) : juce::Colour (0xff2fa8d8);
        g.setColour (colour.withAlpha (0.12f));
        g.fillRect (x0, top + flagHeight, x1 - x0, bottom - top - flagHeight - loopLane);
        g.setColour (colour.withAlpha (0.8f));
        g.fillRect (x0, bottom - loopLane, std::max (2.0f, x1 - x0), loopLane);
        g.setColour (Colours::background);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("Loop " + juce::String (i + 1) + (loop->name.empty() ? juce::String() : "  " + juce::String::fromUTF8 (loop->name.c_str())),
                    juce::Rectangle<float> (x0 + 3, bottom - loopLane, std::max (0.0f, x1 - x0 - 4), loopLane), juce::Justification::centredLeft, true);
    }

    // The loop the deck is playing now.
    if (auto& deck = session.deck(); deck.isLooping())
    {
        const auto x0 = xForFrame (deck.loopStartFrame()), x1 = xForFrame (deck.loopEndFrame());
        g.setColour (Colours::accent.withAlpha (0.18f));
        g.fillRect (x0, top + flagHeight, x1 - x0, bottom - top - flagHeight - loopLane);
        g.setColour (Colours::accent);
        g.drawRect (x0, top + flagHeight, x1 - x0, bottom - top - flagHeight - loopLane, 1.5f);
    }

    // Memory cues: a thin line and a small triangle at the bottom of the waveform.
    for (const auto& cue : r.memoryCues)
    {
        const auto x = xForFrame (static_cast<double> (cue.frame));
        if (x < -10 || x > static_cast<float> (getWidth()) + 10)
            continue;
        const auto colour = cue.colour ? toColour (*cue.colour) : juce::Colour (0xffe0e0e0);
        g.setColour (colour.withAlpha (0.5f));
        g.fillRect (x, top + flagHeight, 1.0f, bottom - top - flagHeight - loopLane);
        juce::Path tri;
        const auto y = bottom - loopLane - 2;
        tri.addTriangle (x - 6, y, x + 6, y, x, y - 9);
        g.setColour (colour);
        g.fillPath (tri);
    }

    // Hot cues: a line and a lettered flag along the top. A hot loop shades its length.
    for (std::size_t i = 0; i < r.hotCues.size(); ++i)
    {
        const auto& cue = r.hotCues[i];
        if (! cue)
            continue;
        const auto x = xForFrame (static_cast<double> (cue->frame));
        const auto colour = hotCueColour (r, i);
        if (cue->loopEnd)
        {
            const auto x1 = xForFrame (static_cast<double> (*cue->loopEnd));
            g.setColour (colour.withAlpha (0.15f));
            g.fillRect (x, top + flagHeight, x1 - x, bottom - top - flagHeight - loopLane);
        }
        if (x < -30 || x > static_cast<float> (getWidth()) + 2)
            continue;
        g.setColour (colour);
        g.fillRect (x - 0.5f, top, 1.5f, bottom - top - loopLane);
        g.fillRoundedRectangle (x, top, 20.0f, flagHeight, 3.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String::charToString (TrackRecord::hotCueLetter (i)), juce::Rectangle<float> (x, top, 20.0f, flagHeight),
                    juce::Justification::centred);
    }
}

// --- Mouse --------------------------------------------------------------------------------------------------

std::optional<WaveformView::Marker> WaveformView::markerAt (juce::Point<float> p) const
{
    const auto* r = session.record();
    if (r == nullptr)
        return std::nullopt;
    const auto near = [&] (Frame f, float fromX = 0, float toX = 0) {
        const auto x = xForFrame (static_cast<double> (f));
        return p.x >= x - grabDistance + fromX && p.x <= x + grabDistance + toX;
    };
    const auto h = static_cast<float> (getHeight());
    if (p.y <= flagHeight + 2)
        for (std::size_t i = 0; i < r->hotCues.size(); ++i)
            if (r->hotCues[i] && near (r->hotCues[i]->frame, 0, 20))
                return Marker { Marker::Kind::hotCue, i };
    if (p.y >= h - loopLane - 2)
        for (std::size_t i = 0; i < r->savedLoops.size(); ++i)
            if (const auto& l = r->savedLoops[i]; l && p.x >= xForFrame (static_cast<double> (l->start)) - grabDistance
                                                     && p.x <= xForFrame (static_cast<double> (l->end)))
                return Marker { Marker::Kind::loop, i };
    if (p.y >= h - loopLane - 14)
        for (std::size_t i = 0; i < r->memoryCues.size(); ++i)
            if (near (r->memoryCues[i].frame))
                return Marker { Marker::Kind::memoryCue, i };
    for (std::size_t i = 0; i < r->hotCues.size(); ++i)
        if (r->hotCues[i] && near (r->hotCues[i]->frame))
            return Marker { Marker::Kind::hotCue, i };
    return std::nullopt;
}

Frame WaveformView::markerFrame (const TrackRecord& r, const Marker& m) const
{
    switch (m.kind)
    {
        case Marker::Kind::hotCue:    return r.hotCues[m.index] ? r.hotCues[m.index]->frame : 0;
        case Marker::Kind::memoryCue: return m.index < r.memoryCues.size() ? r.memoryCues[m.index].frame : 0;
        case Marker::Kind::loop:      return r.savedLoops[m.index] ? r.savedLoops[m.index]->start : 0;
    }
    return 0;
}

void WaveformView::moveMarker (TrackRecord& r, const Marker& m, Frame to) const
{
    to = std::clamp<Frame> (to, 0, std::max<Frame> (0, r.lengthFrames - 1));
    switch (m.kind)
    {
        case Marker::Kind::hotCue:
            if (auto& c = r.hotCues[m.index])
            {
                if (c->loopEnd)
                    *c->loopEnd += to - c->frame;
                c->frame = to;
            }
            break;
        case Marker::Kind::memoryCue:
            if (m.index < r.memoryCues.size())
                r.memoryCues[m.index].frame = to;
            break;
        case Marker::Kind::loop:
            if (auto& l = r.savedLoops[m.index])
            {
                l->end += to - l->start;
                l->start = to;
            }
            break;
    }
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    dragged = false;
    dragStartView = viewStart;
    dragging = markerAt (e.position);
    dragRecord.reset();

    if (e.mods.isPopupMenu())
    {
        if (dragging)
            showMarkerMenu (*dragging);
        else
            showBackgroundMenu (snap (static_cast<Frame> (std::llround (frameAtX (e.position.x)))));
        dragging.reset();
        return;
    }
    if (dragging && session.record() != nullptr)
        dragRecord = *session.record();
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    if (e.getDistanceFromDragStart() > 3)
        dragged = true;
    if (! dragged)
        return;

    if (dragging && dragRecord)
    {
        const auto* original = session.record();
        if (original == nullptr)
            return;
        const auto startFrame = markerFrame (*original, *dragging);
        const auto delta = static_cast<double> (e.getDistanceFromDragStartX()) * framesPerPixel;
        auto moved = *original;
        moveMarker (moved, *dragging, snap (static_cast<Frame> (std::llround (static_cast<double> (startFrame) + delta))));
        // Moving the loop the deck is playing moves what you hear (and the orange box) with it.
        if (dragging->kind == Marker::Kind::loop && dragRecord && dragRecord->savedLoops[dragging->index]
            && moved.savedLoops[dragging->index] && deckIsPlayingLoop (session.deck(), *dragRecord->savedLoops[dragging->index]))
            session.deck().setLoop (moved.savedLoops[dragging->index]->start, moved.savedLoops[dragging->index]->end);
        dragRecord = std::move (moved);
        repaint();
        return;
    }

    // Scroll the view; stop following the playhead until it's let go.
    following = false;
    viewStart = dragStartView - static_cast<double> (e.getDistanceFromDragStartX()) * framesPerPixel;
    clampView();
    zoomAnchorFrame = frameAtX (static_cast<float> (zoomAnchorX));
    repaint();
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    if (dragging && dragRecord && dragged)
    {
        auto record = std::move (*dragRecord);
        dragRecord.reset();
        if (dragging->kind == Marker::Kind::memoryCue)
        {
            // Keep memory cues in frame order.
            std::sort (record.memoryCues.begin(), record.memoryCues.end(), [] (const MemoryCue& a, const MemoryCue& b) { return a.frame < b.frame; });
        }
        session.update (record);
    }
    else if (! dragged)
    {
        // A click: jump there (a marker click jumps to the marker).
        const auto* r = session.record();
        const auto target = dragging && r != nullptr ? markerFrame (*r, *dragging) : static_cast<Frame> (std::llround (frameAtX (e.position.x)));
        session.deck().seek (std::max<Frame> (0, target));
    }
    dragging.reset();
    dragRecord.reset();
    following = true;
    repaint();
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (markerAt (e.position) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Up and down zoom around the mouse (like Rekordbox and Serato); sideways swipes, or Shift / Command with
    // the wheel, move through the track.
    const bool sideways = std::abs (wheel.deltaX) > std::abs (wheel.deltaY);
    const auto mods = juce::ModifierKeys::currentModifiers;
    if (! sideways && ! mods.isShiftDown() && ! mods.isCommandDown())
    {
        const auto amount = static_cast<double> (wheel.deltaY) * (wheel.isSmooth ? 1.5 : 1.0);
        zoomBy (std::pow (2.0, -amount * 1.5), e.position.x);
        return;
    }
    const auto delta = sideways ? wheel.deltaX : wheel.deltaY;
    following = false;
    followAgainAt = juce::Time::getMillisecondCounter() + 1500;
    viewStart -= static_cast<double> (delta) * visibleFrames() * 0.4;
    zoomAnchorFrame = frameAtX (static_cast<float> (zoomAnchorX));
    clampView();
    repaint();
}

void WaveformView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (scaleFactor > 0)
        zoomBy (1.0 / static_cast<double> (scaleFactor), e.position.x);
}

// --- Menus --------------------------------------------------------------------------------------------------

void WaveformView::showMarkerMenu (const Marker& m)
{
    const auto* r = session.record();
    if (r == nullptr)
        return;
    juce::PopupMenu menu;
    juce::String what;
    switch (m.kind)
    {
        case Marker::Kind::hotCue:    what = juce::String ("Hot Cue ") + TrackRecord::hotCueLetter (m.index); break;
        case Marker::Kind::memoryCue: what = "Memory Cue"; break;
        case Marker::Kind::loop:      what = "Loop " + juce::String (m.index + 1); break;
    }
    menu.addSectionHeader (what);
    menu.addItem ("Jump Here", [this, frame = markerFrame (*r, m)] { session.deck().seek (frame); });
    if (m.kind == Marker::Kind::loop)
        menu.addItem ("Play This Loop", [this, m] {
            if (const auto* rec = session.record(); rec != nullptr && rec->savedLoops[m.index])
            {
                session.deck().setLoop (rec->savedLoops[m.index]->start, rec->savedLoops[m.index]->end);
                session.deck().seek (rec->savedLoops[m.index]->start);
                session.deck().play();
            }
        });
    menu.addItem ("Rename...", [this, m, what] {
        auto* window = new juce::AlertWindow ("Rename " + what, {}, juce::MessageBoxIconType::NoIcon, this);
        const auto* rec = session.record();
        std::string current;
        if (rec != nullptr)
        {
            if (m.kind == Marker::Kind::hotCue && rec->hotCues[m.index]) current = rec->hotCues[m.index]->name;
            if (m.kind == Marker::Kind::memoryCue && m.index < rec->memoryCues.size()) current = rec->memoryCues[m.index].name;
            if (m.kind == Marker::Kind::loop && rec->savedLoops[m.index]) current = rec->savedLoops[m.index]->name;
        }
        window->addTextEditor ("name", juce::String::fromUTF8 (current.c_str()));
        window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, m, window] (int result) {
            if (result != 1 || session.record() == nullptr)
                return;
            auto updated = *session.record();
            const auto name = window->getTextEditorContents ("name").trim().toStdString();
            if (m.kind == Marker::Kind::hotCue && updated.hotCues[m.index]) updated.hotCues[m.index]->name = name;
            if (m.kind == Marker::Kind::memoryCue && m.index < updated.memoryCues.size()) updated.memoryCues[m.index].name = name;
            if (m.kind == Marker::Kind::loop && updated.savedLoops[m.index]) updated.savedLoops[m.index]->name = name;
            session.update (updated);
        }), true);
    });

    if (m.kind == Marker::Kind::hotCue)
    {
        juce::PopupMenu colours;
        for (int c = 0; c < 8; ++c)
        {
            const auto colour = serato::defaultCueColour (c);
            colours.addColouredItem (100 + c, "Colour " + juce::String (c + 1), toColour (colour), true, false, nullptr);
        }
        menu.addSubMenu ("Colour", colours);
    }
    menu.addSeparator();
    menu.addItem ("Delete", [this, m] {
        if (session.record() == nullptr)
            return;
        auto rec = *session.record();
        if (m.kind == Marker::Kind::hotCue) rec.hotCues[m.index].reset();
        if (m.kind == Marker::Kind::memoryCue && m.index < rec.memoryCues.size()) rec.memoryCues.erase (rec.memoryCues.begin() + static_cast<std::ptrdiff_t> (m.index));
        if (m.kind == Marker::Kind::loop && rec.savedLoops[m.index])
        {
            if (deckIsPlayingLoop (session.deck(), *rec.savedLoops[m.index]))
                session.deck().clearLoop();
            rec.savedLoops[m.index].reset();
        }
        session.update (rec);
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, m] (int result) {
        if (result < 100 || result >= 108 || session.record() == nullptr || m.kind != Marker::Kind::hotCue)
            return;
        auto rec = *session.record();
        if (rec.hotCues[m.index])
        {
            rec.hotCues[m.index]->colour = serato::defaultCueColour (result - 100);
            session.update (rec);
        }
    });
}

void WaveformView::showBackgroundMenu (Frame at)
{
    juce::PopupMenu menu;
    if (addBackgroundActions)
        addBackgroundActions (menu, at);
    if (menu.getNumItems() > 0)
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

} // namespace dss1::ui
