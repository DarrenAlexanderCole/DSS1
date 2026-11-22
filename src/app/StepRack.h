#pragma once

#include "engine/Modular.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace dss1::ui
{

/// STEP, the sample snatcher: the effects rack flipped into a 16-step sequencer. GRAB takes the next 1, 2 or 4
/// beats of what's playing, cut into sixteenths; each step plays one of them (click a step to switch it on or
/// off, drag it up or down to pick its slice); RUN plays the pattern in time with the music. CLEAR throws the
/// sample away to grab again.
///
/// Like DELUXE, the first time it opens in a session the panel sinks into the case and slides away, and the
/// sequencer comes up, its steps lighting one after another.
class StepRack : public juce::Component, private juce::Timer
{
public:
    explicit StepRack (engine::Modular& sound);
    ~StepRack() override;

    void open (juce::Image previousPanel, juce::Rectangle<int> panelArea, bool animate);
    void close();

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    struct Pad;

private:
    void timerCallback() override;
    float introProgress (double delayMs, double lengthMs) const;
    void setBeats (int beats);
    void resetSteps();
    void refreshPads();
    void readSample();

    engine::Modular& sound;
    engine::Modular::Step& step;
    std::array<std::unique_ptr<Pad>, 16> pads;
    juce::TextButton grab { "GRAB" }, clear { "CLEAR" }, one { "1" }, two { "2" }, four { "4" }, reset { "RESET" };
    juce::ToggleButton run, replace;
    juce::ToggleButton reverse;
    using Knob = juce::Slider;
    Knob level, swing, length, decay, repeat, echoTime, echoAmount, low, mid, high;
    /// The steps' own tempo: SYNC (follow the track) or a BPM of their own, set with - + or by dragging it.
    juce::ToggleButton sync;
    juce::TextButton slower { "-1" }, faster { "+1" };
    struct TempoField;
    std::unique_ptr<TempoField> tempoField;
    void setTempo (float bpm);
    /// The knobs, their names and their readouts, for painting.
    struct KnobInfo
    {
        Knob* knob;
        const char* name;
        std::function<juce::String()> text;
    };
    std::vector<KnobInfo> knobInfo;
    /// The groups of controls under the steps: PLAY, GATE, NOTE REPEAT, SPACE ECHO.
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> groups;
    juce::Rectangle<int> header, row, leftBay, rightBay, monitor, ledRow;

    // The grabbed sample's outline for the monitor (peaks per pixel column), read once it's ready.
    std::vector<float> outline;
    int shownState = -1, shownPlaying = -2;

    juce::Image previous;
    juce::Rectangle<int> previousArea;
    bool introRunning = false;
    double introStartMs = 0;
};

} // namespace dss1::ui
