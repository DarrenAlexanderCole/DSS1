#include "TestExportPanel.h"
#include "Theme.h"

#include "core/Hits.h"
#include "core/Id3.h"
#include "core/RekordboxMapping.h"
#include "core/SeratoTags.h"
#include "core/TestExport.h"
#include "io/AudioDecoder.h"

#include <thread>

namespace dss1::ui
{

namespace
{
    constexpr int dropZoneHeight = 96;
    constexpr std::size_t hitCount = 8;

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
        "Each hot cue should sit at the very start of a hit (a kick, snare or clap): exactly where the waveform jumps up. "
        "Zoom the waveform in as far as it goes to see it clearly.\n\n"
        "For an MP3 you also get a \"(DSS1 timing, WAV)\" copy with the same cues. Load both, one on each deck. "
        "If the WAV's cues sit on the hits but the MP3's are a little early or late, that's the timing difference "
        "this test is looking for.\n\n"
        "Serato DJ: drag the copies from Music > DSS1 Test Exports into your library.\n\n"
        "Rekordbox (6 or 7): in Preferences > Advanced > Database, set the rekordbox xml Imported Library to "
        "\"DSS1 timing.xml\" in the same folder. In the side panel, open rekordbox xml > Playlists > DSS1 timing, "
        "right-click each track and choose Import To Collection.\n\n"
        "Then tell me what you saw, ideally with a zoomed-in screenshot of one cue in each app.";

    id3::Bytes readBytes (const juce::File& file)
    {
        juce::MemoryBlock data;
        if (! file.loadFileAsData (data))
            throw std::runtime_error ("couldn't read " + file.getFullPathName().toStdString());
        const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
        return id3::Bytes (bytes, bytes + data.getSize());
    }

    /// Writes `audioFileBytes` to `target` with Serato cues inside, and returns the Rekordbox entry for it.
    rekordbox::Track makeCopy (const juce::File& target, const TrackRecord& record, const juce::String& name,
                               const juce::String& extension, const id3::Bytes& audioFileBytes)
    {
        const auto container = id3::containerForExtension (extension.toStdString());
        if (! container)
            throw std::runtime_error ("unsupported file type " + extension.toStdString());
        const auto serato = serato::exportTrack (record);
        const std::vector<id3::GeobFrame> frames { { "Serato Markers2", serato::wrapMarkersForGeob (serato::encodeMarkers (serato.markers)) } };
        const auto tagged = id3::writeGeobFrames (audioFileBytes, *container, frames);
        if (! target.replaceWithData (tagged.data(), tagged.size()))
            throw std::runtime_error ("couldn't write " + target.getFullPathName().toStdString());

        rekordbox::TrackDetails details;
        details.name = name.toStdString();
        details.kind = rekordboxKind (extension).toStdString();
        return rekordbox::exportTrack (record, details);
    }

    /// The whole test for one file. Runs on a background thread.
    TestExportPanel::Outcome runTimingTest (const juce::File& source, const juce::File& outputFolder)
    {
        TestExportPanel::Outcome out;
        const auto extension = source.getFileExtension();
        if (! id3::containerForExtension (extension.toStdString()))
        {
            out.report = "\"" + source.getFileName() + "\" isn't a WAV, AIFF or MP3. For this test, please use one of those.";
            return out;
        }

        try
        {
            const auto decoded = io::decode (source.getFullPathName().toStdString());
            const auto rate = decoded.info.sampleRate;
            const auto length = decoded.info.lengthFrames;

            // Look between 5 s in and 3 minutes (or 5 s before the end), so cues land in the music.
            const auto margin = static_cast<Frame> (5 * rate);
            const auto to = std::min<Frame> (length - margin, static_cast<Frame> (180 * rate));
            const auto hits = findHits (mixDown (decoded.channels), rate, hitCount, margin, to);
            if (hits.empty())
            {
                out.report = "DSS1 couldn't find clear hits in \"" + source.getFileName()
                           + "\". Try a track with a strong kick or snare, at least 30 seconds long.";
                return out;
            }

            if (! outputFolder.createDirectory())
                throw std::runtime_error ("couldn't create " + outputFolder.getFullPathName().toStdString());

            juce::String made;
            const auto stem = source.getFileNameWithoutExtension();

            // 1. A copy of the file itself.
            const auto name = stem + " (DSS1 timing)";
            const auto target = outputFolder.getChildFile (name + extension);
            auto record = makeTimingRecord (target.getFullPathName().toStdString(), rate, length, hits);
            // Serato and Rekordbox count an MP3's encoder delay; DSS1 removed it. (The WAV copy has none.)
            record.exportOffsetFrames = decoded.info.encoderDelayFrames;
            out.tracks.push_back (makeCopy (target, record, name, extension, readBytes (source)));
            made << "\"" << target.getFileName() << "\"";

            // 2. For an MP3, the same music decoded by DSS1 and saved as WAV, with the same cues.
            if (extension.equalsIgnoreCase (".mp3"))
            {
                const auto wavName = stem + " (DSS1 timing, WAV)";
                const auto wavTarget = outputFolder.getChildFile (wavName + ".wav");
                io::writeWav (wavTarget.getFullPathName().toStdString(), decoded);
                const auto wavRecord = makeTimingRecord (wavTarget.getFullPathName().toStdString(), rate, length, hits);
                out.tracks.push_back (makeCopy (wavTarget, wavRecord, wavName, ".wav", readBytes (wavTarget)));
                made << " and \"" << wavTarget.getFileName() << "\"";
            }

            juce::String text;
            text << "Made " << made << " and \"DSS1 timing.xml\" in Music > DSS1 Test Exports.\n\n";
            text << "HOT CUES ON HITS\n";
            for (const auto& p : testPositions (record))
                text << p.what << ": " << formatTime (p.seconds) << "\n";
            if (decoded.info.encoderDelayFrames > 0)
                text << "\n(MP3 detail for me: the encoder added " << static_cast<int> (decoded.info.encoderDelayFrames)
                     << " samples of silence at the start; DSS1 shifted the MP3's cues by that much for Serato and Rekordbox.)\n";
            text << "\n" << howToCheck;
            out.report = text;
        }
        catch (const std::exception& e)
        {
            out.tracks.clear();
            out.report = "Something went wrong: " + juce::String (e.what());
        }
        return out;
    }
} // namespace

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
                                      start (file);
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
    g.drawText (busy ? "Working... finding hits and writing copies"
                     : "MP3 timing test: drop a WAV, AIFF or MP3 here",
                text.removeFromTop (dropZone.getHeight() / 2).withTrimmedTop (14), juce::Justification::bottomLeft);
    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("DSS1 puts hot cues on 8 hits in the music. An MP3 works best. Your original isn't changed.",
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
        if (const juce::File file (path); file.existsAsFile())
        {
            start (file);
            break;  // one at a time
        }
}

void TestExportPanel::showReport (const juce::String& text)
{
    report.setText (text);
    report.moveCaretToTop (false);
}

void TestExportPanel::start (const juce::File& source)
{
    if (busy)
        return;
    busy = true;
    chooseButton.setEnabled (false);
    showReport ("Working on \"" + source.getFileName() + "\"...");
    repaint();

    std::thread ([weak = juce::WeakReference<TestExportPanel> (this), source, folder = outputFolder] {
        auto outcome = std::make_shared<Outcome> (runTimingTest (source, folder));
        juce::MessageManager::callAsync ([weak, outcome] {
            if (auto* panel = weak.get())
                panel->finish (std::move (*outcome));
        });
    }).detach();
}

void TestExportPanel::finish (Outcome outcome)
{
    busy = false;
    chooseButton.setEnabled (true);

    const bool madeSomething = ! outcome.tracks.empty();
    for (auto& track : outcome.tracks)
    {
        track.trackId = std::to_string (nextTrackId++);
        std::erase_if (exported, [&] (const rekordbox::Track& t) { return t.location == track.location; });
        exported.push_back (std::move (track));
    }
    if (madeSomething)
    {
        writeRekordboxXml();
        revealButton.setEnabled (true);
    }

    showReport (outcome.report);
    repaint();
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
    playlist.name = "DSS1 timing";
    for (const auto& t : exported)
        playlist.trackKeys.push_back (t.trackId);
    c.playlists.children.push_back (playlist);

    outputFolder.getChildFile ("DSS1 timing.xml").replaceWithText (juce::String::fromUTF8 (rekordbox::write (c).c_str()));
}

} // namespace dss1::ui
