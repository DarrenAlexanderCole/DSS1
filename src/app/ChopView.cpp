#include "ChopView.h"
#include "ChopPanel.h"
#include "Hardware.h"

#include "core/SeratoTags.h"

namespace dss1::ui
{

namespace
{
    juce::Colour bankColour (int b)
    {
        const auto c = serato::defaultCueColour (b);
        return juce::Colour (c.red, c.green, c.blue);
    }
}

ChopView::ChopView (ChopPanel& o) : owner (o) {}

void ChopView::resized()
{
    auto b = getLocalBounds();
    overviewArea = b.removeFromBottom (34);
    b.removeFromBottom (6);
    waveArea = b;
    clampView();
}

double ChopView::length() const
{
    const auto* a = owner.shownAudio();
    return a != nullptr && ! a->channels.empty() ? static_cast<double> (a->channels.front().size()) : 0.0;
}

double ChopView::visibleFrames() const { return framesPerPixel * juce::jmax (1, waveArea.getWidth()); }
double ChopView::frameAtX (float x) const { return viewStart + static_cast<double> (x - static_cast<float> (waveArea.getX())) * framesPerPixel; }
float ChopView::xForFrame (double frame) const { return static_cast<float> (waveArea.getX()) + static_cast<float> ((frame - viewStart) / framesPerPixel); }

double ChopView::playhead (bool& playing) const
{
    const auto& b = owner.current();
    if (b.audio != nullptr && ! owner.sourceIsOnDeck (b))
    {
        auto& preview = owner.session.output().chopPreview();
        playing = preview.isPlaying();
        return playing ? preview.progress() * length() : static_cast<double> (owner.previewCursor);
    }
    playing = owner.session.deck().isPlaying();
    return owner.session.deck().position();
}

void ChopView::clampView()
{
    const auto len = length();
    if (len <= 0 || waveArea.getWidth() <= 0)
        return;
    const auto maxFpp = juce::jmax (0.5, len / waveArea.getWidth() * 1.02);
    framesPerPixel = juce::jlimit (0.5, maxFpp, framesPerPixel);
    targetFramesPerPixel = juce::jlimit (0.5, maxFpp, targetFramesPerPixel);
    viewStart = juce::jlimit (-visibleFrames() * 0.1, juce::jmax (0.0, len - visibleFrames() * 0.9), viewStart);
}

void ChopView::zoomBy (double factor, std::optional<float> anchorX)
{
    bool playing = false;
    const auto ph = xForFrame (playhead (playing));
    const bool inView = ph >= static_cast<float> (waveArea.getX()) && ph <= static_cast<float> (waveArea.getRight());
    float x;
    if (playing && following && inView)
        x = ph;
    else if (anchorX)
        x = *anchorX;
    else
        x = inView ? ph : static_cast<float> (waveArea.getCentreX());
    zoomAnchorX = x;
    zoomAnchorFrame = frameAtX (x);
    const auto len = length();
    targetFramesPerPixel = juce::jlimit (0.5, juce::jmax (0.5, len / juce::jmax (1, waveArea.getWidth()) * 1.02), targetFramesPerPixel * factor);
}

void ChopView::showRange (double from, double to)
{
    if (to <= from || waveArea.getWidth() <= 0)
        return;
    const auto span = (to - from) * 1.1;
    targetFramesPerPixel = framesPerPixel = juce::jmax (0.5, span / waveArea.getWidth());
    viewStart = from - (to - from) * 0.05;
    following = false;
    followAgainAt = 0;
    clampView();
    repaint();
}

void ChopView::showWhole()
{
    showRange (0, length());
}

void ChopView::reset()
{
    const auto& b = owner.current();
    const auto len = length();
    if (len <= 0)
        return;
    double from = len, to = 0;
    for (int p = 0; p < 16; ++p)
        if (const auto& m = b.marks[static_cast<std::size_t> (p)]; m)
        {
            from = juce::jmin (from, static_cast<double> (*m));
            to = juce::jmax (to, static_cast<double> (owner.sliceEnd (owner.bank, p).value_or (*m)));
        }
    if (to > from)
        return showRange (from, to);
    if (b.audio != nullptr && ! owner.sourceIsOnDeck (b))
        return showWhole();
    // Nothing chopped yet: about 12 seconds around the playhead.
    const auto rate = owner.shownAudio()->info.sampleRate;
    const auto at = owner.session.deck().position();
    showRange (at - 4 * rate, at + 8 * rate);
    following = true;
}

void ChopView::tick()
{
    if (! following && followAgainAt != 0 && juce::Time::getMillisecondCounter() >= followAgainAt && drag == Drag::none)
    {
        following = true;
        followAgainAt = 0;
    }
    if (std::abs (std::log (targetFramesPerPixel / framesPerPixel)) > 0.002)
    {
        framesPerPixel *= std::pow (targetFramesPerPixel / framesPerPixel, 0.35);
        viewStart = zoomAnchorFrame - (zoomAnchorX - waveArea.getX()) * framesPerPixel;
        clampView();
    }
    else if (std::abs (framesPerPixel - targetFramesPerPixel) > 1e-9)
    {
        framesPerPixel = targetFramesPerPixel;
        viewStart = zoomAnchorFrame - (zoomAnchorX - waveArea.getX()) * framesPerPixel;
        clampView();
    }
    // Keep a playing playhead in view: page along when it reaches the right side.
    bool playing = false;
    const auto ph = playhead (playing);
    if (playing && following && drag == Drag::none)
    {
        const auto x = xForFrame (ph);
        if (x > waveArea.getX() + waveArea.getWidth() * 0.85f || x < static_cast<float> (waveArea.getX()))
        {
            viewStart = ph - visibleFrames() * 0.15;
            clampView();
        }
    }
    repaint();
}

void ChopView::ensurePeaks (const io::DecodedAudio* a)
{
    if (a == peaksFor)
        return;
    peaksFor = a;
    peakMin.clear();
    peakMax.clear();
    if (a == nullptr || a->channels.empty())
        return;
    const auto n = a->channels.front().size();
    const auto blocks = n / peakBlock + 1;
    peakMin.assign (blocks, 0.0f);
    peakMax.assign (blocks, 0.0f);
    const auto chs = static_cast<float> (a->channels.size());
    for (std::size_t i = 0; i < n; ++i)
    {
        float v = 0;
        for (const auto& c : a->channels)
            v += c[i];
        v /= chs;
        auto& lo = peakMin[i / peakBlock];
        auto& hi = peakMax[i / peakBlock];
        lo = juce::jmin (lo, v);
        hi = juce::jmax (hi, v);
    }
}

void ChopView::paintWave (juce::Graphics& g, juce::Rectangle<float> area, const io::DecodedAudio& a) const
{
    const auto n = a.channels.front().size();
    const auto mid = area.getCentreY();
    const auto scale = area.getHeight() * 0.46f;
    const auto chs = static_cast<float> (a.channels.size());
    const auto sampleAt = [&] (std::size_t i) {
        float v = 0;
        for (const auto& c : a.channels)
            v += c[i];
        return v / chs;
    };
    if (framesPerPixel < 3.0)
    {
        // Close in: the actual wave, so zero crossings can be seen.
        juce::Path p;
        bool started = false;
        const auto first = static_cast<std::int64_t> (std::floor (frameAtX (area.getX())));
        const auto last = static_cast<std::int64_t> (std::ceil (frameAtX (area.getRight()))) + 1;
        for (auto i = juce::jmax<std::int64_t> (0, first); i < juce::jmin<std::int64_t> (static_cast<std::int64_t> (n), last); ++i)
        {
            const auto x = xForFrame (static_cast<double> (i));
            const auto y = mid - sampleAt (static_cast<std::size_t> (i)) * scale;
            if (! started)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
            started = true;
        }
        g.strokePath (p, juce::PathStrokeType (1.5f));
        if (framesPerPixel < 0.8)
            for (auto i = juce::jmax<std::int64_t> (0, first); i < juce::jmin<std::int64_t> (static_cast<std::int64_t> (n), last); ++i)
                g.fillEllipse (xForFrame (static_cast<double> (i)) - 1.5f, mid - sampleAt (static_cast<std::size_t> (i)) * scale - 1.5f, 3.0f, 3.0f);
        return;
    }
    for (int x = 0; x < static_cast<int> (area.getWidth()); ++x)
    {
        const auto f0 = frameAtX (area.getX() + static_cast<float> (x)), f1 = f0 + framesPerPixel;
        if (f1 < 0 || f0 >= static_cast<double> (n))
            continue;
        float lo = 0, hi = 0;
        if (framesPerPixel >= peakBlock && ! peakMin.empty())
        {
            for (auto b = static_cast<std::size_t> (juce::jmax (0.0, f0) / peakBlock); b <= static_cast<std::size_t> (juce::jmin (static_cast<double> (n - 1), f1) / peakBlock) && b < peakMin.size(); ++b)
            {
                lo = juce::jmin (lo, peakMin[b]);
                hi = juce::jmax (hi, peakMax[b]);
            }
        }
        else
        {
            for (auto i = static_cast<std::size_t> (juce::jmax (0.0, f0)); i < static_cast<std::size_t> (juce::jmin (static_cast<double> (n), f1)); ++i)
            {
                const auto v = sampleAt (i);
                lo = juce::jmin (lo, v);
                hi = juce::jmax (hi, v);
            }
        }
        g.fillRect (area.getX() + static_cast<float> (x), mid - hi * scale, 1.0f, juce::jmax (1.0f, (hi - lo) * scale));
    }
}

void ChopView::paintOverview (juce::Graphics& g, juce::Rectangle<float> area, const io::DecodedAudio& a) const
{
    hw::recess (g, area);
    const auto inner = area.reduced (4, 3);
    const auto n = static_cast<double> (a.channels.front().size());
    const auto w = juce::jmax (1, static_cast<int> (inner.getWidth()));
    const auto mid = inner.getCentreY();
    g.setColour (hw::colour::amberMid);
    for (int x = 0; x < w && ! peakMax.empty(); ++x)
    {
        const auto b0 = static_cast<std::size_t> (n * x / w / peakBlock), b1 = static_cast<std::size_t> (n * (x + 1) / w / peakBlock);
        float hi = 0;
        for (auto b = b0; b <= b1 && b < peakMax.size(); ++b)
            hi = juce::jmax (hi, juce::jmax (peakMax[b], -peakMin[b]));
        const auto h = hi * inner.getHeight() * 0.48f;
        g.fillRect (inner.getX() + static_cast<float> (x), mid - h, 1.0f, juce::jmax (1.0f, h * 2));
    }
    const auto toX = [&] (double f) { return inner.getX() + static_cast<float> (f / juce::jmax (1.0, n)) * inner.getWidth(); };
    const auto& b = owner.current();
    g.setColour (bankColour (owner.bank));
    for (const auto& m : b.marks)
        if (m)
            g.fillRect (toX (static_cast<double> (*m)) - 0.5f, inner.getY(), 1.0f, inner.getHeight());
    // The part on screen.
    const auto x0 = toX (viewStart), x1 = toX (viewStart + visibleFrames());
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.fillRect (juce::Rectangle<float> (x0, inner.getY(), juce::jmax (2.0f, x1 - x0), inner.getHeight()).getIntersection (inner));
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawRect (juce::Rectangle<float> (x0, inner.getY(), juce::jmax (2.0f, x1 - x0), inner.getHeight()).getIntersection (inner), 1.0f);
    hw::glass (g, area);
}

void ChopView::paint (juce::Graphics& g)
{
    const auto bounds = waveArea.toFloat();
    hw::recess (g, bounds);
    const auto* audio = owner.shownAudio();
    if (audio == nullptr || audio->channels.empty() || audio->channels.front().empty())
    {
        hw::glowText (g, "LOAD A TRACK (DOUBLE-CLICK ONE IN CATALOG), OR LOAD... A SOUND FILE TO CHOP", bounds, hw::vfd (22.0f), hw::colour::amberDim,
                      juce::Justification::centred);
        hw::glass (g, bounds);
        return;
    }
    const_cast<ChopView*> (this)->ensurePeaks (audio);
    const auto area = bounds.reduced (2, 6);
    const auto& b = owner.current();
    const bool chopped = ! b.empty();
    const auto colour = bankColour (owner.bank);
    g.saveState();
    g.reduceClipRegion (bounds.toNearestInt());

    // Beat grid, if this is the deck's track and it has one.
    if (const auto* record = owner.session.record(); record != nullptr && record->grid && ! record->grid->isEmpty() && (b.audio == nullptr || owner.sourceIsOnDeck (b)))
    {
        const auto& grid = *record->grid;
        const auto firstBeat = std::floor (grid.beatAt (juce::jmax (0.0, viewStart)));
        const auto lastBeat = std::ceil (grid.beatAt (viewStart + visibleFrames()));
        const auto beatPx = (grid.frameAt (firstBeat + 1) - grid.frameAt (firstBeat)) / framesPerPixel;
        if (beatPx > 5 && lastBeat - firstBeat < 4000)
            for (auto beat = firstBeat; beat <= lastBeat; beat += 1)
            {
                const auto x = xForFrame (grid.frameAt (beat));
                const bool bar = static_cast<long> (std::lround (beat)) % 4 == 0;
                g.setColour (juce::Colours::white.withAlpha (bar ? 0.16f : 0.06f));
                g.fillRect (x, area.getY(), 1.0f, area.getHeight());
            }
    }

    // The chosen pad's slice, shaded, with its end marked.
    if (const auto& m = b.marks[static_cast<std::size_t> (owner.selected)]; m)
    {
        const auto end = static_cast<double> (owner.sliceEnd (owner.bank, owner.selected).value_or (*m));
        const auto x0 = xForFrame (static_cast<double> (*m)), x1 = xForFrame (end);
        g.setColour (colour.withAlpha (0.16f));
        g.fillRect (juce::Rectangle<float> (x0, area.getY(), juce::jmax (1.0f, x1 - x0), area.getHeight()));
        g.setColour (colour.withAlpha (0.6f));
        for (float y = area.getY(); y < area.getBottom(); y += 8)
            g.fillRect (x1 - 0.5f, y, 1.0f, 4.0f);
    }

    g.setColour (chopped || b.audio != nullptr ? hw::colour::amber : hw::colour::amberMid);
    paintWave (g, area, *audio);

    // Markers: a line and a numbered tag.
    g.setFont (hw::brand (12.0f));
    for (int p = 0; p < 16; ++p)
        if (const auto& m = b.marks[static_cast<std::size_t> (p)]; m)
        {
            const auto x = xForFrame (static_cast<double> (*m));
            if (x < bounds.getX() - 30 || x > bounds.getRight())
                continue;
            const bool chosen = p == owner.selected;
            g.setColour (chosen ? juce::Colours::white : colour.brighter (0.2f));
            g.fillRect (x - (chosen ? 1.0f : 0.5f), area.getY(), chosen ? 2.0f : 1.0f, area.getHeight());
            const auto tag = juce::Rectangle<float> (x, bounds.getY() + 2, 38.0f, 18.0f);
            g.fillRoundedRectangle (tag, 2.0f);
            g.setColour (juce::Colours::black);
            g.drawText (juce::String (p + 1) + " " + juce::String::charToString (static_cast<juce::juce_wchar> (owner.padKeys[p])).toUpperCase(), tag, juce::Justification::centred, false);
        }

    // Playheads: the deck (or file preview) in red, each playing slice in green.
    bool playing = false;
    const auto ph = xForFrame (playhead (playing));
    g.setColour (juce::Colour (0xffff4a3a).withAlpha (playing ? 1.0f : 0.7f));
    g.fillRect (ph - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
    g.setColour (hw::colour::playGreen);
    for (int v = 0; v < engine::Sampler::maxVoices; ++v)
    {
        const auto [slot, at] = owner.sampler.voicePosition (v);
        if (slot >= 0 && slot / 16 == owner.bank)
            g.fillRect (xForFrame (at) - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
    }
    g.restoreState();

    if (! chopped)
        hw::glowText (g, b.audio != nullptr ? "EQUAL 16, TRANSIENTS, OR CHOP MODE AND CLICK TO PLACE PAD " + juce::String (owner.selected + 1)
                                            : "PLAY THE TRACK AND TAP PADS ON THE HITS YOU WANT, OR TRY TRANSIENTS",
                      bounds.reduced (12, 8).removeFromBottom (24), hw::vfd (20.0f), hw::colour::amber, juce::Justification::centred);
    hw::glass (g, bounds);
    paintOverview (g, overviewArea.toFloat(), *audio);
}

int ChopView::markerNear (float x) const
{
    const auto& b = owner.current();
    int best = -1;
    float bestDistance = 7.0f;
    for (int p = 0; p < 16; ++p)
        if (const auto& m = b.marks[static_cast<std::size_t> (p)]; m)
            if (const auto d = std::abs (xForFrame (static_cast<double> (*m)) - x); d < bestDistance)
            {
                best = p;
                bestDistance = d;
            }
    return best;
}

void ChopView::moveViewToOverview (float x)
{
    const auto inner = overviewArea.toFloat().reduced (4, 3);
    const auto f = static_cast<double> ((x - inner.getX()) / juce::jmax (1.0f, inner.getWidth())) * length();
    viewStart = f - visibleFrames() / 2;
    following = false;
    followAgainAt = juce::Time::getMillisecondCounter() + 2500;
    clampView();
    repaint();
}

void ChopView::mouseMove (const juce::MouseEvent& e)
{
    const bool onMarker = waveArea.contains (e.getPosition()) && (markerNear (e.position.x) >= 0 || e.position.y < waveArea.getY() + 22);
    setMouseCursor (onMarker ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void ChopView::mouseDown (const juce::MouseEvent& e)
{
    moved = false;
    drag = Drag::none;
    if (owner.shownAudio() == nullptr)
        return;
    if (overviewArea.contains (e.getPosition()))
    {
        drag = Drag::overview;
        moveViewToOverview (e.position.x);
        return;
    }
    const auto x = e.position.x;
    const auto at = static_cast<Frame> (juce::jlimit (0.0, juce::jmax (0.0, length() - 1), frameAtX (x)));
    auto& b = owner.current();

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        const auto pad = owner.selected;
        menu.addItem ("Put Pad " + juce::String (pad + 1) + " Here", [this, pad, at] {
            auto& bk = owner.current();
            if (bk.audio == nullptr && ! owner.startBankFromDeck())
                return;
            owner.setMark (pad, owner.snapped (at));
        });
        if (b.audio == nullptr || owner.sourceIsOnDeck (b))
            menu.addItem ("Move the Deck Playhead Here", [this, at] { owner.session.deck().seek (at); });
        else
            menu.addItem ("Play From Here (Space)", [this, at] {
                owner.previewCursor = at;
                owner.toggleFilePreview();
            });
        menu.addSeparator();
        menu.addItem ("Zoom to Pad " + juce::String (pad + 1), b.marks[static_cast<std::size_t> (pad)].has_value(), false, [this, pad] {
            const auto& bk = owner.current();
            showRange (static_cast<double> (*bk.marks[static_cast<std::size_t> (pad)]), static_cast<double> (owner.sliceEnd (owner.bank, pad).value_or (0)));
        });
        menu.addItem ("Show Whole Sample", [this] { showWhole(); });
        menu.addSeparator();
        menu.addItem ("Clear Pad " + juce::String (pad + 1), b.marks[static_cast<std::size_t> (pad)].has_value(), false, [this, pad] { owner.clearPad (pad); });
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
        return;
    }

    dragStartX = x;
    dragStartView = viewStart;
    if (const auto p = markerNear (x); p >= 0)
    {
        owner.rememberForUndo();
        drag = Drag::marker;
        dragMarker = p;
        owner.selectPad (p);
        return;
    }
    const bool chosenEmpty = ! b.marks[static_cast<std::size_t> (owner.selected)].has_value();
    if (owner.chopMode || (chosenEmpty && b.audio != nullptr && ! owner.sourceIsOnDeck (b)))
    {
        // CHOP: put the chosen pad's marker here (and keep dragging it).
        if (b.audio == nullptr && ! owner.startBankFromDeck())
            return;
        owner.setMark (owner.selected, owner.snapped (at));
        drag = Drag::marker;
        dragMarker = owner.selected;
        return;
    }
    // PLAY: a click in a slice plays it; dragging the background moves the view.
    drag = Drag::pan;
    for (int p = 0; p < 16; ++p)
        if (const auto& m = b.marks[static_cast<std::size_t> (p)]; m && at >= *m && at < owner.sliceEnd (owner.bank, p).value_or (*m))
        {
            owner.selectPad (p);
            owner.sampler.trigger (owner.slotOf (p));
            moved = true;  // played: don't also move the playhead on release
            break;
        }
}

void ChopView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::overview)
        return moveViewToOverview (e.position.x);
    if (drag == Drag::marker && dragMarker >= 0)
    {
        auto& m = owner.current().marks[static_cast<std::size_t> (dragMarker)];
        const auto at = static_cast<Frame> (juce::jlimit (0.0, juce::jmax (0.0, length() - 1), frameAtX (e.position.x)));
        m = owner.snapped (at);
        owner.updateRegions (owner.bank);
        owner.say ("PAD " + juce::String (dragMarker + 1) + " " + owner.describe (dragMarker).fromFirstOccurrenceOf ("]", false, false).trim());
        // Near the edges, scroll.
        if (e.position.x > waveArea.getRight() - 20)
            viewStart += visibleFrames() * 0.02;
        if (e.position.x < waveArea.getX() + 20)
            viewStart -= visibleFrames() * 0.02;
        clampView();
        repaint();
        return;
    }
    if (drag == Drag::pan && std::abs (e.position.x - dragStartX) > 3)
    {
        moved = true;
        following = false;
        followAgainAt = juce::Time::getMillisecondCounter() + 2500;
        viewStart = dragStartView - static_cast<double> (e.position.x - dragStartX) * framesPerPixel;
        clampView();
        repaint();
    }
}

void ChopView::mouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::marker)
    {
        owner.updateRegions (owner.bank);
        owner.saveKit();
    }
    else if (drag == Drag::pan && ! moved && waveArea.contains (e.getPosition()))
    {
        // A plain click outside the slices moves the playhead there (the deck, or where Space plays a file from).
        const auto at = static_cast<Frame> (juce::jlimit (0.0, juce::jmax (0.0, length() - 1), frameAtX (e.position.x)));
        const auto& b = owner.current();
        if (b.audio == nullptr || owner.sourceIsOnDeck (b))
        {
            if (owner.session.record() != nullptr)
                owner.session.deck().seek (at);
        }
        else
        {
            owner.previewCursor = at;
            auto& preview = owner.session.output().chopPreview();
            if (preview.isPlaying())
                preview.play (static_cast<double> (at) / juce::jmax (1.0, length()));
        }
    }
    drag = Drag::none;
    dragMarker = -1;
    repaint();
}

void ChopView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto at = static_cast<Frame> (frameAtX (e.position.x));
    const auto& b = owner.current();
    for (int p = 0; p < 16; ++p)
        if (const auto& m = b.marks[static_cast<std::size_t> (p)]; m && at >= *m && at < owner.sliceEnd (owner.bank, p).value_or (*m))
            return showRange (static_cast<double> (*m), static_cast<double> (*owner.sliceEnd (owner.bank, p)));
}

void ChopView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
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
    followAgainAt = juce::Time::getMillisecondCounter() + 2500;
    viewStart -= static_cast<double> (delta) * visibleFrames() * 0.4;
    zoomAnchorFrame = frameAtX (static_cast<float> (zoomAnchorX));
    clampView();
    repaint();
}

void ChopView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (scaleFactor > 0)
        zoomBy (1.0 / static_cast<double> (scaleFactor), e.position.x);
}

} // namespace dss1::ui
