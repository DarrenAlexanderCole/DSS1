#include "CatalogPanel.h"
#include "Theme.h"


#include <algorithm>

namespace dss1::ui
{

namespace
{
    enum Column { title = 1, artist, time, bpm, key, loudness, album, genre };

    double seconds (const StoredTrack& t)
    {
        return t.record.sampleRate > 0 ? static_cast<double> (t.record.lengthFrames) / t.record.sampleRate : 0.0;
    }

    /// "4:05" for the table (no milliseconds).
    juce::String minutes (double s)
    {
        const auto total = static_cast<int> (s + 0.5);
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }

    /// Sorts Camelot codes the way the wheel goes: 1A, 1B, 2A … 12B.
    int camelotOrder (const std::string& code)
    {
        if (code.size() < 2)
            return 1000;
        return std::atoi (code.c_str()) * 2 + (code.back() == 'B' ? 1 : 0);
    }
}

CatalogPanel::CatalogPanel (Session& s) : session (s), crate (s.library())
{
    auto& header = table.getHeader();
    const auto columnFlags = juce::TableHeaderComponent::defaultFlags;
    header.addColumn ("Title", title, 300, 120, -1, columnFlags);
    header.addColumn ("Artist", artist, 200, 80, -1, columnFlags);
    header.addColumn ("Album", album, 160, 60, -1, columnFlags);
    header.addColumn ("Genre", genre, 110, 50, -1, columnFlags);
    header.addColumn ("Time", time, 70, 50, 120, columnFlags);
    header.addColumn ("BPM", bpm, 80, 50, 120, columnFlags);
    header.addColumn ("Key", key, 120, 60, 200, columnFlags);
    header.addColumn ("Loudness", loudness, 100, 60, 160, columnFlags);
    header.setSortColumnId (title, true);
    header.setColour (juce::TableHeaderComponent::backgroundColourId, Colours::panel);
    header.setColour (juce::TableHeaderComponent::textColourId, Colours::dimText);
    table.setColour (juce::ListBox::backgroundColourId, Colours::background);
    table.setRowHeight (28);
    table.setMultipleSelectionEnabled (true);
    addAndMakeVisible (table);

    addButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Add music: files, folders or zips",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectDirectories
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc) {
                                  if (! fc.getResults().isEmpty())
                                      crate.add (fc.getResults());
                              });
    };
    addAndMakeVisible (addButton);

    foldersButton.setTooltip ("Folders DSS1 watches for new music");
    foldersButton.onClick = [this] { showFoldersMenu(); };
    addAndMakeVisible (foldersButton);

    findButton.setTooltip ("Point DSS1 at a folder or drive where missing tracks are now");
    findButton.onClick = [this] { chooseMissingFolder(); };
    addChildComponent (findButton);

    statusLabel.setColour (juce::Label::textColourId, Colours::dimText);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    crate.addChangeListener (this);
    setWantsKeyboardFocus (true);
    refreshRows();
}

CatalogPanel::~CatalogPanel()
{
    crate.removeChangeListener (this);
}

void CatalogPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshRows();
}

void CatalogPanel::refreshRows()
{
    // Keep the selection on the same track across refreshes.
    std::int64_t selectedId = -1;
    if (const auto row = table.getSelectedRow(); row >= 0 && row < static_cast<int> (rows.size()))
        selectedId = rows[static_cast<std::size_t> (row)].id;

    rows = crate.tracks();
    missing = crate.missing();
    findButton.setVisible (! missing.empty());
    findButton.setButtonText ("Find " + juce::String (missing.size()) + " Missing...");
    resized();
    sortRows();
    table.updateContent();

    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].id == selectedId)
            table.selectRow (static_cast<int> (i), true, true);

    auto status = crate.status();
    if (status.isEmpty())
        status = rows.empty() ? "Drop music here, or click Add Music: files, folders or record-pool zips."
                              : juce::String (rows.size()) + (rows.size() == 1 ? " track" : " tracks");
    statusLabel.setText (status, juce::dontSendNotification);
    repaint();
}

void CatalogPanel::sortRows()
{
    const auto text = [] (const std::string& v) { return juce::String::fromUTF8 (v.c_str()).toLowerCase(); };
    const auto less = [&] (const StoredTrack& a, const StoredTrack& b) {
        const auto& x = a.record.info;
        const auto& y = b.record.info;
        switch (sortColumn)
        {
            case artist:   return text (x.artist) < text (y.artist);
            case album:    return text (x.album) < text (y.album);
            case genre:    return text (x.genre) < text (y.genre);
            case time:     return seconds (a) < seconds (b);
            case bpm:      return x.bpm.value_or (0) < y.bpm.value_or (0);
            case key:      return camelotOrder (x.camelot) < camelotOrder (y.camelot);
            case loudness: return x.loudnessLufs.value_or (-100) < y.loudnessLufs.value_or (-100);
            default:       return text (x.title) < text (y.title);
        }
    };
    std::stable_sort (rows.begin(), rows.end(), [&] (const StoredTrack& a, const StoredTrack& b) {
        return sortAscending ? less (a, b) : less (b, a);
    });
}

int CatalogPanel::getNumRows()
{
    return static_cast<int> (rows.size());
}

void CatalogPanel::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    if (selected)
        g.fillAll (Colours::accent.withAlpha (0.35f));
    else if (row % 2 == 1)
        g.fillAll (Colours::panel.withAlpha (0.5f));
}

juce::String CatalogPanel::cellText (const StoredTrack& t, int column) const
{
    const auto& info = t.record.info;
    switch (column)
    {
        case title:  return juce::String::fromUTF8 (info.title.c_str());
        case artist: return juce::String::fromUTF8 (info.artist.c_str());
        case album:  return juce::String::fromUTF8 (info.album.c_str());
        case genre:  return juce::String::fromUTF8 (info.genre.c_str());
        case time:   return minutes (seconds (t));
        case bpm:    return info.bpm ? juce::String (*info.bpm, 1) : (info.analysed ? "-" : "...");
        case key:
            if (info.camelot.empty())
                return info.analysed ? "-" : "...";
            return juce::String (info.camelot) + "  " + juce::String (info.key);
        case loudness:
            return info.loudnessLufs ? juce::String (*info.loudnessLufs, 1) + " LUFS" : (info.analysed ? "-" : "...");
        default: return {};
    }
}

void CatalogPanel::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= static_cast<int> (rows.size()))
        return;
    const auto& t = rows[static_cast<std::size_t> (row)];
    const bool numeric = column == time || column == bpm || column == loudness;
    const bool gone = missing.count (t.id) != 0;
    auto text = cellText (t, column);
    if (gone && column == title)
        text = "Missing:  " + text;
    g.setColour (gone ? Colours::dimText.withAlpha (0.7f) : column == title ? Colours::text : Colours::text.withAlpha (0.8f));
    g.setFont (juce::FontOptions (14.0f, gone && column == title ? juce::Font::italic : juce::Font::plain));
    g.drawText (text, 8, 0, width - 16, height,
                numeric ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
}

void CatalogPanel::sortOrderChanged (int column, bool ascending)
{
    sortColumn = column;
    sortAscending = ascending;
    refreshRows();
}

void CatalogPanel::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    play (row);
}

void CatalogPanel::returnKeyPressed (int row)
{
    play (row);
}

void CatalogPanel::selectedRowsChanged (int)
{
    crate.setSelection (selectedIds());
}

void CatalogPanel::deleteKeyPressed (int)
{
    removeSelected();
}

std::vector<std::int64_t> CatalogPanel::selectedIds() const
{
    std::vector<std::int64_t> ids;
    const auto selection = table.getSelectedRows();
    for (int i = 0; i < selection.size(); ++i)
        if (const auto row = selection[i]; row >= 0 && row < static_cast<int> (rows.size()))
            ids.push_back (rows[static_cast<std::size_t> (row)].id);
    return ids;
}

void CatalogPanel::removeSelected()
{
    const auto ids = selectedIds();
    if (ids.empty())
        return;
    const auto text = ids.size() == 1 ? juce::String ("Remove this track from DSS1?")
                                      : "Remove " + juce::String (ids.size()) + " tracks from DSS1?";
    const auto options = juce::MessageBoxOptions()
                             .withIconType (juce::MessageBoxIconType::QuestionIcon)
                             .withTitle (text)
                             .withMessage ("Their cues, loops and grids in DSS1 are deleted. The audio files stay where they are.")
                             .withButton ("Remove", 1)
                             .withButton ("Cancel", 0)
                             .withAssociatedComponent (this);
    messageBox = juce::AlertWindow::showScopedAsync (options, [weak = juce::WeakReference<CatalogPanel> (this), ids] (int result) {
        if (result == 1 && weak != nullptr)
            weak->crate.remove (ids);
    });
}

void CatalogPanel::cellClicked (int row, int, const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || row < 0 || row >= static_cast<int> (rows.size()))
        return;
    if (! table.isRowSelected (row))
        table.selectRow (row);
    const auto track = rows[static_cast<std::size_t> (row)];
    const juce::File file (juce::String::fromUTF8 (track.record.identity.path.c_str()));
    const bool gone = missing.count (track.id) != 0;
    const auto count = table.getNumSelectedRows();

    juce::PopupMenu menu;
    menu.addItem ("Play", ! gone, false, [this, row] { play (row); });
   #if JUCE_MAC
    menu.addItem ("Show in Finder", ! gone, false, [file] { file.revealToUser(); });
   #else
    menu.addItem ("Show in Folder", ! gone, false, [file] { file.revealToUser(); });
   #endif
    if (gone)
        menu.addItem ("Find Missing Tracks...", [this] { chooseMissingFolder(); });
    menu.addSeparator();
    menu.addItem (count > 1 ? "Export " + juce::String (count) + " Tracks..." : juce::String ("Export This Track..."), [this] {
        crate.setSelection (selectedIds());
        if (onExportSelected)
            onExportSelected();
    });
    menu.addItem (count > 1 ? "Analyse " + juce::String (count) + " Tracks Again (New Grids)" : juce::String ("Analyse Again (New Grid)"),
                  [this] { crate.analyseAgain (selectedIds()); });
    menu.addItem (count > 1 ? "Remove " + juce::String (count) + " Tracks from DSS1..." : juce::String ("Remove from DSS1..."),
                  [this] { removeSelected(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void CatalogPanel::showFoldersMenu()
{
    const auto folders = crate.watchedFolders();
    juce::PopupMenu menu;
    if (folders.isEmpty())
        menu.addItem ("No watched folders yet. Add a folder and DSS1 keeps an eye on it.", false, false, [] {});
    for (const auto& folder : folders)
    {
        juce::PopupMenu sub;
        const juce::File dir (folder);
       #if JUCE_MAC
        sub.addItem ("Show in Finder", dir.isDirectory(), false, [dir] { dir.revealToUser(); });
       #else
        sub.addItem ("Show in Folder", dir.isDirectory(), false, [dir] { dir.revealToUser(); });
       #endif
        sub.addItem ("Stop Watching", [this, folder] { crate.stopWatching (folder); });
        menu.addSubMenu (dir.isDirectory() ? folder : folder + "  (not connected)", sub);
    }
    menu.addSeparator();
    menu.addItem ("Look for New Music Now", [this] { crate.rescan(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (foldersButton));
}

void CatalogPanel::chooseMissingFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Where are the missing tracks now? Choose a folder or drive.",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc) {
                              if (fc.getResult().isDirectory())
                                  crate.findMissingIn (fc.getResult());
                          });
}

bool CatalogPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        if (session.deck().isPlaying())
            session.deck().pause();
        else
            session.deck().play();
        return true;
    }
    return false;
}

void CatalogPanel::play (int row)
{
    if (row < 0 || row >= static_cast<int> (rows.size()))
        return;
    const auto& track = rows[static_cast<std::size_t> (row)];
    if (missing.count (track.id) != 0)
    {
        chooseMissingFolder();
        return;
    }
    session.load (track, true);
}

void CatalogPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
}

void CatalogPanel::paintOverChildren (juce::Graphics& g)
{
    if (! dragOver && ! rows.empty())
        return;
    const auto area = table.getBounds().reduced (24).toFloat();
    if (dragOver)
    {
        g.setColour (Colours::accent.withAlpha (0.12f));
        g.fillRoundedRectangle (area, 12.0f);
    }
    g.setColour (dragOver ? Colours::accent : Colours::dimText);
    g.drawRoundedRectangle (area, 12.0f, 1.5f);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText (dragOver ? "Drop to add to your crate" : "Drop music here: files, folders or record-pool zips",
                area.toNearestInt(), juce::Justification::centred);
}

void CatalogPanel::resized()
{
    auto area = getLocalBounds();

    auto top = area.removeFromTop (52).reduced (16, 10);
    addButton.setBounds (top.removeFromLeft (130));
    top.removeFromLeft (8);
    foldersButton.setBounds (top.removeFromLeft (90));
    top.removeFromLeft (8);
    if (findButton.isVisible())
    {
        findButton.setBounds (top.removeFromRight (170));
        top.removeFromRight (8);
    }
    top.removeFromLeft (8);
    statusLabel.setBounds (top);

    table.setBounds (area.reduced (8, 0));
}

void CatalogPanel::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void CatalogPanel::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void CatalogPanel::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    juce::Array<juce::File> items;
    for (const auto& f : files)
        items.add (juce::File (f));
    crate.add (items);
}

} // namespace dss1::ui
