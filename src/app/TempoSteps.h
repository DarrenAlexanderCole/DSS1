#pragma once

#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui
{

/// A row of small backlit squares that steps along with the music, like a step sequencer's running light:
/// the lit square moves on each step and leaves a fading trail, so you see how fast the track is moving.
/// Follows the track's grid when it has one (so the first square is the bar's downbeat), else its BPM.
/// Holds still where the playhead is when the deck is stopped.
class TempoSteps : public juce::Component, private juce::Timer
{
public:
    /// `steps` squares across one bar of 4 beats: 16 shows sixteenth notes, 4 shows the beats.
    TempoSteps (Session& session, int steps);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    std::optional<double> beatNow() const;

    Session& session;
    int steps;
};

} // namespace dss1::ui
