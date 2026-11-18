#pragma once

#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui
{

/// The deck's controls along the bottom of the window, on every stage: the loaded track's waveform (click to
/// jump), play, position, tempo and key lock.
class TransportBar : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit TransportBar (Session& session);
    ~TransportBar() override;

    void togglePlay();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateTempoLabel();
    void seekTo (int x);
    void updatePlayButton();

    Session& session;
    engine::Deck& deck;
    juce::String trackTitle { "No track loaded" }, trackDetails { "Double-click a track to play it" };
    std::vector<float> overview;
    juce::Rectangle<int> waveformArea;

    juce::TextButton playButton { "Play" };
    juce::Slider tempoSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label tempoLabel;
    juce::ToggleButton keyLockButton { "Key lock" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

/// One peak value per column: the loudest sample in that stretch of the track, across channels.
std::vector<float> overviewPeaks (const std::vector<std::vector<float>>& channels, int columns);

} // namespace dss1::ui
