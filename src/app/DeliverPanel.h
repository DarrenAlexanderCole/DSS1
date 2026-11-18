#pragma once

#include "Crate.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>

namespace dss1::ui
{

/// The Deliver stage: sends the prep to DJ apps without ever changing the user's music files. Rekordbox gets an
/// XML file pointing at the originals; Serato gets copies with the cues and loops written in. Exports run in
/// the background.
class DeliverPanel : public juce::Component, private juce::ChangeListener
{
public:
    explicit DeliverPanel (Crate& crate);
    ~DeliverPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /// Where exports go (inside the user's Music folder).
    static juce::File rekordboxXmlFile();
    static juce::File seratoFolder();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshCounts();
    std::vector<StoredTrack> chosenTracks() const;
    void exportRekordbox();
    void exportSerato();
    void finished (const juce::String& message, const juce::File& reveal);

    Crate& crate;
    std::unique_ptr<juce::PropertiesFile> settings;

    juce::ComboBox scopeBox;
    juce::TextButton rekordboxButton { "Export for Rekordbox" }, rekordboxReveal { "Show in Finder" };
    juce::TextButton seratoButton { "Export Copies for Serato" }, seratoReveal { "Show in Finder" };
    juce::ToggleButton seratoGrid { "Include beat grid" };
    juce::TextButton timingButton { "Export Timing Check..." };
    juce::Label status;

    juce::Rectangle<int> rekordboxCard, seratoCard;
    std::atomic<bool> busy { false };

    JUCE_DECLARE_WEAK_REFERENCEABLE (DeliverPanel)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeliverPanel)
};

} // namespace dss1::ui
