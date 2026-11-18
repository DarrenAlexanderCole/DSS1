#pragma once

#include "Session.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui
{

/// The pad sampler on the Listen & Design screen: banks A–H of 16 pads (128 samples, like a drum machine).
/// Capture a slice of the loaded track (the playing loop, or a length in beats from the playhead) into a pad,
/// or load a sound file. Every captured sample is written straight away as a WAV into the samples folder the
/// user chooses (Bank A … Bank H inside it), and the kit is remembered between launches.
class SamplerSection : public juce::Component, private juce::Timer
{
public:
    explicit SamplerSection (Session& session);
    ~SamplerSection() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    juce::File samplesFolder() const;

private:
    void timerCallback() override;
    int slotOf (int pad) const noexcept { return bank * 16 + pad; }
    void selectBank (int b);
    void padPressed (int pad);
    void showPadMenu (int pad);
    void capture (int slot);
    void loadFile (int slot);
    void clear (int slot);
    void setSlot (int slot, std::shared_ptr<const io::DecodedAudio> audio, const juce::File& file);
    void saveKit();
    void restoreKit();
    void chooseFolder();
    juce::String slotName (int slot) const;
    void refreshPads();

    Session& session;
    engine::Sampler& sampler;
    juce::PropertiesFile* settings = nullptr;

    int bank = 0, selected = 0;
    std::array<juce::File, 128> files;
    std::array<juce::TextButton, 8> bankKeys;
    std::array<juce::TextButton, 16> pads;
    juce::Slider lengthKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::TextButton captureKey { "CAPTURE" }, loadKey { "LOAD..." }, clearKey { "CLEAR" }, stopKey { "STOP ALL" }, folderKey { "CHANGE..." };
    juce::Rectangle<int> padArea, readoutArea, folderArea;
    juce::String message;
    std::unique_ptr<juce::FileChooser> chooser;
    std::array<bool, 16> wasPlaying {};

    JUCE_DECLARE_WEAK_REFERENCEABLE (SamplerSection)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerSection)
};

} // namespace dss1::ui
