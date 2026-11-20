#include "TransportBar.h"
#include "Scope.h"
#include "Settings.h"
#include "Theme.h"

#include "core/TestExport.h"

#include <cmath>

namespace dss1::ui
{

std::vector<float> overviewPeaks (const std::vector<std::vector<float>>& channels, int columns)
{
    std::vector<float> peaks (static_cast<std::size_t> (std::max (columns, 1)), 0.0f);
    if (channels.empty() || channels.front().empty())
        return peaks;
    const auto frames = channels.front().size();
    for (std::size_t col = 0; col < peaks.size(); ++col)
    {
        const auto from = frames * col / peaks.size();
        const auto to = std::max (from + 1, frames * (col + 1) / peaks.size());
        float peak = 0;
        for (const auto& channel : channels)
            for (auto i = from; i < to && i < channel.size(); ++i)
                peak = std::max (peak, std::abs (channel[i]));
        peaks[col] = std::min (peak, 1.0f);
    }
    return peaks;
}

TransportBar::TransportBar (Session& s) : session (s), deck (s.deck())
{
    session.addChangeListener (this);
    changeListenerCallback (nullptr);

    settings = &appSettings();

    tempoSlider.setRange (-8.0, 8.0, 0.01);
    tempoSlider.setValue (0.0, juce::dontSendNotification);
    const double ranges[] { 8.0, 16.0, 50.0 };
    for (std::size_t i = 0; i < rangeKeys.size(); ++i)
    {
        auto& k = rangeKeys[i];
        const auto r = ranges[i];
        k.setButtonText (juce::String (static_cast<int> (r)) + "%");
        k.setTooltip ("Pitch fader range: " + juce::String::fromUTF8 ("\xc2\xb1") + juce::String (static_cast<int> (r)) + " %");
        k.onClick = [this, r] { setPitchRange (r); };
        addAndMakeVisible (k);
    }
    tempoSlider.setDoubleClickReturnValue (true, 0.0);
    tempoSlider.setColour (juce::Slider::thumbColourId, Colours::accent);
    tempoSlider.onValueChange = [this] {
        deck.setTempo (1.0 + tempoSlider.getValue() / 100.0);
        updateTempoLabel();
    };
    addAndMakeVisible (tempoSlider);

    hwStyle (tempoResetButton, KeyStyle::cream);
    tempoResetButton.setButtonText ("RESET");
    tempoResetButton.setTooltip ("Back to the track's own tempo (0 %)");
    tempoResetButton.onClick = [this] { tempoSlider.setValue (0.0, juce::sendNotificationSync); };
    addAndMakeVisible (tempoResetButton);

    tempoLabel.setText ("0.00 %", juce::dontSendNotification);
    tempoLabel.setJustificationType (juce::Justification::centredLeft);
    tempoLabel.setColour (juce::Label::textColourId, hw::colour::amber);
    tempoLabel.setFont (hw::vfd (26.0f));
    tempoLabel.setTooltip ("Tempo change, and the track's BPM at this tempo");
    addAndMakeVisible (tempoLabel);

    keyLockButton.setToggleState (deck.keyLock(), juce::dontSendNotification);
    keyLockButton.onClick = [this] { deck.setKeyLock (keyLockButton.getToggleState()); };
    addAndMakeVisible (keyLockButton);

    setPitchRange (settings->getDoubleValue ("pitchRange", 8.0));
    startTimerHz (30);
}

void TransportBar::setPitchRange (double percent)
{
    pitchRange = percent > 30.0 ? 50.0 : percent > 12.0 ? 16.0 : 8.0;
    settings->setValue ("pitchRange", pitchRange);
    const auto current = juce::jlimit (-pitchRange, pitchRange, tempoSlider.getValue());
    tempoSlider.setRange (-pitchRange, pitchRange, pitchRange > 20 ? 0.05 : 0.01);
    tempoSlider.setValue (current, juce::sendNotificationSync);
    const double ranges[] { 8.0, 16.0, 50.0 };
    for (std::size_t i = 0; i < rangeKeys.size(); ++i)
        rangeKeys[i].setToggleState (std::abs (ranges[i] - pitchRange) < 0.5, juce::dontSendNotification);
    repaint();
}

TransportBar::~TransportBar()
{
    session.removeChangeListener (this);
    stopTimer();
}

void TransportBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    trackTitle = session.title();
    switch (session.state())
    {
        case Session::State::empty:   trackTitle = "No track loaded";
                                       trackDetails = session.audioProblem().isNotEmpty() ? "Audio: " + session.audioProblem()
                                                                                          : juce::String ("Double-click a track to play it");
                                       break;
        case Session::State::loading: trackDetails = "Loading..."; break;
        case Session::State::failed:  trackDetails = session.problem(); break;
        case Session::State::loaded:  trackDetails = session.details(); break;
    }
    overview = session.overview();
    updateTempoLabel();
    updatePlayButton();
    repaint();
}

void TransportBar::togglePlay()
{
    if (deck.isPlaying())
        deck.pause();
    else
        deck.play();
    updatePlayButton();
}

void TransportBar::updatePlayButton() {}

void TransportBar::timerCallback()
{
    // Echo follows the track's tempo at the current pitch.
    if (const auto bpm = session.playingBpm())
        session.effects().bpm = static_cast<float> (*bpm);
    updatePlayButton();
    updateTempoLabel();
    tempoResetButton.setEnabled (std::abs (tempoSlider.getValue()) > 0.004);
    repaint (waveformArea.expanded (2).withTop (0));
}

void TransportBar::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto ear = static_cast<float> (hw::RackUnit::earWidth);
    hw::faceplate (g, b.reduced (ear, 0));
    hw::rackEar (g, b.withWidth (ear), 2);
    hw::rackEar (g, b.withLeft (b.getRight() - ear), 2);

    hw::silk (g, "OVERVIEW", "CLICK TO JUMP", waveformArea.toFloat().withHeight (16).translated (0, -22));
    hw::silk (g, "PITCH", juce::String::fromUTF8 ("\xc2\xb1") + juce::String (static_cast<int> (pitchRange)) + " %", pitchArea.toFloat().withHeight (16).translated (0, -22));
    hw::silk (g, "RANGE", {}, rangeArea.toFloat().withHeight (16).translated (0, -22));

    // The whole track: played part dim, playhead red.
    const auto area = waveformArea.toFloat();
    hw::recess (g, area);
    const auto length = deck.length();
    if (! overview.empty())
    {
        const auto played = length > 0 ? static_cast<float> (deck.position() / static_cast<double> (length)) : 0.0f;
        // Resampled to one column per pixel so the shape is smooth at any window size.
        const auto inner = area.reduced (3, 4);
        const auto columns = juce::jmax (1, static_cast<int> (inner.getWidth()));
        std::vector<scope::Span> cols (static_cast<std::size_t> (columns));
        const auto src = static_cast<double> (overview.size());
        for (int c = 0; c < columns; ++c)
        {
            const auto from = static_cast<std::size_t> (src * c / columns);
            const auto to = juce::jmax (from + 1, static_cast<std::size_t> (src * (c + 1) / columns));
            float h = 0;
            for (auto i = from; i < to && i < overview.size(); ++i)
                h = juce::jmax (h, overview[i]);
            cols[static_cast<std::size_t> (c)] = { -h, h };
        }
        scope::wave (g, inner, cols, inner.getWidth() / static_cast<float> (columns), hw::colour::amber,
                     inner.getX() + played * inner.getWidth(), 0.46f);
        g.setColour (Colours::accent);
        g.fillRect (area.getX() + played * area.getWidth() - 1.0f, area.getY(), 2.0f, area.getHeight());
    }
    else
    {
        hw::glowText (g, session.audioProblem().isNotEmpty() ? "AUDIO: " + session.audioProblem().toUpperCase() : juce::String ("NO TRACK"),
                      area.reduced (12, 0), hw::vfd (22.0f), hw::colour::amberDim);
    }
    hw::glass (g, area);

    hw::recess (g, tempoLabel.getBounds().toFloat().reduced (0, 4));
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (hw::RackUnit::earWidth, 0).reduced (30, 0);
    area.removeFromTop (34);
    area.removeFromBottom (16);
    auto right = area.removeFromRight (juce::jmin (860, area.getWidth() * 3 / 5));
    area.removeFromRight (28);
    waveformArea = area;

    keyLockButton.setBounds (right.removeFromRight (110).withSizeKeepingCentre (110, 40));
    right.removeFromRight (10);
    tempoResetButton.setBounds (right.removeFromRight (70).withSizeKeepingCentre (70, 42));
    right.removeFromRight (12);
    tempoLabel.setBounds (right.removeFromRight (200).withSizeKeepingCentre (200, 44));
    right.removeFromRight (16);
    rangeArea = right.removeFromLeft (3 * 50 + 8);
    {
        auto r = rangeArea.withSizeKeepingCentre (rangeArea.getWidth(), 40);
        for (auto& k : rangeKeys)
        {
            k.setBounds (r.removeFromLeft (50));
            r.removeFromLeft (4);
        }
    }
    right.removeFromLeft (16);
    pitchArea = right;
    tempoSlider.setBounds (right.withSizeKeepingCentre (right.getWidth(), 46));
}

void TransportBar::seekTo (int x)
{
    if (deck.length() <= 0 || waveformArea.getWidth() <= 0)
        return;
    const auto fraction = juce::jlimit (0.0, 1.0, static_cast<double> (x - waveformArea.getX()) / waveformArea.getWidth());
    deck.seek (static_cast<Frame> (fraction * static_cast<double> (deck.length())));
    repaint();
}

void TransportBar::mouseDown (const juce::MouseEvent& e)
{
    if (waveformArea.contains (e.getPosition()))
        seekTo (e.x);
}

void TransportBar::mouseDrag (const juce::MouseEvent& e)
{
    if (waveformArea.contains (e.getMouseDownPosition()))
        seekTo (e.x);
}

} // namespace dss1::ui

namespace dss1::ui
{

void TransportBar::updateTempoLabel()
{
    const auto percent = tempoSlider.getValue();
    juce::String text = (percent > 0.004 ? "+" : "") + juce::String (percent, 2) + " %";

    // The BPM you'd hear: the track's BPM, or on a grid with tempo changes the tempo where the playhead is.
    if (const auto* r = session.record())
    {
        std::optional<double> base;
        if (r->grid && ! r->grid->isEmpty() && (! r->grid->isConstantTempo() || ! r->info.bpm))
            base = r->grid->bpmAt (deck.position());
        else
            base = r->info.bpm;
        if (base)
            text << "   " << juce::String (*base * (1.0 + percent / 100.0), 2) << " BPM";
    }
    if (text != tempoLabel.getText())
        tempoLabel.setText (text, juce::dontSendNotification);
}

} // namespace dss1::ui
