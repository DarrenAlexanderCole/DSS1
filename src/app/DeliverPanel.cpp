#include "DeliverPanel.h"
#include "TestExportPanel.h"
#include "Settings.h"
#include "Theme.h"

#include "core/Deliver.h"

#include <thread>

namespace dss1::ui
{

namespace
{
    enum Scope { prepared = 1, everything = 2, selection = 3 };

    const char* const rekordboxHow =
        "Writes one file that points at your music; your files aren't touched. "
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

juce::File DeliverPanel::rekordboxXmlFile() const
{
    const auto chosen = settings->getValue ("rekordboxXml");
    if (chosen.isNotEmpty() && juce::File::isAbsolutePath (chosen))
        return juce::File (chosen);
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DSS1 Exports").getChildFile ("DSS1 rekordbox.xml");
}

juce::File DeliverPanel::seratoFolder() const
{
    const auto chosen = settings->getValue ("seratoFolder");
    if (chosen.isNotEmpty() && juce::File::isAbsolutePath (chosen))
        return juce::File (chosen);
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DSS1 for Serato");
}

void DeliverPanel::chooseRekordboxFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Where should the Rekordbox file go?", rekordboxXmlFile(), "*.xml");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              if (! f.hasFileExtension ("xml"))
                                  f = f.withFileExtension ("xml");
                              settings->setValue ("rekordboxXml", f.getFullPathName());
                              rekordboxReveal.setVisible (rekordboxXmlFile().existsAsFile());
                              status.setText ("Rekordbox exports will go to " + f.getFullPathName(), juce::dontSendNotification);
                              repaint();
                          });
}

void DeliverPanel::chooseSeratoFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder for the Serato copies", seratoFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc) {
                              const auto dir = fc.getResult();
                              if (! dir.isDirectory())
                                  return;
                              // Copies inside a watched folder would be added to DSS1 as new tracks.
                              for (const auto& w : crate.watchedFolders())
                                  if (dir == juce::File (w) || dir.isAChildOf (juce::File (w)))
                                  {
                                      status.setText ("That folder is inside a watched folder (" + w
                                                          + "): the copies would show up in DSS1 as new tracks. Choose another.",
                                                      juce::dontSendNotification);
                                      return;
                                  }
                              settings->setValue ("seratoFolder", dir.getFullPathName());
                              seratoReveal.setVisible (dir.isDirectory());
                              status.setText ("Serato copies will go to " + dir.getFullPathName(), juce::dontSendNotification);
                              repaint();
                          });
}

DeliverPanel::DeliverPanel (Crate& c) : crate (c)
{
    settings = &appSettings();

    scopeBox.onChange = [this] {
        if (scopeBox.getSelectedId() != selection)
            settings->setValue ("deliverScope", scopeBox.getSelectedId());
        refreshCounts();
    };
    chosenLabel.setColour (juce::Label::textColourId, hw::colour::amber);
    chosenLabel.setFont (hw::vfd (22.0f));
    chosenLabel.setBorderSize ({ 0, 12, 0, 8 });
    chosenLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (chosenLabel);
    addAndMakeVisible (scopeBox);

    rekordboxButton.setButtonText ("EXPORT FOR REKORDBOX");
    seratoButton.setButtonText ("EXPORT COPIES FOR SERATO");
    rekordboxReveal.setButtonText ("SHOW IN FINDER");
    seratoReveal.setButtonText ("SHOW IN FINDER");
    timingButton.setButtonText ("EXPORT TIMING CHECK");
    seratoGrid.setButtonText ("BEAT GRID");
    hwStyle (rekordboxButton, KeyStyle::cue);
    hwStyle (seratoButton, KeyStyle::cue);
    rekordboxButton.onClick = [this] { exportRekordbox(); };
    seratoButton.onClick = [this] { exportSerato(); };
    rekordboxReveal.onClick = [this] { rekordboxXmlFile().revealToUser(); };
    seratoReveal.onClick = [this] { seratoFolder().revealToUser(); };
    rekordboxDest.setTooltip ("Choose where the Rekordbox XML file is saved");
    rekordboxDest.onClick = [this] { chooseRekordboxFile(); };
    seratoDest.setTooltip ("Choose the folder the Serato copies go to");
    seratoDest.onClick = [this] { chooseSeratoFolder(); };
    hwStyle (rekordboxDest, KeyStyle::cream);
    hwStyle (seratoDest, KeyStyle::cream);
    addAndMakeVisible (rekordboxDest);
    addAndMakeVisible (seratoDest);
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

    status.setColour (juce::Label::textColourId, hw::colour::amber);
    status.setFont (hw::vfd (22.0f));
    status.setBorderSize ({ 0, 12, 0, 8 });
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
                if (target == fileFor (t))
                    throw std::runtime_error ("the copy would replace the original, so it was skipped (choose another folder)");
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

        juce::String message = "Copied " + plural (static_cast<std::size_t> (copied), "track", "tracks") + " to "
                               + folder.getFullPathName() + ".";
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
    const auto b = getLocalBounds().toFloat();
    const auto ear = static_cast<float> (hw::RackUnit::earWidth);
    hw::faceplate (g, b.reduced (ear, 0));
    hw::rackEar (g, b.withWidth (ear), 5);
    hw::rackEar (g, b.withLeft (b.getRight() - ear), 5);

    hw::silk (g, "DELIVER", "YOUR MUSIC FILES ARE NEVER CHANGED", scopeBox.getBounds().toFloat().withWidth (500).withHeight (16).translated (0, -24));
    hw::recess (g, chosenLabel.getBounds().toFloat());

    // Two modules, each in its own sunken bay.
    const auto module = [&] (juce::Rectangle<int> r, const juce::String& title, const juce::String& sub, const char* how) {
        const auto rf = r.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (rf, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (rf.reduced (0.5f), 4.0f, 1.0f);
        auto inner = rf.reduced (22, 18);
        hw::silk (g, title, sub, inner.removeFromTop (20), juce::Justification::centredLeft, 15.0f);
        inner.removeFromTop (62);  // keys
        inner.removeFromTop (78);  // destination
        g.setColour (hw::colour::silk.withAlpha (0.7f));
        g.setFont (hw::label (15.0f, false));
        g.drawFittedText (how, inner.removeFromTop (110).toNearestInt(), juce::Justification::topLeft, 6, 1.0f);
    };
    module (rekordboxCard, "REKORDBOX", "XML COLLECTION", rekordboxHow);
    module (seratoCard, "SERATO", "TAGGED COPIES", seratoHow);

    // Destinations: where each export goes.
    const auto destination = [&] (juce::Rectangle<int> r, const juce::File& where) {
        hw::silk (g, "DESTINATION", {}, r.toFloat().withHeight (16).translated (0, -21));
        hw::recess (g, r.toFloat());
        auto path = where.getFullPathName();
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
        if (path.startsWith (home))
            path = "~" + path.substring (home.length());
        hw::glowText (g, path, r.toFloat().reduced (10, 0), hw::vfd (20.0f));
        hw::glass (g, r.toFloat());
    };
    destination (rekordboxDestArea, rekordboxXmlFile());
    destination (seratoDestArea, seratoFolder());

    hw::silk (g, "STATUS", {}, status.getBounds().toFloat().withHeight (16).translated (0, -22));
    hw::recess (g, status.getBounds().toFloat());
}

void DeliverPanel::paintOverChildren (juce::Graphics& g)
{
    hw::glass (g, chosenLabel.getBounds().toFloat());
    hw::glass (g, status.getBounds().toFloat());
}

void DeliverPanel::resized()
{
    auto area = getLocalBounds().reduced (hw::RackUnit::earWidth, 0).reduced (30, 0);
    area.removeFromTop (44);
    area.removeFromBottom (24);
    auto scopeRow = area.removeFromTop (46);
    scopeBox.setBounds (scopeRow.removeFromLeft (380));
    scopeRow.removeFromLeft (16);
    chosenLabel.setBounds (scopeRow.reduced (4, 4));
    area.removeFromTop (22);

    timingButton.setBounds (area.removeFromBottom (42).removeFromLeft (220));
    area.removeFromBottom (16);
    status.setBounds (area.removeFromBottom (46).reduced (4, 4));
    area.removeFromBottom (34);

    auto modules = area;
    rekordboxCard = modules.removeFromLeft (modules.getWidth() / 2 - 10);
    modules.removeFromLeft (20);
    seratoCard = modules;

    const auto layoutModule = [&] (juce::Rectangle<int> r, juce::TextButton& action, juce::TextButton& reveal, juce::Component* extra) {
        auto inner = r.reduced (22, 18);
        inner.removeFromTop (30);
        auto row = inner.removeFromTop (46);
        action.setBounds (row.removeFromLeft (240));
        row.removeFromLeft (10);
        reveal.setBounds (row.removeFromLeft (150));
        if (extra != nullptr)
        {
            row.removeFromLeft (20);
            extra->setBounds (row.removeFromLeft (180));
        }
    };
    layoutModule (rekordboxCard, rekordboxButton, rekordboxReveal, nullptr);
    layoutModule (seratoCard, seratoButton, seratoReveal, &seratoGrid);

    const auto layoutDest = [] (juce::Rectangle<int> r, juce::TextButton& change) {
        auto row = r.reduced (22, 18).withTrimmedTop (30 + 46 + 36).removeFromTop (40);
        change.setBounds (row.removeFromRight (110));
        row.removeFromRight (10);
        return row.reduced (4, 3);
    };
    rekordboxDestArea = layoutDest (rekordboxCard, rekordboxDest);
    seratoDestArea = layoutDest (seratoCard, seratoDest);
}

} // namespace dss1::ui
