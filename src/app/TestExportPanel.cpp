#include "TestExportPanel.h"
#include "Theme.h"

#include "core/Id3.h"
#include "core/RekordboxMapping.h"
#include "core/SeratoTags.h"
#include "core/TestExport.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace dss1::ui
{

namespace
{
    constexpr int dropZoneHeight = 96;

    juce::String rekordboxKind (const juce::String& extension)
    {
        const auto e = extension.toLowerCase();
        if (e == ".mp3")
            return "MP3 File";
        if (e == ".aif" || e == ".aiff")
            return "AIFF File";
        return "WAV File";
    }

    const char* const howToCheck =
        "HOW TO CHECK\n"
        "Each cue and loop should start at exactly the time listed. In Rekordbox they also sit on bar lines of a 120 BPM grid from 0:00; Serato gets no grid (DSS1 leaves Serato grids off unless you ask).\n\n"
        "Serato DJ: drag the test copy from Music > DSS1 Test Exports into your library and load it. "
        "Check the times of cues 1-8 and saved loops 1-2 in the cue panel.\n\n"
        "Rekordbox (6 or 7): in Preferences > View > Layout, tick \"rekordbox xml\". In Preferences > Advanced > "
        "Database, set the rekordbox xml Imported Library to \"DSS1 test.xml\" in the same folder. In the browser's "
        "side panel, open rekordbox xml > Playlists > DSS1 test, right-click the track and choose Import To "
        "Collection. Look at hot cues A-H, the memory cue, the memory loops and the grid.\n\n"
        "Then tell me: did each one land where the list says, and if not, roughly how far off was it?";
}

TestExportPanel::TestExportPanel()
{
    outputFolder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DSS1 Test Exports");

    chooseButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Choose a WAV, AIFF or MP3",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.aif;*.aiff;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) {
                                  const auto file = fc.getResult();
                                  if (file.existsAsFile())
                                      makeTestCopy (file);
                              });
    };
    addAndMakeVisible (chooseButton);

    revealButton.onClick = [this] {
        if (outputFolder.isDirectory())
            outputFolder.revealToUser();
    };
    revealButton.setEnabled (false);
    addAndMakeVisible (revealButton);

    report.setMultiLine (true, true);
    report.setReadOnly (true);
    report.setCaretVisible (false);
    report.setScrollbarsShown (true);
    report.setFont (juce::FontOptions (14.0f));
    report.setColour (juce::TextEditor::backgroundColourId, Colours::panel);
    report.setColour (juce::TextEditor::textColourId, Colours::text);
    report.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    report.setText (howToCheck);
    addAndMakeVisible (report);
}

void TestExportPanel::paint (juce::Graphics& g)
{
    const auto zone = dropZone.toFloat();
    g.setColour (dragOver ? Colours::accent.withAlpha (0.18f) : Colours::panel);
    g.fillRoundedRectangle (zone, 10.0f);

    juce::Path outline;
    outline.addRoundedRectangle (zone.reduced (1.0f), 10.0f);
    juce::Path dashed;
    const float dashes[] = { 8.0f, 6.0f };
    juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (dragOver ? Colours::accent : Colours::dimText);
    g.fillPath (dashed);

    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    auto text = dropZone.reduced (20, 0).withTrimmedRight (200);
    g.drawText ("Test export: drop a WAV, AIFF or MP3 here", text.removeFromTop (dropZone.getHeight() / 2).withTrimmedTop (14),
                juce::Justification::bottomLeft);
    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("DSS1 makes a tagged copy for Serato and an XML for Rekordbox. Your original isn't changed.",
                text.withTrimmedBottom (14), juce::Justification::topLeft);
}

void TestExportPanel::resized()
{
    auto area = getLocalBounds();
    dropZone = area.removeFromTop (dropZoneHeight);

    auto buttons = dropZone.reduced (20).removeFromRight (170);
    chooseButton.setBounds (buttons.removeFromTop (buttons.getHeight() / 2).reduced (0, 3));
    revealButton.setBounds (buttons.reduced (0, 3));

    area.removeFromTop (16);
    report.setBounds (area);
}

void TestExportPanel::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void TestExportPanel::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void TestExportPanel::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    for (const auto& path : files)
    {
        const juce::File file (path);
        if (file.existsAsFile())
            makeTestCopy (file);
    }
}

void TestExportPanel::showReport (const juce::String& text)
{
    report.setText (text);
    report.moveCaretToTop (false);
}

void TestExportPanel::makeTestCopy (const juce::File& source)
{
    const auto extension = source.getFileExtension();
    const auto container = id3::containerForExtension (extension.toStdString());
    if (! container)
    {
        showReport ("\"" + source.getFileName() + "\" isn't a WAV, AIFF or MP3. For this test, please use one of those.\n\n"
                    + howToCheck);
        return;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
    if (reader == nullptr || reader->sampleRate <= 0)
    {
        showReport ("DSS1 couldn't read the audio in \"" + source.getFileName() + "\". Try another file.\n\n" + howToCheck);
        return;
    }
    const auto sampleRate = reader->sampleRate;
    const auto length = static_cast<Frame> (reader->lengthInSamples);

    if (! outputFolder.createDirectory())
    {
        showReport ("DSS1 couldn't create the folder " + outputFolder.getFullPathName());
        return;
    }

    const auto name = source.getFileNameWithoutExtension() + " (DSS1 test)";
    const auto target = outputFolder.getChildFile (name + extension);
    const auto record = makeTestRecord (target.getFullPathName().toStdString(), sampleRate, length);

    try
    {
        // Serato: the cues, loops and grid go inside a copy of the file.
        juce::MemoryBlock data;
        if (! source.loadFileAsData (data))
            throw std::runtime_error ("couldn't read the file");
        const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
        const id3::Bytes original (bytes, bytes + data.getSize());

        const auto serato = serato::exportTrack (record);
        std::vector<id3::GeobFrame> frames { { "Serato Markers2", serato::wrapMarkersForGeob (serato::encodeMarkers (serato.markers)) } };
        if (serato.grid)
            frames.push_back ({ "Serato BeatGrid", serato::encodeGrid (*serato.grid) });
        const auto tagged = id3::writeGeobFrames (original, *container, frames);
        if (! target.replaceWithData (tagged.data(), tagged.size()))
            throw std::runtime_error ("couldn't write " + target.getFullPathName().toStdString());

        // Rekordbox: the same positions in the XML, pointing at the copy.
        rekordbox::TrackDetails details;
        details.trackId = std::to_string (nextTrackId++);
        details.name = name.toStdString();
        details.kind = rekordboxKind (extension).toStdString();
        auto track = rekordbox::exportTrack (record, details);
        std::erase_if (exported, [&] (const rekordbox::Track& t) { return t.location == track.location; });
        exported.push_back (std::move (track));
        writeRekordboxXml();

        juce::String text;
        text << "Made \"" << target.getFileName() << "\" and \"DSS1 test.xml\" in Music > DSS1 Test Exports.\n\n";
        text << "WHAT YOU SHOULD SEE\n";
        for (const auto& p : testPositions (record))
        {
            text << p.what << ": " << formatTime (p.seconds);
            if (p.endSeconds > 0)
                text << " to " << formatTime (p.endSeconds);
            text << "\n";
        }
        text << "Grid (Rekordbox only): 120 BPM, first downbeat at 0:00.000\n";
        if (! serato.notes.empty())
        {
            text << "\nSerato only: ";
            for (const auto& n : serato.notes)
                text << n << ". ";
            text << "\n";
        }
        text << "\n" << howToCheck;
        showReport (text);
        revealButton.setEnabled (true);
    }
    catch (const std::exception& e)
    {
        showReport ("Something went wrong making the test copy: " + juce::String (e.what()) + "\n\n" + howToCheck);
    }
}

void TestExportPanel::writeRekordboxXml()
{
    rekordbox::Collection c;
    c.productName = "DSS1";
    c.productVersion = JUCE_APPLICATION_VERSION_STRING;
    c.productCompany = "DSS1";
    c.tracks = exported;

    rekordbox::Node playlist;
    playlist.kind = rekordbox::Node::Kind::playlist;
    playlist.name = "DSS1 test";
    for (const auto& t : exported)
        playlist.trackKeys.push_back (t.trackId);
    c.playlists.children.push_back (playlist);

    outputFolder.getChildFile ("DSS1 test.xml").replaceWithText (juce::String::fromUTF8 (rekordbox::write (c).c_str()));
}

} // namespace dss1::ui
