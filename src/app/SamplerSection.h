#pragma once

#include "Session.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

namespace dss1::ui
{

/// The pad sampler on the Listen & Design screen: banks A–H of 16 pads (128, like a drum machine), laid out and
/// keyed like one: 1–4 on Z X C V along the bottom, 5–8 on A S D F, 9–12 on Q W E R, 13–16 on 1 2 3 4 at the top.
///
/// It slices. The first press of an empty pad marks the playhead; every press after plays the slice, which runs
/// from that mark to the next mark in the same track (in time) in this bank. The last slice runs LAST SLICE
/// beats. Marks move nothing in the track: slices are kept as marks and only written as WAVs when you press
/// SAVE BANK, into Bank A … Bank H inside the samples folder. A pad can also hold a sound file. The kit (marks
/// and files) is remembered between launches.
class SamplerSection : public juce::Component, private juce::Timer
{
public:
    explicit SamplerSection (Session& session);
    ~SamplerSection() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    /// Pad keys (Z X C V, A S D F, Q W E R, 1 2 3 4). True if the key was a pad's.
    bool handleKey (const juce::KeyPress&);
    /// Call when keys go up, so a held key doesn't keep re-triggering its pad.
    void keysChanged();

    juce::File samplesFolder() const;

    static constexpr const char* padKeys = "zxcvasdfqwer1234";  ///< pad 1 … 16

private:
    struct Pad
    {
        enum class Kind { empty, slice, file } kind = Kind::empty;
        juce::File file;                                  // the track a slice is in, or the sound file
        std::shared_ptr<const io::DecodedAudio> audio;
        Frame start = 0;                                  // slices: the mark
        double framesPerBeat = 0;                         // slices: for the last slice's length
    };

    void timerCallback() override;
    int slotOf (int pad) const noexcept { return bank * 16 + pad; }
    void selectBank (int b);
    void padPressed (int pad);
    void showPadMenu (int pad);
    void mark (int slot);
    void loadFile (int slot);
    void clear (int slot);
    void clearBank();
    void saveBank();
    Frame sliceEnd (int slot) const;
    void updateRegions();
    std::shared_ptr<const io::DecodedAudio> sourceFor (const juce::File& track);
    void saveKit();
    void restoreKit();
    void chooseFolder();
    juce::String slotName (int slot) const;
    juce::String describe (int slot) const;
    void refreshPads();

    Session& session;
    engine::Sampler& sampler;
    juce::PropertiesFile* settings = nullptr;

    int bank = 0, selected = 0;
    std::array<Pad, 128> kit;
    std::array<juce::TextButton, 8> bankKeys;
    std::array<juce::TextButton, 16> pads;
    std::array<bool, 16> held {};
    juce::Slider lengthKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::TextButton saveKey { "SAVE BANK" }, loadKey { "LOAD..." }, clearKey { "CLEAR" }, stopKey { "STOP ALL" }, folderKey { "CHANGE..." };
    juce::Rectangle<int> padArea, readoutArea, folderArea;
    juce::String message;
    std::unique_ptr<juce::FileChooser> chooser;
    std::array<bool, 16> wasPlaying {};

    JUCE_DECLARE_WEAK_REFERENCEABLE (SamplerSection)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerSection)
};

} // namespace dss1::ui
