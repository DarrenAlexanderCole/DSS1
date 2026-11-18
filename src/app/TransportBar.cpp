#include "TransportBar.h"
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
    playButton.onClick = [this] { togglePlay(); };
    addAndMakeVisible (playButton);

    tempoSlider.setRange (-8.0, 8.0, 0.01);
    tempoSlider.setValue (0.0, juce::dontSendNotification);
    tempoSlider.setDoubleClickReturnValue (true, 0.0);
    tempoSlider.setColour (juce::Slider::thumbColourId, Colours::accent);
    tempoSlider.onValueChange = [this] {
        deck.setTempo (1.0 + tempoSlider.getValue() / 100.0);
        updateTempoLabel();
    };
    addAndMakeVisible (tempoSlider);

    tempoResetButton.setTooltip ("Back to the track's own tempo (0 %)");
    tempoResetButton.onClick = [this] { tempoSlider.setValue (0.0, juce::sendNotificationSync); };
    addAndMakeVisible (tempoResetButton);

    tempoLabel.setText ("0.00 %", juce::dontSendNotification);
    tempoLabel.setJustificationType (juce::Justification::centredLeft);
    tempoLabel.setColour (juce::Label::textColourId, Colours::text);
    tempoLabel.setTooltip ("Tempo change, and the track's BPM at this tempo");
    addAndMakeVisible (tempoLabel);

    keyLockButton.setToggleState (deck.keyLock(), juce::dontSendNotification);
    keyLockButton.onClick = [this] { deck.setKeyLock (keyLockButton.getToggleState()); };
    addAndMakeVisible (keyLockButton);

    startTimerHz (30);
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

void TransportBar::updatePlayButton()
{
    playButton.setButtonText (deck.isPlaying() ? "Pause" : "Play");
}

void TransportBar::timerCallback()
{
    updatePlayButton();
    updateTempoLabel();
    tempoResetButton.setEnabled (std::abs (tempoSlider.getValue()) > 0.004);
    repaint (waveformArea.expanded (2).withTop (0));
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);

    // Title and time.
    auto top = getLocalBounds().reduced (16, 8).removeFromTop (22);
    const auto rate = deck.trackSampleRate();
    const auto length = deck.length();
    juce::String time;
    if (rate > 0)
        time = juce::String (formatTime (deck.position() / rate)) + " / " + juce::String (formatTime (static_cast<double> (length) / rate));
    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (time, top.removeFromRight (200), juce::Justification::centredRight);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (trackTitle, top.removeFromLeft (top.getWidth() / 2), juce::Justification::centredLeft);
    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (trackDetails, top, juce::Justification::centredLeft);

    // Waveform overview with the played part dimmed and the playhead.
    const auto area = waveformArea.toFloat();
    g.setColour (Colours::background);
    g.fillRoundedRectangle (area, 4.0f);
    if (! overview.empty())
    {
        const auto mid = area.getCentreY();
        const auto played = length > 0 ? static_cast<float> (deck.position() / static_cast<double> (length)) : 0.0f;
        const auto columns = static_cast<int> (overview.size());
        const auto columnWidth = area.getWidth() / static_cast<float> (columns);
        for (int c = 0; c < columns; ++c)
        {
            const auto h = overview[static_cast<std::size_t> (c)] * area.getHeight() * 0.48f;
            const auto x = area.getX() + static_cast<float> (c) * columnWidth;
            const bool done = static_cast<float> (c) / static_cast<float> (columns) < played;
            g.setColour (done ? Colours::dimText.withAlpha (0.6f) : Colours::text.withAlpha (0.85f));
            g.fillRect (x, mid - h, std::max (columnWidth - 0.5f, 0.5f), h * 2.0f);
        }
        g.setColour (Colours::accent);
        g.fillRect (area.getX() + played * area.getWidth() - 1.0f, area.getY(), 2.0f, area.getHeight());
    }
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (16, 8);
    area.removeFromTop (26);
    auto controls = area.removeFromBottom (30);
    area.removeFromBottom (6);
    waveformArea = area;

    playButton.setBounds (controls.removeFromLeft (90));
    controls.removeFromLeft (16);
    keyLockButton.setBounds (controls.removeFromRight (100));
    tempoLabel.setBounds (controls.removeFromRight (170));
    controls.removeFromRight (4);
    tempoResetButton.setBounds (controls.removeFromRight (60).withSizeKeepingCentre (60, 26));
    controls.removeFromRight (8);
    tempoSlider.setBounds (controls.removeFromRight (std::min (320, controls.getWidth())));
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
            text << "    " << juce::String (*base * (1.0 + percent / 100.0), 1) << " BPM";
    }
    if (text != tempoLabel.getText())
        tempoLabel.setText (text, juce::dontSendNotification);
}

} // namespace dss1::ui
