#include "DeliverPanel.h"
#include "TestExportPanel.h"
#include "Theme.h"

#include "core/Deliver.h"

#include <thread>

namespace dss1::ui
{

namespace
{
    enum Scope { prepared = 1, everything = 2, selection = 3 };

    const char* const rekordboxHow =
        "Writes one file that points at your music; your files aren't touched.\n"
        "In Rekordbox: Preferences > Advanced > Database > rekordbox xml, choose the file. Then in the sidebar "
        "open rekordbox xml > Playlists > DSS1, select the tracks, right-click > Import To Collection.";

    const char* const seratoHow =
        "Serato reads cues from inside the files, so DSS1 makes copies with your cues and loops written in. "
        "Your originals aren't touched. Drag the folder into Serato (or find it under Files). Exporting again "
        "replaces the copies. FLAC isn't supported for Serato yet.";

    juce::String plural (std::size_t n, const char* one, const char* many)
    {
        return juce::String (n) + " " + (n == 1 ? one : many);
    }

    juce::File fileFor (const StoredTrack& t) { return juce::File (juce::String::fromUTF8 (t.record.identity.path.c_str())); }
}

juce::File DeliverPanel::rekordboxXmlFile()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DSS1 Exports").getChildFile ("DSS1 rekordbox.xml");
}

juce::File DeliverPanel::seratoFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DSS1 for Serato");
}

DeliverPanel::DeliverPanel (Crate& c) : crate (c)
{
    juce::PropertiesFile::Options options;
    options.applicationName = "DSS1";
    options.filenameSuffix = ".settings";
    options.folderName = "DSS1";
    options.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (options);

    scopeBox.onChange = [this] {
        if (scopeBox.getSelectedId() != selection)
            settings->setValue ("deliverScope", scopeBox.getSelectedId());
        refreshCounts();
    };
    chosenLabel.setColour (juce::Label::textColourId, Colours::dimText);
    chosenLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (chosenLabel);
    addAndMakeVisible (scopeBox);

    rekordboxButton.onClick = [this] { exportRekordbox(); };
    seratoButton.onClick = [this] { exportSerato(); };
    rekordboxReveal.onClick = [] { rekordboxXmlFile().revealToUser(); };
    seratoReveal.onClick = [] { seratoFolder().revealToUser(); };
    seratoGrid.setTooltip ("Off: Serato keeps its own grid (or none). On: DSS1's grid goes into the copies.");
    seratoGrid.setToggleState (settings->getBoolValue ("seratoGrid", false), juce::dontSendNotification);
    seratoGrid.onClick = [this] { settings->setValue ("seratoGrid", seratoGrid.getToggleState()); };
    for (auto* b : { &rekordboxButton, &seratoButton })
        addAndMakeVisible (b);
    addChildComponent (rekordboxReveal);
    addChildComponent (seratoReveal);
    addAndMakeVisible (seratoGrid);

    timingButton.setTooltip ("Checks that cues land on the same beat in Serato and Rekordbox, for a file you choose");
    timingButton.onClick = [] {
        juce::DialogWindow::LaunchOptions o;
        auto panel = std::make_unique<TestExportPanel>();
        panel->setSize (900, 560);
        o.content.setOwned (panel.release());
        o.dialogTitle = "Export timing check";
        o.dialogBackgroundColour = Colours::background;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = true;
        o.launchAsync();
    };
    addAndMakeVisible (timingButton);

    status.setColour (juce::Label::textColourId, Colours::text);
    status.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (status);

    rekordboxReveal.setVisible (rekordboxXmlFile().existsAsFile());
    seratoReveal.setVisible (seratoFolder().isDirectory());

    crate.addChangeListener (this);
    refreshCounts();
}

DeliverPanel::~DeliverPanel()
{
    crate.removeChangeListener (this);
}

void DeliverPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshCounts();
}

void DeliverPanel::refreshCounts()
{
    const auto all = crate.tracks();
    const auto preparedCount = static_cast<std::size_t> (std::count_if (all.begin(), all.end(), [] (const StoredTrack& t) {
        return deliver::isPrepared (t.record);
    }));
    const auto selected = scopeBox.getSelectedId() != 0 ? scopeBox.getSelectedId() : settings->getIntValue ("deliverScope", prepared);
    scopeBox.clear (juce::dontSendNotification);
    scopeBox.addItem ("Tracks you've prepared (" + juce::String (preparedCount) + ")", prepared);
    scopeBox.addItem ("All tracks (" + juce::String (all.size()) + ")", everything);
    scopeBox.addItem ("Tracks selected in Catalog (" + juce::String (crate.selection().size()) + ")", selection);
    scopeBox.setSelectedId (selected, juce::dontSendNotification);

    // Say which tracks will go: by name for a few, by count for many.
    const auto chosen = chosenTracks();
    juce::String text;
    if (chosen.empty())
        text = selected == selection ? "Select tracks in Catalog (Shift or Command for several), or right-click > Export."
                                     : "No tracks to export yet.";
    else if (chosen.size() <= 3)
    {
        juce::StringArray names;
        for (const auto& t : chosen)
            names.add (juce::String::fromUTF8 ((t.record.info.artist.empty() ? t.record.info.title
                                                                             : t.record.info.artist + " - " + t.record.info.title).c_str()));
        text = "Exporting: " + names.joinIntoString (",  ");
    }
    else
        text = "Exporting " + juce::String (chosen.size()) + " tracks.";
    chosenLabel.setText (text, juce::dontSendNotification);
}

void DeliverPanel::useSelection()
{
    scopeBox.setSelectedId (selection, juce::dontSendNotification);
    refreshCounts();
}

std::vector<StoredTrack> DeliverPanel::chosenTracks() const
{
    auto all = crate.tracks();
    const auto gone = crate.missing();
    const auto& picked = crate.selection();
    const auto scope = scopeBox.getSelectedId();
    std::erase_if (all, [&] (const StoredTrack& t) {
        if (gone.count (t.id) != 0)
            return true;
        if (scope == selection)
            return std::find (picked.begin(), picked.end(), t.id) == picked.end();
        return scope != everything && ! deliver::isPrepared (t.record);
    });
    return all;
}

void DeliverPanel::finished (const juce::String& message, const juce::File& reveal)
{
    busy = false;
    rekordboxButton.setEnabled (true);
    seratoButton.setEnabled (true);
    status.setText (message, juce::dontSendNotification);
    rekordboxReveal.setVisible (rekordboxXmlFile().existsAsFile());
    seratoReveal.setVisible (seratoFolder().isDirectory());
    juce::ignoreUnused (reveal);
}

void DeliverPanel::exportRekordbox()
{
    const auto tracks = chosenTracks();
    if (tracks.empty())
    {
        status.setText (scopeBox.getSelectedId() == selection ? "No tracks selected: select them in Catalog first."
                                                              : "Nothing to export yet: prepare a track (cues, loops or a grid) in Prep first.",
                        juce::dontSendNotification);
        return;
    }
    const auto file = rekordboxXmlFile();
    file.getParentDirectory().createDirectory();
    try
    {
        auto collection = deliver::rekordboxCollection (tracks, "DSS1");
        collection.productVersion = JUCE_APPLICATION_VERSION_STRING;
        if (! file.replaceWithText (juce::String::fromUTF8 (rekordbox::write (collection).c_str())))
            throw std::runtime_error ("couldn't write " + file.getFullPathName().toStdString());
        finished ("Exported " + plural (tracks.size(), "track", "tracks") + " to " + file.getFileName()
                      + ". Import them in Rekordbox as described above.",
                  file);
    }
    catch (const std::exception& e)
    {
        finished ("Rekordbox export failed: " + juce::String (e.what()), {});
    }
}

void DeliverPanel::exportSerato()
{
    if (busy)
        return;
    const auto tracks = chosenTracks();
    if (tracks.empty())
    {
        status.setText (scopeBox.getSelectedId() == selection ? "No tracks selected: select them in Catalog first."
                                                              : "Nothing to export yet: prepare a track (cues, loops or a grid) in Prep first.",
                        juce::dontSendNotification);
        return;
    }
    busy = true;
    rekordboxButton.setEnabled (false);
    seratoButton.setEnabled (false);
    status.setText ("Copying " + plural (tracks.size(), "track", "tracks") + " for Serato...", juce::dontSendNotification);

    serato::ExportOptions options;
    options.includeBeatGrid = seratoGrid.getToggleState();
    const auto folder = seratoFolder();

    std::thread ([weak = juce::WeakReference<DeliverPanel> (this), tracks, options, folder] {
        folder.createDirectory();
        std::vector<std::string> paths;
        for (const auto& t : tracks)
            paths.push_back (t.record.identity.path);
        const auto names = deliver::copyNames (paths);

        int copied = 0, skipped = 0, failed = 0;
        juce::String firstProblem;
        for (std::size_t i = 0; i < tracks.size(); ++i)
        {
            const auto& t = tracks[i];
            if (! deliver::canTagForSerato (t.record.identity.path))
            {
                ++skipped;
                continue;
            }
            try
            {
                juce::MemoryBlock data;
                if (! fileFor (t).loadFileAsData (data))
                    throw std::runtime_error ("couldn't read the file");
                const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
                const auto tagged = deliver::seratoTaggedCopy (deliver::Bytes (bytes, bytes + data.getSize()), t.record.identity.path,
                                                               t.record, options);
                const auto target = folder.getChildFile (juce::String::fromUTF8 (names[i].c_str()));
                if (! target.replaceWithData (tagged.data(), tagged.size()))
                    throw std::runtime_error ("couldn't write the copy");
                ++copied;
            }
            catch (const std::exception& e)
            {
                ++failed;
                if (firstProblem.isEmpty())
                    firstProblem = fileFor (t).getFileName() + ": " + e.what();
            }
            if ((i + 1) % 5 == 0)
                juce::MessageManager::callAsync ([weak, done = i + 1, total = tracks.size()] {
                    if (auto* p = weak.get())
                        p->status.setText ("Copying for Serato: " + juce::String (done) + " of " + juce::String (total) + "...",
                                           juce::dontSendNotification);
                });
        }

        juce::String message = "Copied " + plural (static_cast<std::size_t> (copied), "track", "tracks") + " to Music > "
                               + folder.getFileName() + ".";
        if (skipped > 0)
            message << " Skipped " << skipped << " FLAC (not supported for Serato yet).";
        if (failed > 0)
            message << " " << failed << " failed (" << firstProblem << ").";
        juce::MessageManager::callAsync ([weak, message, folder] {
            if (auto* p = weak.get())
                p->finished (message, folder);
        });
    }).detach();
}

void DeliverPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    auto area = getLocalBounds().reduced (24, 16);

    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("Deliver", area.removeFromTop (34), juce::Justification::centredLeft);
    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("Send your prep to Rekordbox and Serato. Your music files are never changed.", area.removeFromTop (22),
                juce::Justification::centredLeft);

    const auto card = [&] (juce::Rectangle<int> r, const juce::String& title, const char* how) {
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        auto inner = r.reduced (18, 14);
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText (title, inner.removeFromTop (26), juce::Justification::centredLeft);
        inner.removeFromTop (44);  // buttons
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (13.5f));
        g.drawFittedText (how, inner.removeFromTop (60), juce::Justification::topLeft, 4, 1.0f);
    };
    card (rekordboxCard, "Rekordbox", rekordboxHow);
    card (seratoCard, "Serato", seratoHow);
}

void DeliverPanel::resized()
{
    auto area = getLocalBounds().reduced (24, 16);
    area.removeFromTop (64);
    auto scopeRow = area.removeFromTop (30);
    scopeBox.setBounds (scopeRow.removeFromLeft (320));
    scopeRow.removeFromLeft (16);
    chosenLabel.setBounds (scopeRow);
    area.removeFromTop (16);

    const auto layoutCard = [&] (juce::Rectangle<int> r, juce::TextButton& action, juce::TextButton& reveal, juce::Component* extra) {
        auto inner = r.reduced (18, 14);
        inner.removeFromTop (30);
        auto row = inner.removeFromTop (32);
        action.setBounds (row.removeFromLeft (220));
        row.removeFromLeft (8);
        reveal.setBounds (row.removeFromLeft (130));
        if (extra != nullptr)
        {
            row.removeFromLeft (16);
            extra->setBounds (row.removeFromLeft (180));
        }
    };
    rekordboxCard = area.removeFromTop (150);
    area.removeFromTop (14);
    seratoCard = area.removeFromTop (150);
    layoutCard (rekordboxCard, rekordboxButton, rekordboxReveal, nullptr);
    layoutCard (seratoCard, seratoButton, seratoReveal, &seratoGrid);

    area.removeFromTop (14);
    status.setBounds (area.removeFromTop (28));
    timingButton.setBounds (getLocalBounds().reduced (24, 16).removeFromBottom (30).removeFromLeft (200));
}

} // namespace dss1::ui
