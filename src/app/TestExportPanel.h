#pragma once

#include "core/RekordboxXml.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace dss1::ui
{

/// Test build only: the MP3 timing test. Drop a track; DSS1 finds 8 sharp hits (kicks, snares) and writes
/// copies with hot cues A–H exactly on them, for Serato (inside the file) and Rekordbox (an XML). For an MP3 it
/// also writes a WAV decoded by DSS1 with the same cues, so the two can be compared side by side.
/// Output goes to ~/Music/DSS1 Test Exports. The original is never changed.
class TestExportPanel : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    TestExportPanel();

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return ! busy; }
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

    /// What the background work hands back to the window.
    struct Outcome
    {
        juce::String report;
        std::vector<rekordbox::Track> tracks;   ///< for the XML; TrackIDs are filled in on the message thread
    };

private:
    void start (const juce::File& source);
    void finish (Outcome outcome);
    void writeRekordboxXml();
    void showReport (const juce::String& text);

    juce::File outputFolder;
    std::vector<rekordbox::Track> exported;  ///< every track made this session, for the XML
    int nextTrackId = 1;
    bool busy = false;

    juce::TextButton chooseButton { "Choose a track..." };
    juce::TextButton revealButton { "Show in Finder" };
    juce::TextEditor report;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> dropZone;
    bool dragOver = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (TestExportPanel)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TestExportPanel)
};

} // namespace dss1::ui
