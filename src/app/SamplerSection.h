#pragma once

#include "Session.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace dss1::ui
{

/// The sampler on the Listen & Design screen, built the way an MPC chops: each of the 8 banks (A–H) chops one
/// sample (the track on the deck, or a sound file) into up to 16 slices, one per pad. Pads are laid out and keyed
/// like a drum machine: 1–4 on Z X C V along the bottom, 5–8 on A S D F, 9–12 on Q W E R, 13–16 on 1 2 3 4.
///
/// Chopping: press an empty pad to mark the playhead (tap along while the track plays), let AUTO 16 cut the loop
/// (or bars from the playhead) into 16 equal slices, or click the waveform. A slice runs from its mark to the
/// next mark; the last one runs SLICE beats (or to where AUTO 16 ended). In CHOP mode a pad press moves its mark
/// to the playhead and a click on the waveform moves the chosen pad's mark there; in PLAY mode pads only play.
/// Drag a marker on the waveform to fine-tune it.
///
/// Playing: a pad restarts itself, and with CHOKE (on by default) cuts the rest of its bank, so slices play one
/// at a time. HOLD plays a slice only while the key or pad is held. Everything fades in and out in a few ms.
///
/// Marks are kept in settings, not as audio; SAVE BANK writes the slices as WAVs into the samples folder.
class SamplerSection : public juce::Component, private juce::Timer
{
public:
    explicit SamplerSection (Session& session);
    ~SamplerSection() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /// Pad keys (Z X C V, A S D F, Q W E R, 1 2 3 4). True if the key was a pad's.
    bool handleKey (const juce::KeyPress&);
    /// Call when keys go up: ends held pads, and stops a held key re-triggering.
    void keysChanged();

    juce::File samplesFolder() const;

    static constexpr const char* padKeys = "zxcvasdfqwer1234";  ///< pad 1 … 16

private:
    struct Bank
    {
        juce::File source;                              // the chopped track or sound file
        std::shared_ptr<const io::DecodedAudio> audio;
        std::array<std::optional<Frame>, 16> marks;     // where each pad's slice starts
        double framesPerBeat = 0;
        std::optional<Frame> chopEnd;                   // where AUTO 16's last slice ends
        bool empty() const noexcept;
    };

    class SliceView;
    friend class SliceView;

    void timerCallback() override;
    int slotOf (int pad) const noexcept { return bank * 16 + pad; }
    Bank& current() noexcept { return bankData[static_cast<std::size_t> (bank)]; }
    const Bank& current() const noexcept { return bankData[static_cast<std::size_t> (bank)]; }
    bool sourceIsOnDeck (const Bank&) const;

    void selectBank (int b);
    void padDown (int pad);
    void padUp (int pad);
    void showPadMenu (int pad);
    void markAtPlayhead (int pad);
    void setMark (int pad, Frame at);
    void clearPad (int pad);
    void clearBank();
    void autoChop();
    void loadFile();
    void saveBank();
    void setMode (bool chop);
    std::optional<Frame> sliceEnd (int bankIndex, int pad) const;
    void updateRegions (int bankIndex);
    bool startBankFromDeck();
    void saveKit();
    void restoreKit();
    void chooseFolder();
    juce::String slotName (int slot) const;
    juce::String describe (int pad) const;
    void say (const juce::String& text);
    void refreshPads();

    Session& session;
    engine::Sampler& sampler;
    juce::PropertiesFile* settings = nullptr;

    int bank = 0, selected = 0;
    bool chopMode = false, hold = false;
    std::array<Bank, 8> bankData;
    std::array<juce::TextButton, 8> bankKeys;
    std::array<juce::TextButton, 16> pads;
    std::array<bool, 16> held {}, wasPlaying {};
    int mouseHeldPad = -1;
    std::unique_ptr<SliceView> view;
    juce::Slider sliceKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::TextButton chopKey { "CHOP" }, playKey { "PLAY" }, holdKey { "HOLD" }, chokeKey { "CHOKE" };
    juce::TextButton autoKey { "AUTO 16" }, saveKey { "SAVE BANK" }, loadKey { "LOAD..." }, clearKey { "CLEAR" },
        clearBankKey { "CLEAR BANK" }, stopKey { "STOP ALL" }, folderKey { "CHANGE..." };
    juce::Rectangle<int> readoutArea, folderArea;
    juce::String message;
    juce::uint32 messageTime = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ScopedMessageBox messageBox;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SamplerSection)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerSection)
};

} // namespace dss1::ui
