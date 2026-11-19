#include "SamplerSection.h"
#include "Hardware.h"
#include "Settings.h"
#include "Theme.h"

#include "core/SeratoTags.h"

#include <map>
#include <thread>

namespace dss1::ui
{

namespace
{
    constexpr double lengths[] { 0.25, 0.5, 1, 2, 4, 8, 16 };
    const char* const lengthNames[] { "1/4", "1/2", "1", "2", "4", "8", "16" };
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
        return juce::String::charToString (static_cast<juce::juce_wchar> (SamplerSection::padKeys[pad])).toUpperCase();
    }
}

bool SamplerSection::Bank::empty() const noexcept
{
    return std::none_of (marks.begin(), marks.end(), [] (const auto& m) { return m.has_value(); });
}

//==============================================================================================================
/// The chopped sample with its slice markers. Click a slice to hear it; drag a marker to move it; in CHOP mode
/// (or for an empty chosen pad) a click puts the chosen pad's marker there.
class SamplerSection::SliceView : public juce::Component
{
public:
    explicit SliceView (SamplerSection& s) : owner (s) {}

    /// The part of the sample on screen: around the slices, or around the playhead before there are any.
    std::pair<double, double> range() const
    {
        if (dragging >= 0)
            return frozen;
        const auto& b = owner.current();
        const auto* audio = shownAudio();
        if (audio == nullptr || audio->channels.empty())
            return { 0, 1 };
        const auto total = static_cast<double> (audio->channels.front().size());
        const auto rate = audio->info.sampleRate;
        if (b.empty())
        {
            if (b.audio != nullptr)  // a sound file waiting to be chopped: all of it
                return { 0, total };
            const auto at = owner.session.deck().position();
            return { juce::jlimit (0.0, total, at - 4 * rate), juce::jlimit (0.0, total, at + 8 * rate) };
        }
        double from = total, to = 0;
        for (int p = 0; p < 16; ++p)
            if (b.marks[static_cast<std::size_t> (p)])
            {
                from = juce::jmin (from, static_cast<double> (*b.marks[static_cast<std::size_t> (p)]));
                to = juce::jmax (to, static_cast<double> (owner.sliceEnd (owner.bank, p).value_or (0)));
            }
        const auto span = juce::jmax (to - from, 2.0 * rate);
        return { juce::jmax (0.0, from - span * 0.08), juce::jmin (total, to + span * 0.08) };
    }

    const io::DecodedAudio* shownAudio() const
    {
        if (const auto& b = owner.current(); b.audio != nullptr)
            return b.audio.get();
        return std::as_const (owner.session).audio().get();
    }

    double frameAtX (float x) const
    {
        const auto [from, to] = range();
        const auto area = getLocalBounds().toFloat().reduced (6, 4);
        return from + (static_cast<double> (x - area.getX()) / juce::jmax (1.0f, area.getWidth())) * (to - from);
    }

    float xOfFrame (double frame) const
    {
        const auto [from, to] = range();
        const auto area = getLocalBounds().toFloat().reduced (6, 4);
        return area.getX() + static_cast<float> ((frame - from) / juce::jmax (1.0, to - from)) * area.getWidth();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        hw::recess (g, bounds);
        const auto area = bounds.reduced (6, 4);
        const auto* audio = shownAudio();
        const auto& b = owner.current();
        if (audio == nullptr || audio->channels.empty() || audio->channels.front().empty())
        {
            hw::glowText (g, "LOAD A TRACK, THEN TAP PADS TO CHOP IT", area, hw::vfd (19.0f), hw::colour::amberDim, juce::Justification::centred);
            hw::glass (g, bounds);
            return;
        }
        const auto [from, to] = range();
        const bool chopped = ! b.empty();

        // The waveform (cached: it only changes when the view moves).
        const auto w = juce::jmax (1, static_cast<int> (area.getWidth()));
        if (audio != cachedAudio || std::abs (from - cachedFrom) > 0.5 || std::abs (to - cachedTo) > 0.5 || w != static_cast<int> (peaks.size()))
        {
            cachedAudio = audio;
            cachedFrom = from;
            cachedTo = to;
            peaks.assign (static_cast<std::size_t> (w), 0.0f);
            const auto& ch = audio->channels;
            const auto total = ch.front().size();
            const auto perColumn = (to - from) / w;
            const auto stride = juce::jmax<std::size_t> (1, static_cast<std::size_t> (perColumn / 64));  // enough to see the shape
            for (int x = 0; x < w; ++x)
            {
                auto i = static_cast<std::size_t> (from + x * perColumn);
                const auto e = juce::jmin (total, static_cast<std::size_t> (from + (x + 1) * perColumn) + 1);
                float peak = 0;
                for (; i < e; i += stride)
                    for (const auto& c : ch)
                        peak = juce::jmax (peak, std::abs (c[i]));
                peaks[static_cast<std::size_t> (x)] = peak;
            }
        }
        const auto mid = area.getCentreY();
        const auto colour = bankColour (owner.bank);

        // The chosen pad's slice, shaded.
        if (const auto& m = b.marks[static_cast<std::size_t> (owner.selected)]; m)
        {
            const auto x0 = xOfFrame (static_cast<double> (*m)), x1 = xOfFrame (static_cast<double> (owner.sliceEnd (owner.bank, owner.selected).value_or (*m)));
            g.setColour (colour.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float> (x0, area.getY(), juce::jmax (1.0f, x1 - x0), area.getHeight()).getIntersection (area));
        }
        g.setColour (chopped ? hw::colour::amber : hw::colour::amberMid);
        for (int x = 0; x < w; ++x)
        {
            const auto h = juce::jmin (1.0f, peaks[static_cast<std::size_t> (x)]) * area.getHeight() * 0.42f;
            g.fillRect (area.getX() + static_cast<float> (x), mid - h, 1.0f, juce::jmax (1.0f, h * 2));
        }

        // Markers: a line and the pad number.
        g.setFont (hw::brand (10.0f));
        for (int p = 0; p < 16; ++p)
            if (const auto& m = b.marks[static_cast<std::size_t> (p)]; m)
            {
                const auto x = xOfFrame (static_cast<double> (*m));
                if (x < area.getX() - 1 || x > area.getRight() + 1)
                    continue;
                const bool chosen = p == owner.selected;
                g.setColour (chosen ? juce::Colours::white : colour.brighter (0.2f));
                g.fillRect (x - (chosen ? 1.0f : 0.5f), area.getY(), chosen ? 2.0f : 1.0f, area.getHeight());
                const auto tag = juce::Rectangle<float> (x, area.getY(), 18.0f, 13.0f);
                g.fillRect (tag);
                g.setColour (juce::Colours::black);
                g.drawText (juce::String (p + 1), tag, juce::Justification::centred, false);
            }

        // Where the deck is (if this is the track on the deck), and where each slice is playing.
        if (b.audio == nullptr || owner.sourceIsOnDeck (b))
        {
            const auto x = xOfFrame (owner.session.deck().position());
            if (x >= area.getX() && x <= area.getRight())
            {
                g.setColour (juce::Colour (0xffff4a3a));
                g.fillRect (x - 1.0f, area.getY(), 2.0f, area.getHeight());
            }
        }
        g.setColour (hw::colour::playGreen);
        for (int v = 0; v < engine::Sampler::maxVoices; ++v)
        {
            const auto [slot, at] = owner.sampler.voicePosition (v);
            if (slot / 16 != owner.bank || slot < 0)
                continue;
            const auto x = xOfFrame (at);
            if (x >= area.getX() && x <= area.getRight())
                g.fillRect (x - 1.0f, area.getY(), 2.0f, area.getHeight());
        }
        if (! chopped)
            hw::glowText (g, b.audio != nullptr ? "AUTO 16, OR CLICK TO PLACE PAD " + juce::String (owner.selected + 1)
                                                : "PLAY THE TRACK AND TAP PADS ON THE BEATS YOU WANT",
                          area.withTrimmedTop (area.getHeight() - 20), hw::vfd (17.0f), hw::colour::amber, juce::Justification::centred);
        hw::glass (g, bounds);
    }

    int markerNear (float x) const
    {
        const auto& b = owner.current();
        int best = -1;
        float bestDistance = 6.0f;
        for (int p = 0; p < 16; ++p)
            if (const auto& m = b.marks[static_cast<std::size_t> (p)]; m)
                if (const auto d = std::abs (xOfFrame (static_cast<double> (*m)) - x); d < bestDistance)
                {
                    best = p;
                    bestDistance = d;
                }
        return best;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (markerNear (static_cast<float> (e.x)) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& b = owner.current();
        if (shownAudio() == nullptr)
            return;
        const auto x = static_cast<float> (e.x);
        if (const auto p = markerNear (x); p >= 0)
        {
            // Grab a marker to move it.
            frozen = range();
            dragging = p;
            owner.selected = p;
            owner.refreshPads();
            owner.repaint();
            return;
        }
        const auto at = static_cast<Frame> (frameAtX (x));
        const bool chosenEmpty = ! b.marks[static_cast<std::size_t> (owner.selected)];
        if (owner.chopMode || chosenEmpty)
        {
            if (b.audio == nullptr && ! owner.startBankFromDeck())
                return;
            owner.setMark (owner.selected, at);
            return;
        }
        // PLAY: click a slice to hear it.
        for (int p = 0; p < 16; ++p)
            if (const auto& m = b.marks[static_cast<std::size_t> (p)]; m && at >= *m && at < owner.sliceEnd (owner.bank, p).value_or (*m))
            {
                owner.selected = p;
                owner.sampler.trigger (owner.slotOf (p));
                owner.refreshPads();
                owner.repaint();
                break;
            }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0)
            return;
        auto* audio = owner.current().audio.get();
        if (audio == nullptr)
            return;
        const auto total = static_cast<Frame> (audio->channels.front().size());
        auto& m = owner.current().marks[static_cast<std::size_t> (dragging)];
        m = juce::jlimit<Frame> (0, total - 1, static_cast<Frame> (frameAtX (static_cast<float> (e.x))));
        owner.updateRegions (owner.bank);
        owner.say ("PAD " + juce::String (dragging + 1) + " AT " + clock (static_cast<double> (*m) / audio->info.sampleRate));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging >= 0)
        {
            dragging = -1;
            owner.saveKit();
            owner.repaint();
        }
    }

private:
    SamplerSection& owner;
    int dragging = -1;
    std::pair<double, double> frozen { 0, 1 };
    mutable const io::DecodedAudio* cachedAudio = nullptr;
    mutable double cachedFrom = -1, cachedTo = -1;
    mutable std::vector<float> peaks;
};

//==============================================================================================================
SamplerSection::SamplerSection (Session& s) : session (s), sampler (s.output().sampler())
{
    settings = &appSettings();

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
        pad.addMouseListener (this, false);  // pads are played from mouseDown/mouseUp, so HOLD works with the mouse
        addAndMakeVisible (pad);
    }

    view = std::make_unique<SliceView> (*this);
    addAndMakeVisible (*view);

    sliceKnob.setRange (0, 6, 1);
    sliceKnob.setValue (settings->getIntValue ("sliceBeats", 2), juce::dontSendNotification);
    sliceKnob.setTooltip ("Beats per slice for AUTO 16, and how long the last slice plays");
    hwScale (sliceKnob, "1/4|1/2|1|2|4|8|16");
    sliceKnob.onValueChange = [this] {
        settings->setValue ("sliceBeats", static_cast<int> (sliceKnob.getValue()));
        for (int b = 0; b < 8; ++b)
            updateRegions (b);
        repaint();
    };
    addAndMakeVisible (sliceKnob);

    hold = settings->getBoolValue ("padHold", false);
    sampler.setChoke (settings->getBoolValue ("padChoke", true));

    for (auto* k : { &chopKey, &playKey })
        hwStyle (*k, KeyStyle::cream);
    chopKey.setTooltip ("CHOP: pads move their mark to the playhead; clicking the waveform moves the chosen pad's mark");
    playKey.setTooltip ("PLAY: pads play their slices (empty pads still mark)");
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

    hwStyle (autoKey, KeyStyle::cue);
    autoKey.setTooltip ("Chop into 16 equal slices: the loop that's playing, or 16 x SLICE beats from the playhead (a sound file: all of it)");
    autoKey.onClick = [this] { autoChop(); };
    hwStyle (saveKey, KeyStyle::cue);
    saveKey.setTooltip ("Write this bank's slices as WAVs into the samples folder (Bank A … Bank H)");
    saveKey.onClick = [this] { saveBank(); };
    loadKey.setTooltip ("Chop a sound file in this bank instead of the track on the deck");
    loadKey.onClick = [this] { loadFile(); };
    clearKey.setTooltip ("Clear the chosen pad's mark");
    clearKey.onClick = [this] { clearPad (selected); };
    clearBankKey.setTooltip ("Clear every mark in this bank, to chop something else");
    clearBankKey.onClick = [this] { clearBank(); };
    stopKey.onClick = [this] { sampler.stopAll(); };
    hwStyle (folderKey, KeyStyle::cream);
    folderKey.setTooltip ("Choose the folder SAVE BANK writes to");
    folderKey.onClick = [this] { chooseFolder(); };
    for (auto* k : { &chopKey, &playKey, &holdKey, &chokeKey, &autoKey, &saveKey, &loadKey, &clearKey, &clearBankKey, &stopKey, &folderKey })
        addAndMakeVisible (k);

    message = "PRESS A PAD (OR ITS KEY) TO MARK A SLICE";
    setMode (false);
    selectBank (0);
    restoreKit();
    startTimerHz (30);
}

SamplerSection::~SamplerSection() = default;

juce::File SamplerSection::samplesFolder() const
{
    const auto chosen = settings->getValue ("samplesFolder");
    if (chosen.isNotEmpty() && juce::File::isAbsolutePath (chosen))
        return juce::File (chosen);
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DSS1 Samples");
}

juce::String SamplerSection::slotName (int slot) const
{
    return juce::String::charToString (static_cast<juce::juce_wchar> ('A' + slot / 16)) + juce::String (slot % 16 + 1).paddedLeft ('0', 2);
}

bool SamplerSection::sourceIsOnDeck (const Bank& b) const
{
    const auto* record = session.record();
    return record != nullptr && b.source == juce::File (juce::String::fromUTF8 (record->identity.path.c_str()));
}

juce::String SamplerSection::describe (int pad) const
{
    const auto& b = current();
    const auto head = slotName (slotOf (pad)) + " [" + keyName (pad) + "]  ";
    const auto& m = b.marks[static_cast<std::size_t> (pad)];
    if (! m || b.audio == nullptr)
        return head + "EMPTY: PRESS TO MARK";
    const auto rate = b.audio->info.sampleRate;
    const auto end = sliceEnd (bank, pad).value_or (*m);
    return head + clock (static_cast<double> (*m) / rate) + "  " + juce::String (static_cast<double> (end - *m) / rate, 2) + " S";
}

void SamplerSection::say (const juce::String& text)
{
    message = text;
    messageTime = juce::Time::getMillisecondCounter();
    repaint (readoutArea);
    view->repaint();
}

void SamplerSection::setMode (bool chop)
{
    chopMode = chop;
    chopKey.setToggleState (chop, juce::dontSendNotification);
    playKey.setToggleState (! chop, juce::dontSendNotification);
    say (chop ? "CHOP: PADS MOVE TO THE PLAYHEAD, CLICK TO PLACE" : "PLAY: PADS PLAY THEIR SLICES");
}

void SamplerSection::selectBank (int b)
{
    bank = juce::jlimit (0, 7, b);
    for (int i = 0; i < 8; ++i)
        bankKeys[static_cast<std::size_t> (i)].setToggleState (i == bank, juce::dontSendNotification);
    const auto& cb = current();
    if (cb.audio != nullptr)
        say ("BANK " + slotName (slotOf (0)).substring (0, 1) + ": " + cb.source.getFileNameWithoutExtension().toUpperCase());
    refreshPads();
    repaint();
}

void SamplerSection::refreshPads()
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

void SamplerSection::timerCallback()
{
    bool changed = false;
    for (int p = 0; p < 16; ++p)
        changed |= sampler.isPlaying (slotOf (p)) != wasPlaying[static_cast<std::size_t> (p)];
    if (changed)
        refreshPads();
    sampler.collectGarbage();
    view->repaint();  // playheads
    if (message.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime > 2500)
    {
        message.clear();
        repaint (readoutArea);
    }
}

//--- Pads ------------------------------------------------------------------------------------------------------

bool SamplerSection::handleKey (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown())
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

void SamplerSection::keysChanged()
{
    for (int p = 0; p < 16; ++p)
        if (held[static_cast<std::size_t> (p)] && ! juce::KeyPress::isKeyCurrentlyDown (padKeys[p])
            && ! juce::KeyPress::isKeyCurrentlyDown (static_cast<int> (juce::CharacterFunctions::toUpperCase (static_cast<juce::juce_wchar> (padKeys[p])))))
        {
            held[static_cast<std::size_t> (p)] = false;
            padUp (p);
        }
}

void SamplerSection::mouseDown (const juce::MouseEvent& e)
{
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

void SamplerSection::mouseUp (const juce::MouseEvent&)
{
    if (mouseHeldPad >= 0)
        padUp (std::exchange (mouseHeldPad, -1));
}

void SamplerSection::padDown (int pad)
{
    selected = pad;
    const auto& b = current();
    const bool marked = b.marks[static_cast<std::size_t> (pad)].has_value() && b.audio != nullptr;
    if (! marked || chopMode)
        markAtPlayhead (pad);
    else
    {
        sampler.trigger (slotOf (pad));
        say (describe (pad));
    }
    refreshPads();
    repaint();
}

void SamplerSection::padUp (int pad)
{
    if (hold)
        sampler.release (slotOf (pad));
}

void SamplerSection::showPadMenu (int pad)
{
    selected = pad;
    refreshPads();
    repaint();
    const bool marked = current().marks[static_cast<std::size_t> (pad)].has_value();
    juce::PopupMenu menu;
    menu.addSectionHeader ("PAD " + slotName (slotOf (pad)) + "  [" + keyName (pad) + "]");
    menu.addItem (marked ? "Move Mark to the Playhead" : "Mark at the Playhead", [this, pad] { markAtPlayhead (pad); });
    menu.addItem ("Play", marked, false, [this, pad] { sampler.trigger (slotOf (pad)); });
    menu.addSeparator();
    menu.addItem ("Clear Pad", marked, false, [this, pad] { clearPad (pad); });
    menu.addItem ("Clear Bank " + slotName (slotOf (pad)).substring (0, 1), ! current().empty(), false, [this] { clearBank(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

//--- Chopping --------------------------------------------------------------------------------------------------

bool SamplerSection::startBankFromDeck()
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
             + ": USE ANOTHER BANK");
        return false;
    }
    if (b.audio == nullptr || b.source != track)
    {
        // Slices of one track in any bank share one copy of it.
        std::shared_ptr<const io::DecodedAudio> audio = loaded;
        for (const auto& other : bankData)
            if (other.audio != nullptr && other.source == track)
                audio = other.audio;
        b = {};
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

void SamplerSection::markAtPlayhead (int pad)
{
    auto& b = current();
    if (b.audio != nullptr && ! sourceIsOnDeck (b))
    {
        if (b.empty() || session.record() == nullptr)
        {
            say ("CLICK THE WAVEFORM TO PLACE PAD " + juce::String (pad + 1) + ", OR AUTO 16");
            return;
        }
        // A sound file's bank can't take marks from the deck.
        say ("BANK CHOPS A FILE: CLICK THE WAVEFORM TO PLACE PAD " + juce::String (pad + 1));
        return;
    }
    if (! startBankFromDeck())
        return;
    setMark (pad, session.snap (static_cast<Frame> (session.deck().position())));
}

void SamplerSection::setMark (int pad, Frame at)
{
    auto& b = current();
    if (b.audio == nullptr)
        return;
    const auto total = static_cast<Frame> (b.audio->channels.front().size());
    b.marks[static_cast<std::size_t> (pad)] = juce::jlimit<Frame> (0, juce::jmax<Frame> (0, total - 1), at);
    selected = pad;
    for (int p = 0; p < 16; ++p)
        if (b.marks[static_cast<std::size_t> (p)])
            sampler.setSample (slotOf (p), b.audio);
    updateRegions (bank);
    saveKit();
    say ("PAD " + juce::String (pad + 1) + " [" + keyName (pad) + "] AT " + clock (static_cast<double> (*b.marks[static_cast<std::size_t> (pad)]) / b.audio->info.sampleRate));
    refreshPads();
    repaint();
}

std::optional<Frame> SamplerSection::sliceEnd (int bankIndex, int pad) const
{
    const auto& b = bankData[static_cast<std::size_t> (bankIndex)];
    const auto& m = b.marks[static_cast<std::size_t> (pad)];
    if (! m || b.audio == nullptr || b.audio->channels.empty())
        return std::nullopt;
    const auto total = static_cast<Frame> (b.audio->channels.front().size());
    // To the next mark …
    std::optional<Frame> next;
    for (const auto& other : b.marks)
        if (other && *other > *m)
            next = juce::jmin (next.value_or (*other), *other);
    if (next)
        return *next;
    // … or, for the last one, to where AUTO 16 ended, or SLICE beats.
    if (b.chopEnd && *b.chopEnd > *m)
        return juce::jmin (total, *b.chopEnd);
    const auto perBeat = b.framesPerBeat > 0 ? b.framesPerBeat : b.audio->info.sampleRate * 0.5;
    const auto beats = lengths[juce::jlimit (0, 6, static_cast<int> (sliceKnob.getValue()))];
    return juce::jmin (total, *m + static_cast<Frame> (beats * perBeat));
}

void SamplerSection::updateRegions (int bankIndex)
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

void SamplerSection::clearPad (int pad)
{
    current().marks[static_cast<std::size_t> (pad)].reset();
    sampler.setSample (slotOf (pad), nullptr);
    updateRegions (bank);
    saveKit();
    say ("CLEARED PAD " + juce::String (pad + 1) + ": NEXT PRESS MARKS IT");
    refreshPads();
    repaint();
}

void SamplerSection::clearBank()
{
    sampler.stopAll();
    current() = {};
    for (int p = 0; p < 16; ++p)
        sampler.setSample (slotOf (p), nullptr);
    saveKit();
    say ("CLEARED BANK " + slotName (slotOf (0)).substring (0, 1) + ": READY TO CHOP");
    refreshPads();
    repaint();
}

void SamplerSection::autoChop()
{
    const auto chop = [this] {
        auto& b = current();
        Frame from = 0, to = 0;
        if (b.audio != nullptr && ! sourceIsOnDeck (b))
        {
            to = static_cast<Frame> (b.audio->channels.front().size());  // a sound file: all of it
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
            else
            {
                const auto* record = session.record();
                from = static_cast<Frame> (deck.position());
                if (record != nullptr && record->grid && ! record->grid->isEmpty())
                    from = static_cast<Frame> (record->grid->frameAt (std::round (record->grid->beatAt (static_cast<double> (from)))));
                const auto beats = lengths[juce::jlimit (0, 6, static_cast<int> (sliceKnob.getValue()))];
                to = from + static_cast<Frame> (16 * beats * current().framesPerBeat);
            }
            from = juce::jlimit<Frame> (0, total - 1, from);
            to = juce::jlimit<Frame> (from + 1, total, to);
        }
        auto& cb = current();
        for (int p = 0; p < 16; ++p)
        {
            cb.marks[static_cast<std::size_t> (p)] = from + (to - from) * p / 16;
            sampler.setSample (slotOf (p), cb.audio);
        }
        cb.chopEnd = to;
        updateRegions (bank);
        saveKit();
        selected = 0;
        say ("CHOPPED INTO 16: " + juce::String (static_cast<double> (to - from) / cb.audio->info.sampleRate / 16.0, 2) + " S EACH");
        refreshPads();
        repaint();
    };
    if (current().empty())
        return chop();
    const auto options = juce::MessageBoxOptions()
                             .withIconType (juce::MessageBoxIconType::QuestionIcon)
                             .withTitle ("Chop bank " + slotName (slotOf (0)).substring (0, 1) + " into 16 again?")
                             .withMessage ("Its marks are replaced by 16 equal slices.")
                             .withButton ("Chop", 1)
                             .withButton ("Cancel", 0)
                             .withAssociatedComponent (this);
    messageBox = juce::AlertWindow::showScopedAsync (options, [weak = juce::WeakReference<SamplerSection> (this), chop] (int result) {
        if (result == 1 && weak != nullptr)
            chop();
    });
}

void SamplerSection::loadFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Chop a sound file in bank " + slotName (slotOf (0)).substring (0, 1), samplesFolder(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, target = bank] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (! file.existsAsFile())
                                  return;
                              say ("LOADING " + file.getFileName().toUpperCase() + "...");
                              std::thread ([weak = juce::WeakReference<SamplerSection> (this), target, file] {
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
                                      {
                                          self->say ("COULDN'T OPEN IT: " + problem.toUpperCase());
                                          return;
                                      }
                                      self->sampler.stopAll();
                                      auto& b = self->bankData[static_cast<std::size_t> (target)];
                                      b = {};
                                      b.source = file;
                                      b.audio = audio;
                                      b.framesPerBeat = audio->info.sampleRate * 0.5;
                                      for (int p = 0; p < 16; ++p)
                                          self->sampler.setSample (target * 16 + p, nullptr);
                                      self->saveKit();
                                      self->selectBank (target);
                                      self->say ("CHOPPING " + file.getFileNameWithoutExtension().toUpperCase() + ": AUTO 16 OR CLICK THE WAVEFORM");
                                  });
                              }).detach();
                          });
}

void SamplerSection::saveBank()
{
    const auto& b = current();
    const auto letter = slotName (slotOf (0)).substring (0, 1);
    const auto folder = samplesFolder().getChildFile ("Bank " + letter);
    int written = 0;
    juce::String problem;
    for (int p = 0; p < 16; ++p)
    {
        const auto& m = b.marks[static_cast<std::size_t> (p)];
        if (! m || b.audio == nullptr)
            continue;
        const auto& source = *b.audio;
        const auto from = static_cast<std::size_t> (*m);
        const auto to = static_cast<std::size_t> (juce::jmax (*m + 1, sliceEnd (bank, p).value_or (*m + 1)));
        io::DecodedAudio slice;
        slice.info = source.info;
        for (const auto& ch : source.channels)
            slice.channels.emplace_back (ch.begin() + static_cast<std::ptrdiff_t> (from), ch.begin() + static_cast<std::ptrdiff_t> (to));
        slice.info.lengthFrames = static_cast<decltype (slice.info.lengthFrames)> (to - from);
        // Short fades, like the pad plays with.
        const auto fade = std::min<std::size_t> (static_cast<std::size_t> (source.info.sampleRate * 0.002), (to - from) / 4);
        for (auto& ch : slice.channels)
            for (std::size_t i = 0; i < fade; ++i)
            {
                const auto g = static_cast<float> (i) / static_cast<float> (fade);
                ch[i] *= g;
                ch[ch.size() - 1 - i] *= g;
            }
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

void SamplerSection::saveKit()
{
    // One line per bank: source <TAB> frames per beat <TAB> chop end (-1) <TAB> 16 marks, comma-separated (blank = none).
    juce::StringArray lines;
    for (const auto& b : bankData)
    {
        if (b.audio == nullptr)
        {
            lines.add ({});
            continue;
        }
        juce::StringArray marks;
        for (const auto& m : b.marks)
            marks.add (m ? juce::String (*m) : juce::String());
        lines.add (b.source.getFullPathName() + "\t" + juce::String (b.framesPerBeat) + "\t" + juce::String (b.chopEnd.value_or (-1)) + "\t"
                   + marks.joinIntoString (","));
    }
    settings->setValue ("sliceKit", lines.joinIntoString ("\n"));
}

void SamplerSection::restoreKit()
{
    const auto lines = juce::StringArray::fromLines (settings->getValue ("sliceKit"));
    struct Entry
    {
        int bank;
        Bank data;
    };
    std::vector<Entry> entries;
    for (int i = 0; i < juce::jmin (8, lines.size()); ++i)
    {
        const auto parts = juce::StringArray::fromTokens (lines[i], "\t", {});
        if (parts.size() < 4 || ! juce::File::isAbsolutePath (parts[0]) || ! juce::File (parts[0]).existsAsFile())
            continue;
        Bank b;
        b.source = juce::File (parts[0]);
        b.framesPerBeat = parts[1].getDoubleValue();
        if (const auto e = parts[2].getLargeIntValue(); e >= 0)
            b.chopEnd = e;
        const auto marks = juce::StringArray::fromTokens (parts[3], ",", {});
        for (int p = 0; p < juce::jmin (16, marks.size()); ++p)
            if (marks[p].isNotEmpty())
                b.marks[static_cast<std::size_t> (p)] = marks[p].getLargeIntValue();
        entries.push_back ({ i, std::move (b) });
    }
    if (entries.empty())
        return;
    // Decode each file once (banks chopping one track share it), off the message thread.
    std::thread ([weak = juce::WeakReference<SamplerSection> (this), entries]() mutable {
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
                    if (b.marks[static_cast<std::size_t> (p)])
                        self->sampler.setSample (e.bank * 16 + p, b.audio);
                self->updateRegions (e.bank);
            }
            self->refreshPads();
            self->repaint();
        });
    }).detach();
}

void SamplerSection::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the folder your samples are saved to", samplesFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc) {
                              const auto dir = fc.getResult();
                              if (! dir.isDirectory())
                                  return;
                              settings->setValue ("samplesFolder", dir.getFullPathName());
                              say ("SAVE BANK WRITES TO " + dir.getFileName().toUpperCase());
                              repaint();
                          });
}

//--- Drawing ---------------------------------------------------------------------------------------------------

void SamplerSection::paint (juce::Graphics& g)
{
    hw::silk (g, "SAMPLER", "CHOP  8 BANKS x 16 PADS", bankKeys[0].getBounds().toFloat().withWidth (300).withHeight (16).translated (0, -22));
    hw::silk (g, "SLICE", lengthNames[juce::jlimit (0, 6, static_cast<int> (sliceKnob.getValue()))] + juce::String (" BEAT"),
              sliceKnob.getBounds().toFloat().withHeight (14).translated (0, -15), juce::Justification::centred, 11.0f);

    // The chosen pad, and what just happened.
    hw::recess (g, readoutArea.toFloat());
    const bool fresh = message.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime < 2500;
    hw::glowText (g, fresh ? message : describe (selected), readoutArea.toFloat().reduced (10, 2), hw::vfd (19.0f),
                  fresh ? hw::colour::amberMid : hw::colour::amber);
    hw::glass (g, readoutArea.toFloat());

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

void SamplerSection::paintOverChildren (juce::Graphics& g)
{
    // Each pad's key, in the corner, like the legend on a drum machine.
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
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawRoundedRectangle (pads[static_cast<std::size_t> (p)].getBounds().toFloat().reduced (1.5f), 4.0f, 1.5f);
        }
    }
}

void SamplerSection::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (22);
    auto top = area.removeFromTop (30);
    area.removeFromTop (8);

    // Pads: a 4 x 4 square on the left, under the bank keys.
    const auto size = juce::jmin ((area.getHeight() - 3 * 6) / 4, (area.getWidth() * 9 / 20 - 3 * 6) / 4);
    const auto padArea = area.removeFromLeft (size * 4 + 18).withHeight (size * 4 + 18);
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            pads[static_cast<std::size_t> (padAt (row, col))].setBounds (padArea.getX() + col * (size + 6), padArea.getY() + row * (size + 6), size, size);
    area.removeFromLeft (16);

    auto banks = top.removeFromLeft (padArea.getWidth());
    const auto bw = (banks.getWidth() - 7 * 3) / 8;
    for (auto& k : bankKeys)
    {
        k.setBounds (banks.removeFromLeft (bw));
        banks.removeFromLeft (3);
    }
    top.removeFromLeft (16);
    const auto mw = (top.getWidth() - 3 * 6) / 4;
    for (auto* k : { &chopKey, &playKey, &holdKey, &chokeKey })
    {
        k->setBounds (top.removeFromLeft (mw));
        top.removeFromLeft (6);
    }

    // Right: readout, the chopped waveform, the knob and keys, where samples go.
    area = area.withHeight (padArea.getHeight());
    readoutArea = area.removeFromTop (26);
    area.removeFromTop (6);
    auto folderRow = area.removeFromBottom (28);
    folderKey.setBounds (folderRow.removeFromRight (96));
    folderRow.removeFromRight (6);
    folderArea = folderRow;
    area.removeFromBottom (8);

    auto block = area.removeFromBottom (70);
    sliceKnob.setBounds (block.removeFromLeft (64).withTrimmedTop (14));
    block.removeFromLeft (10);
    const auto kw = (block.getWidth() - 2 * 6) / 3;
    auto r1 = block.removeFromTop (32);
    auto r2 = block.withTrimmedTop (6);
    for (auto* k : { &autoKey, &saveKey, &loadKey })
    {
        k->setBounds (r1.removeFromLeft (kw));
        r1.removeFromLeft (6);
    }
    for (auto* k : { &clearKey, &clearBankKey, &stopKey })
    {
        k->setBounds (r2.removeFromLeft (kw));
        r2.removeFromLeft (6);
    }
    area.removeFromBottom (8);
    view->setBounds (area);
}

} // namespace dss1::ui
