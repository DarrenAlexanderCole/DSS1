#pragma once

#include "core/RekordboxXml.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace dss1::ui
{

/// Test build only: drop a track to get a copy tagged with known Serato cues, loops and grid, plus a
/// Rekordbox XML with the same positions, so they can be checked in the real apps.
/// Output goes to ~/Music/DSS1 Test Exports.
class TestExportPanel : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    TestExportPanel();

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void makeTestCopy (const juce::File& source);
    void writeRekordboxXml();
    void showReport (const juce::String& text);

    juce::File outputFolder;
    std::vector<rekordbox::Track> exported;  ///< every track made this session, for the XML
    int nextTrackId = 1;

    juce::TextButton chooseButton { "Choose a track..." };
    juce::TextButton revealButton { "Show in Finder" };
    juce::TextEditor report;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> dropZone;
    bool dragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TestExportPanel)
};

} // namespace dss1::ui
