#pragma once

#include "MidiPads.h"
#include "PadMeters.h"
#include "Session.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace dss1::ui
{

class ChopView;

/// One bank of the CHOP screen: the sample it chops and up to 16 slices, one per pad.
struct ChopBank
{
    juce::File source;                                   // the chopped track or sound file
    std::shared_ptr<const io::DecodedAudio> audio;
    std::array<std::optional<Frame>, 16> marks;          // where each pad's slice starts
    std::array<engine::Sampler::PadSettings, 16> settings;
    double framesPerBeat = 0;
    std::optional<Frame> chopEnd;                        // where the last slice ends (EQUAL 16, TRANSIENTS, or the END marker in MM)
    bool empty() const noexcept;
};

/// The CHOP stage: a sampler for producers, built the way an MPC (or a Behringer KICK) chops. Each of the
/// 8 banks (A–H) chops one sample, the track on the deck or a sound file, into up to 16 slices, one per pad.
/// Pads are laid out and keyed like a drum machine: 1–4 on Z X C V along the bottom, 5–8 on A S D F, 9–12 on
/// Q W E R, 13–16 on 1 2 3 4.
///
/// The top unit is the sample, zoomable like Prep's waveform, with the slice markers, the beat grid and the
/// playheads: drag a marker to move it (snapping to the grid or to a zero crossing), nudge it with the arrow
/// keys, click a slice to hear it. The bottom unit has the pads, the ways to chop (tap pads on the beats,
/// EQUAL 16, TRANSIENTS, or click the waveform in CHOP mode), UNDO, and the chosen pad's settings (gain, tune,
/// reverse, attack, fade).
///
/// Pads restart themselves and, with CHOKE (on by default), cut the rest of their bank, so slices play one at
/// a time. HOLD plays a slice only while it's held. Space plays the deck, or the sound file being chopped.
/// Marks are kept in settings, not as audio; SAVE BANK writes the slices as WAVs into the samples folder.
class ChopPanel : public juce::Component, private juce::Timer
{
public:
    explicit ChopPanel (Session& session);
    ~ChopPanel() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /// Keys while CHOP shows: pads (Z X C V, A S D F, Q W E R, 1 2 3 4), arrows (nudge), Cmd+Z (undo), Space
    /// (plays a sound file being chopped). True if the key was used.
    bool handleKey (const juce::KeyPress&);
    /// Call when keys go up: ends held pads, and stops a held key re-triggering.
    void keysChanged();
    /// Chops `file` in the current bank (from the recorder's TO CHOP).
    void chopFile (const juce::File& file);

    juce::File samplesFolder() const;

    static constexpr const char* padKeys = "zxcvasdfqwer1234";  ///< pad 1 … 16

private:
    friend class ChopView;
    enum class Snap { off, beat, zero };

    void timerCallback() override;
    int slotOf (int pad) const noexcept { return bank * 16 + pad; }
    ChopBank& current() noexcept { return bankData[static_cast<std::size_t> (bank)]; }
    const ChopBank& current() const noexcept { return bankData[static_cast<std::size_t> (bank)]; }
    bool sourceIsOnDeck (const ChopBank&) const;
    /// What the waveform shows: the bank's sample, or the deck's track before the bank has one.
    const io::DecodedAudio* shownAudio() const;

    void selectBank (int b);
    void selectPad (int pad);
    void padDown (int pad, float velocity = 1.0f);
    void padUp (int pad);
    void showPadMenu (int pad);
    void markAtPlayhead (int pad);
    void setMark (int pad, Frame at, bool remember = true);
    Frame snapped (Frame at) const;
    void nudge (int direction, bool bigStep);
    void clearPad (int pad);
    void clearBank();
    void chopEqual();
    void chopTransients();
    void applyChop (Frame from, Frame to, const std::vector<Frame>& marks, const juce::String& what);
    void loadFile();
    void saveBank();
    void cycleSnap();
    std::optional<Frame> sliceEnd (int bankIndex, int pad) const;
    /// MM: plays a marked pad's slice with its settings and parks the source (deck or file) at its start.
    void cueFrom (int pad, float velocity);
    /// SLICE at MM (manual): the last slice ends at the END marker you place, not after so many beats.
    bool manualLength() const { return sliceKnob.getValue() < 0.5; }
    std::optional<double> sliceBeats() const;
    void setEndMarker (Frame at);
    void midiNoteOn (int note, float velocity);
    void midiNoteOff (int note);
    void midiControl (int controller, int value);
    void midiPlay (std::optional<bool> play);
    void setLearning (bool on);
    void showMidiMenu();
    void saveMidiMap();
    /// Pads mapped as the connected controller sends them, unless you've learned or chosen a map.
    void autoMidiMap();
    void usePadMap (const std::array<int, 16>& notes, const juce::String& name, bool chosen);
    juce::String padMapSource;  // "auto" (follows the controller), "chosen" (a preset), "learned"
    void updateRegions (int bankIndex);
    void pushSettings (int bankIndex, int pad);
    bool startBankFromDeck();
    void rememberForUndo();
    void undo();
    void toggleFilePreview();
    void showPadSettings();
    void saveKit();
    void restoreKit();
    void chooseFolder();
    juce::String slotName (int slot) const;
    juce::String describe (int pad) const;
    void say (const juce::String& text);
    void refreshPads();
    io::DecodedAudio renderSlice (const ChopBank&, int bankIndex, int pad) const;

    Session& session;
    engine::Sampler& sampler;
    juce::PropertiesFile* settings = nullptr;

    int bank = 0, selected = 0;
    bool hold = false;
    Snap snap = Snap::zero;
    std::array<ChopBank, 8> bankData;
    std::vector<std::pair<int, ChopBank>> undoStack;
    Frame previewCursor = 0;  // where Space plays a sound file from

    std::unique_ptr<ChopView> view;
    std::array<juce::TextButton, 8> bankKeys;
    std::array<juce::TextButton, 16> pads;
    std::array<bool, 16> held {}, wasPlaying {};
    int mouseHeldPad = -1;

    MidiPads midi;
    std::array<int, 16> padNotes {};       // MIDI note for each pad
    int playController = -1, playNote = -1; // a learned play button (a CC or a note)
    bool learning = false;
    int learnTarget = 0;
    juce::uint32 midiSeenAt = 0;
    juce::TextButton midiKey { "MIDI LEARN" }, showSavedKey { "SHOW SAVED" };
    juce::Rectangle<int> midiArea;
    double previousSliceSetting = 3;
    juce::TextButton snapKey, zoomOutKey { "-" }, zoomInKey { "+" }, sliceZoomKey { "SLICE" }, wholeKey { "WHOLE" };
    juce::TextButton holdKey { "HOLD" }, chokeKey { "CHOKE" };
    /// Lights beside the pads: how loud each pad of this bank plays.
    std::unique_ptr<PadMeters> meters;
    /// Once a pad has been played from the keyboard, the big key letters on the pads aren't needed (until
    /// the app opens again).
    bool keysLearned = false;
    juce::TextButton equalKey { "EQUAL 16" }, transientKey { "TRANSIENTS" }, undoKey { "UNDO" }, nudgeLeft { "<" }, nudgeRight { ">" };
    juce::TextButton loadKey { "LOAD..." }, saveKey { "SAVE BANK" }, clearKey { "CLEAR" }, clearBankKey { "CLEAR BANK" }, stopKey { "STOP ALL" },
        folderKey { "CHANGE..." };
    juce::Slider sliceKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider gainKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox },
        tuneKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox },
        attackKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox },
        fadeKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::TextButton reverseKey { "REVERSE" };

    std::array<juce::Rectangle<int>, 4> monitorAreas;  // GAIN, TUNE, ATTACK, FADE curve monitors
    void paintMonitor (juce::Graphics&, int which);
    const std::vector<float>& slicePeaks();
    std::vector<float> peakCache;
    std::tuple<int, int, Frame, Frame, const void*> peakCacheKey { -1, -1, 0, 0, nullptr };
    juce::Rectangle<int> waveUnit, controlUnit, messageArea, readoutArea, folderArea, padSettingsArea;
    juce::String message;
    juce::uint32 messageTime = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ScopedMessageBox messageBox;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ChopPanel)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopPanel)
};

} // namespace dss1::ui
