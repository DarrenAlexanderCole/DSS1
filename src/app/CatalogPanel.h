#pragma once

#include "AudioEngine.h"
#include "Crate.h"
#include "TransportBar.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace dss1::ui
{

/// The Catalog stage: the crate as a sortable table (title, artist, time, BPM, key, loudness), music added by
/// dropping files, folders or zips, and a deck to listen with.
class CatalogPanel : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     private juce::TableListBoxModel,
                     private juce::ChangeListener
{
public:
    CatalogPanel (Crate& crate, AudioEngine& audio);
    ~CatalogPanel() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    void sortOrderChanged (int column, bool ascending) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshRows();
    void sortRows();
    void play (int row);
    juce::String cellText (const StoredTrack& t, int column) const;

    Crate& crate;
    AudioEngine& audio;
    std::vector<StoredTrack> rows;
    int sortColumn = 1;
    bool sortAscending = true;
    int loadGeneration = 0;
    bool dragOver = false;

    juce::TableListBox table { "Crate", this };
    juce::TextButton addButton { "Add Music..." };
    juce::Label statusLabel;
    TransportBar transport;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_WEAK_REFERENCEABLE (CatalogPanel)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CatalogPanel)
};

} // namespace dss1::ui
