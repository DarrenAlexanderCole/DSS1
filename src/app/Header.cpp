#include "Header.h"
#include "Theme.h"

namespace dss1::ui
{

Header::Header (Session& s) : session (s)
{
    left.source = [this] { return session.outputLevel (0); };
    right.source = [this] { return session.outputLevel (1); };
    addAndMakeVisible (left);
    addAndMakeVisible (right);

    const char* names[] { "CATALOG", "PREP", "LISTEN / DESIGN", "CHOP", "DELIVER" };
    for (int i = 0; i < 5; ++i)
    {
        auto& key = stageKeys[static_cast<std::size_t> (i)];
        key.setButtonText (names[i]);
        key.setClickingTogglesState (false);
        key.onClick = [this, i] {
            setStage (i);
            if (onStage)
                onStage (i);
        };
        addAndMakeVisible (key);
    }
    setStage (0);

    startKey.setTooltip ("Back to the start (Home)");
    startKey.onClick = [this] { session.deck().seek (0); };
    backKey.setTooltip ("Beat jump back ([)");
    backKey.onClick = [this] { session.jumpBeats (-session.jumpSize); };
    forwardKey.setTooltip ("Beat jump forward (])");
    forwardKey.onClick = [this] { session.jumpBeats (session.jumpSize); };
    cueKey.setTooltip ("Paused: set the cue point here, or hold to hear it from the cue. Playing: back to the cue point and stop.");
    hwStyle (cueKey, KeyStyle::cue);
    cueKey.onStateChange = [this] {
        const bool down = cueKey.isDown();
        if (down != cueWasDown)
        {
            cueWasDown = down;
            session.cuePressed (down);
        }
    };
    playKey.setTooltip ("Play or pause (Space)");
    hwStyle (playKey, KeyStyle::play);
    playKey.onClick = [this] { session.playPressed(); };
    for (auto* k : { &startKey, &backKey, &forwardKey, &cueKey, &playKey })
        addAndMakeVisible (k);

    session.addChangeListener (this);
    startTimerHz (15);
}

Header::~Header()
{
    session.removeChangeListener (this);
}

void Header::setStage (int index)
{
    for (int i = 0; i < 5; ++i)
        stageKeys[static_cast<std::size_t> (i)].setToggleState (i == index, juce::dontSendNotification);
}

void Header::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint (displayArea.expanded (8));
}

void Header::timerCallback()
{
    const bool playing = session.deck().isPlaying() && ! session.isCuePreviewing();
    playKey.setButtonText (playing ? "PAUSE" : "PLAY");
    playKey.setToggleState (playing, juce::dontSendNotification);
    repaint (displayArea.expanded (8));
}

void Header::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto ear = static_cast<float> (hw::RackUnit::earWidth);
    hw::faceplate (g, b.reduced (ear, 0));
    hw::rackEar (g, b.withWidth (ear), 3);
    hw::rackEar (g, b.withLeft (b.getRight() - ear), 3);
    g.setColour (juce::Colours::black);
    g.fillRect (b.withTop (b.getBottom() - 1.0f));

    // Badge, pilot lamp.
    auto badge = brandArea.toFloat();
    g.setColour (hw::colour::silk);
    g.setFont (hw::brand (32.0f));
    g.drawText ("DSS1", badge.removeFromTop (42), juce::Justification::centredLeft);
    g.setColour (hw::colour::red);
    g.setFont (hw::label (16.0f));
    g.drawText ("Digital Sample Station", badge.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (hw::colour::silkDim);
    g.setFont (hw::label (11.0f));
    g.drawText ("MODEL V2  /  " + juce::String (DSS1_BUILD_LABEL).toUpperCase(), badge.removeFromTop (16), juce::Justification::centredLeft);
    const auto lamp = juce::Point<float> (badge.getX() + 14, badge.getBottom() - 16);
    g.setColour (juce::Colour (0xff9a9b9d));
    g.fillEllipse (juce::Rectangle<float> (30, 30).withCentre (lamp));
    g.setColour (juce::Colours::black);
    g.drawEllipse (juce::Rectangle<float> (30, 30).withCentre (lamp), 1.0f);
    hw::led (g, lamp, 10.0f, juce::Colour (0xffe0301b), true);
    hw::silk (g, "POWER", {}, { lamp.x + 26, lamp.y - 8, 80, 16 });

    // Silk-screen labels.
    hw::silk (g, "OUTPUT", "LEVEL", meterArea.toFloat().withHeight (16).translated (0, -20));
    hw::silk (g, "TRACK", "NOW LOADED", displayArea.toFloat().withHeight (16).translated (0, -20));
    hw::silk (g, "STAGE", "SELECT", stageArea.toFloat().withHeight (16).translated (0, -20));
    hw::silk (g, "TRANSPORT", "BEAT JUMP " + juce::String (session.jumpSize), transportArea.toFloat().withHeight (16).translated (0, -20));

    // The display.
    const auto d = displayArea.toFloat();
    hw::recess (g, d);
    auto inner = d.reduced (16, 10);
    const auto* r = session.record();
    juce::String title = "NO TRACK LOADED", artist = "DOUBLE-CLICK A TRACK IN CATALOG";
    if (r != nullptr)
    {
        title = juce::String::fromUTF8 (r->info.title.c_str()).toUpperCase();
        artist = juce::String::fromUTF8 (r->info.artist.c_str()).toUpperCase();
        if (session.state() == Session::State::loading)
            artist = "LOADING...";
        else if (session.state() == Session::State::failed)
            artist = "COULDN'T OPEN: " + session.problem().toUpperCase();
    }
    hw::glowText (g, title, inner.removeFromTop (30), hw::vfd (32.0f));
    hw::glowText (g, artist, inner.removeFromTop (22), hw::vfd (22.0f), hw::colour::amberMid);
    inner.removeFromTop (6);
    const auto bpm = session.playingBpm();
    const juce::String key = r != nullptr && ! r->info.camelot.empty()
                                 ? juce::String (r->info.camelot) + " " + juce::String (r->info.key).toUpperCase()
                                 : juce::String ("--");
    const std::pair<juce::String, juce::String> fields[] { { "BPM", bpm ? juce::String (*bpm, 2) : juce::String ("---.--") },
                                                           { "KEY", key },
                                                           { "TIME", session.timeText() },
                                                           { "BAR", session.barText() } };
    const auto w = inner.getWidth() / 4.0f;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = inner.withX (inner.getX() + w * static_cast<float> (i)).withWidth (w - 6);
        hw::glowText (g, fields[i].first, cell.removeFromTop (16), hw::vfd (17.0f), hw::colour::amberDim);
        hw::glowText (g, fields[i].second, cell.removeFromTop (32), hw::vfd (32.0f));
    }
    hw::glass (g, d);
}

void Header::resized()
{
    auto area = getLocalBounds().reduced (hw::RackUnit::earWidth, 0).reduced (30, 0);
    area.removeFromTop (28);
    auto top = area.removeFromTop (138);
    brandArea = top.removeFromLeft (190);
    top.removeFromLeft (18);
    meterArea = top.removeFromLeft (juce::jmin (440, top.getWidth() / 2));
    {
        auto m = meterArea;
        left.setBounds (m.removeFromLeft (m.getWidth() / 2 - 6));
        m.removeFromLeft (12);
        right.setBounds (m);
    }
    top.removeFromLeft (22);
    displayArea = top.reduced (4, 4);

    area.removeFromTop (30);
    auto row = area.removeFromTop (48);
    transportArea = row.removeFromRight (4 * 64 + 96 + 30);
    row.removeFromRight (24);
    const auto keyWidth = juce::jmin (122, (row.getWidth() - 4 * 6) / 5);
    stageArea = row.removeFromLeft (5 * (keyWidth + 6));
    {
        auto s = stageArea;
        for (auto& k : stageKeys)
        {
            k.setBounds (s.removeFromLeft (keyWidth));
            s.removeFromLeft (6);
        }
    }
    {
        auto t = transportArea;
        for (auto* k : { &startKey, &backKey, &forwardKey })
        {
            k->setBounds (t.removeFromLeft (58));
            t.removeFromLeft (6);
        }
        t.removeFromLeft (8);
        cueKey.setBounds (t.removeFromLeft (80));
        t.removeFromLeft (6);
        playKey.setBounds (t.removeFromLeft (96));
    }
}

} // namespace dss1::ui
