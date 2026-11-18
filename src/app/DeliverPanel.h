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

    /// Switches the export to the tracks selected in Catalog.
    void useSelection();

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    /// Where exports go (inside the user's Music folder).
    /// Where exports go: the user's choice, or DSS1 Exports / DSS1 for Serato in their Music folder.
    juce::File rekordboxXmlFile() const;
    juce::File seratoFolder() const;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshCounts();
    std::vector<StoredTrack> chosenTracks() const;
    void exportRekordbox();
    void exportSerato();
    void finished (const juce::String& message, const juce::File& reveal);

    Crate& crate;
    juce::PropertiesFile* settings = nullptr;

    juce::ComboBox scopeBox;
    juce::TextButton rekordboxDest { "CHANGE..." }, seratoDest { "CHANGE..." };
    juce::Rectangle<int> rekordboxDestArea, seratoDestArea;
    std::unique_ptr<juce::FileChooser> chooser;
    void chooseRekordboxFile();
    void chooseSeratoFolder();
    juce::TextButton rekordboxButton { "Export for Rekordbox" }, rekordboxReveal { "Show in Finder" };
    juce::TextButton seratoButton { "Export Copies for Serato" }, seratoReveal { "Show in Finder" };
    juce::ToggleButton seratoGrid { "Include beat grid" };
    juce::TextButton timingButton { "Export Timing Check..." };
    juce::Label status;
    juce::Label chosenLabel;

    juce::Rectangle<int> rekordboxCard, seratoCard;
    std::atomic<bool> busy { false };

    JUCE_DECLARE_WEAK_REFERENCEABLE (DeliverPanel)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeliverPanel)
};

} // namespace dss1::ui
