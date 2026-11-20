#pragma once

#include "Crate.h"
#include "BeatScope.h"
#include "Session.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui
{

/// The XY performance pad: X sweeps the filter (low-pass left, high-pass right), Y sends to the echo (more at
/// the top). Touching it takes over the filter and echo; letting go puts them back, unless HOLD is on.
class XyPad : public juce::Component
{
public:
    explicit XyPad (engine::Effects& fx) : effects (fx) {}

    bool hold = false;
    std::function<void()> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /// Puts the filter and echo back as they were before the pad was touched.
    void letGo();

    /// Where the dot is, 0 … 1 each way; nullopt when the pad isn't in use.
    std::optional<juce::Point<float>> position;

private:
    void apply (juce::Point<float> p);
    void release();

    engine::Effects& effects;
    struct Saved
    {
        float filter;
        bool filterOn;
        float echoMix;
        bool echoOn;
    };
    std::optional<Saved> saved;
};

/// The Listen & Design stage: an effects rack (filter, echo, reverb, crusher) that plays over whatever the
/// deck plays, the XY performance pad, and a recorder that captures what you hear to a WAV, drawing its
/// waveform as it goes: listening, and making a sample. A take can be played back on its own, then saved (to
/// Music > DSS1 Recordings, and into Catalog), sent to CHOP to slice, or thrown away; recording again throws
/// away an unsaved take. (The pad sampler is its own stage, CHOP.)
class DesignPanel : public juce::Component, private juce::Timer
{
public:
    DesignPanel (Session& session, Crate& crate);
    ~DesignPanel() override;

    /// TO CHOP: called with the (saved) take, to slice it on the CHOP stage.
    std::function<void (const juce::File&)> onSendToChop;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    static juce::File recordingsFolder();

private:
    void timerCallback() override;
    void toggleRecording();
    void toggleTakePlayback();
    void saveTake();
    void discardTake();
    void mouseDown (const juce::MouseEvent&) override;
    static juce::File unsavedFolder();
    void syncFromEffects();

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::String name;
        std::function<juce::String (double)> readout;
    };

    Knob& addKnob (const juce::String& name, double min, double max, double step, double value, const juce::String& scale,
                   std::function<void (double)> set, std::function<juce::String (double)> readout);

    Session& session;
    Crate& crate;
    engine::Effects& fx;
    juce::PropertiesFile* settings = nullptr;

    std::vector<std::unique_ptr<Knob>> knobs;
    juce::ToggleButton filterOn { "FILTER" }, echoOn { "ECHO" }, reverbOn { "REVERB" }, compOn { "COMP" };
    // The compressor: its type keys, and a row of switches.
    std::array<juce::TextButton, 4> compTypeKeys;
    juce::ToggleButton attackSwitch { "ATTACK" }, releaseSwitch { "RELEASE" }, ratioSwitch { "RATIO" }, hpfSwitch { "SC HPF" }, crushSwitch { "CRUSH" };
    void selectCompType (int type);
    float shownReduction = 0;
    /// The compressor's backlit monitors: EQ curve, transfer curve with live gain reduction, envelope, mix.
    void paintCompMonitors (juce::Graphics&);
    std::array<juce::Rectangle<int>, 4> compMonitors;
    juce::Rectangle<int> filterArea, echoArea, reverbArea, compArea, effectsUnit, lowerUnit, padArea, recorderArea, timeArea, takeArea;

    XyPad pad;
    juce::ToggleButton holdButton { "HOLD" };
    juce::TextButton resetFxButton { "ALL OFF" };
    juce::TextButton recordButton { "REC" }, playTakeButton { "PLAY" }, saveTakeButton { "SAVE" }, discardButton { "DISCARD" },
        toChopButton { "TO CHOP" }, revealButton { "SHOW TAKES" };
    juce::File savedTake;  // the last take saved
    // The recorder's input pad: -10, -20 or -50 dB (one at a time, or none).
    std::array<juce::ToggleButton, 3> padSwitches;
    void setRecordPad (int which);
    BeatScope scope;
    std::vector<float> takePeaks;
    juce::Rectangle<int> waveArea;
    juce::String lastTake, recorderMessage;
    juce::File pendingTake;  // recorded, not saved yet
    bool takeSaved = false, wasTakePlaying = false;
    void updateRecorderKeys();

    JUCE_DECLARE_WEAK_REFERENCEABLE (DesignPanel)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DesignPanel)
};

} // namespace dss1::ui
