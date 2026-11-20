#pragma once

#include "Session.h"
#include "WaveformView.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui
{

/// The Prep stage: get the loaded track gig-ready. A big waveform with the grid, hot cues A–H, memory cues and
/// saved loops; buttons and keys to set them; grid tools (downbeat, nudge, tempo, tempo changes); and a target
/// (Rekordbox or Serato) whose limits are shown as you work. Snapping to the grid is off unless turned on.
class PrepPanel : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit PrepPanel (Session& session);
    ~PrepPanel() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

private:
    enum class Target { rekordbox = 1, serato = 2 };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();

    Frame playhead() const;
    Frame snapped (Frame f) const;
    bool edit (const std::function<void (TrackRecord&)>& change);
    void say (const juce::String& message);

    // Cues and loops.
    void hotCuePressed (std::size_t slot, bool remove);
    void addMemoryCue (Frame at);
    void jumpMemory (int direction);
    void saveLoop (Frame start, double beats);
    Frame loopEndFor (Frame start, double beats) const;
    void resizePlayingLoop();
    void loopIn();
    void loopOut();
    void playSavedLoop (std::size_t slot);
    void deleteLoop (std::size_t slot);
    std::optional<std::size_t> activeLoopSlot() const;
    void stepBeats (double beats);

    // Grid.
    void setDownbeat (Frame at);
    void nudgeGrid (double milliseconds);
    void scaleTempo (double factor);
    void setBpm (double bpm);
    void addTempoChange (Frame at);
    void removeTempoChangeNear (Frame at);
    void clearGrid();
    void addBackgroundActions (juce::PopupMenu& menu, Frame at);

    juce::StringArray targetNotes() const;

    Session& session;
    juce::PropertiesFile* settings = nullptr;
    WaveformView waveform;

    juce::ComboBox targetBox;
    juce::ToggleButton snapButton { "Snap to grid" };
    juce::TextButton zoomOut { "-" }, zoomIn { "+" }, wholeTrack { "Whole Track" };

    std::array<std::unique_ptr<juce::TextButton>, TrackRecord::hotCueSlots> hotButtons;
    std::array<std::unique_ptr<juce::TextButton>, TrackRecord::savedLoopSlots> loopButtons;

    juce::TextButton addMemoryButton { "+ Memory Cue" }, prevMemoryButton { "<" }, nextMemoryButton { ">" };
    juce::Slider loopKnob, jumpKnob;
    juce::Rectangle<int> waveUnit, controlUnit, messageArea;
    int loopLength() const;
    juce::TextButton saveLoopButton { "Save Loop" }, loopInButton { "Loop In" }, loopOutButton { "Loop Out" }, exitLoopButton { "Exit Loop" }, deleteLoopButton { "Delete Loop" };

    juce::Label bpmLabel;
    juce::TextEditor bpmEditor;
    juce::TextButton downbeatButton { "Downbeat Here" }, nudgeEarlier { "< Nudge" }, nudgeLater { "Nudge >" };
    juce::TextButton halveButton { "/2" }, doubleButton { "x2" }, tempoChangeButton { "Tempo Change Here" }, clearGridButton { "Clear Grid" };

    juce::String message;
    juce::uint32 messageTime = 0;
    std::optional<Frame> pendingLoopIn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrepPanel)
};

} // namespace dss1::ui
