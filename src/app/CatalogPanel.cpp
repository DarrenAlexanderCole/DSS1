#include "CatalogPanel.h"
#include "Theme.h"


#include <algorithm>

namespace dss1::ui
{

namespace
{
    enum Column { title = 1, artist, time, bpm, key, loudness, album, genre, notes };

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
    // Tight columns for the facts; NOTES takes whatever room is left (see resized).
    header.addColumn ("Title", title, 280, 120, -1, columnFlags);
    header.addColumn ("Artist", artist, 160, 80, -1, columnFlags);
    header.addColumn ("Album", album, 140, 60, -1, columnFlags);
    header.addColumn ("Genre", genre, 110, 50, -1, columnFlags);
    header.addColumn ("Time", time, 58, 50, 120, columnFlags);
    header.addColumn ("BPM", bpm, 70, 50, 120, columnFlags);
    header.addColumn ("Key", key, 136, 60, 200, columnFlags);
    header.addColumn ("Loudness", loudness, 100, 60, 160, columnFlags);
    header.addColumn ("Notes", notes, 220, 100, -1, columnFlags);
    header.setSortColumnId (title, true);
    header.setColour (juce::TableHeaderComponent::outlineColourId, juce::Colours::transparentBlack);
    header.setColour (juce::TableHeaderComponent::textColourId, hw::colour::amberMid);
    table.setColour (juce::ListBox::backgroundColourId, Colours::background);
    table.setRowHeight (30);
    table.getHeader().setSize (table.getHeader().getWidth(), 30);
    table.setMultipleSelectionEnabled (true);
    addAndMakeVisible (table);

    hwStyle (addButton, KeyStyle::cream);
    hwStyle (findButton, KeyStyle::cue);
    addButton.setButtonText ("ADD MUSIC");
    foldersButton.setButtonText ("FOLDERS");
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

    freshButton.setTooltip ("Start fresh: work from a new folder, or clear the Catalog. Your files and your prep are kept.");
    freshButton.onClick = [this] { showFreshMenu(); };
    addAndMakeVisible (freshButton);

    foldersButton.setTooltip ("Folders DSS1 watches for new music");
    foldersButton.onClick = [this] { showFoldersMenu(); };
    addAndMakeVisible (foldersButton);

    findButton.setTooltip ("Point DSS1 at a folder or drive where missing tracks are now");
    findButton.onClick = [this] { chooseMissingFolder(); };
    addChildComponent (findButton);

    statusLabel.setColour (juce::Label::textColourId, hw::colour::amber);
    statusLabel.setFont (hw::vfd (22.0f));
    statusLabel.setBorderSize ({ 0, 12, 0, 8 });
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    onNotesEdited = [this] (int row, const juce::String& text) {
        if (row >= 0 && row < static_cast<int> (rows.size()))
        {
            auto& t = rows[static_cast<std::size_t> (row)];
            t.record.info.notes = text.trim().toStdString();
            crate.setNotes (t.id, t.record.info.notes);
        }
    };

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
    findButton.setButtonText ("FIND " + juce::String (missing.size()) + " MISSING");
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
    statusLabel.setText (status.toUpperCase(), juce::dontSendNotification);
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
            case notes:    return text (x.notes) < text (y.notes);
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
        g.fillAll (hw::colour::amber.withAlpha (0.22f));
    else if (row % 2 == 1)
        g.fillAll (juce::Colour (0x0affb060));
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRect (0, g.getClipBounds().getBottom() - 1, g.getClipBounds().getWidth(), 1);
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
        case notes:  return juce::String::fromUTF8 (info.notes.c_str());
        default: return {};
    }
}

void CatalogPanel::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= static_cast<int> (rows.size()) || column == notes)
        return;  // notes are an editable cell component
    const auto& t = rows[static_cast<std::size_t> (row)];
    const bool numeric = column == time || column == bpm || column == loudness;
    const bool gone = missing.count (t.id) != 0;
    auto text = cellText (t, column);
    if (gone && column == title)
        text = "Missing:  " + text;
    const bool display = numeric || column == key;
    const auto area = juce::Rectangle<float> (8, 0, static_cast<float> (width - 16), static_cast<float> (height));
    const auto just = numeric ? juce::Justification::centredRight : juce::Justification::centredLeft;
    if (display)
    {
        // Numbers read like the display.
        g.setFont (hw::vfd (21.0f));
        g.setColour (gone ? hw::colour::amberDim : hw::colour::amber.withAlpha (0.92f));
        g.drawText (text.toUpperCase(), area, just, true);
        return;
    }
    g.setColour (gone ? Colours::dimText.withAlpha (0.7f) : column == title ? hw::colour::silk : hw::colour::silk.withAlpha (0.72f));
    g.setFont (hw::label (column == title ? 16.0f : 15.0f, column == title && ! gone));
    g.drawText (text, area, just, true);
}

namespace
{
    /// The NOTES cell: the DJ's own words, in handwriting-ish silk. Double-click (or Return when selected) to
    /// type; Return or clicking away saves, Escape cancels.
    class NotesCell : public juce::Label
    {
    public:
        explicit NotesCell (CatalogPanel& p) : owner (p)
        {
            setEditable (false, true, false);
            setFont (hw::label (15.0f, false));
            setColour (juce::Label::textColourId, hw::colour::silk.withAlpha (0.82f));
            setColour (juce::Label::textWhenEditingColourId, hw::colour::silk);
            setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (0xff1a120a));
            setColour (juce::Label::outlineWhenEditingColourId, hw::colour::amber.withAlpha (0.7f));
            setColour (juce::TextEditor::highlightColourId, hw::colour::amber.withAlpha (0.35f));
            setColour (juce::CaretComponent::caretColourId, hw::colour::amber);
            setBorderSize ({ 0, 8, 0, 8 });
            setJustificationType (juce::Justification::centredLeft);
            setMinimumHorizontalScale (1.0f);
            onTextChange = [this] {
                if (owner.onNotesEdited)
                    owner.onNotesEdited (row, getText());
            };
        }

        void show (int r, const juce::String& text, bool selected)
        {
            row = r;
            isSelected = selected;
            setText (text, juce::dontSendNotification);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            if (isBeingEdited())
                return;
            if (getText().isEmpty())
            {
                if (isSelected || isMouseOver())
                {
                    g.setColour (hw::colour::amberMid.withAlpha (0.6f));
                    g.setFont (hw::label (14.0f, false));
                    g.drawText ("+ double-click for a note", getLocalBounds().reduced (8, 0), juce::Justification::centredLeft, true);
                }
                return;
            }
            juce::Label::paint (g);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            // A click on a note still picks the row, like anywhere else in the list.
            owner.selectRowFromNotes (row, e.mods);
            juce::Label::mouseDown (e);
        }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }

    private:
        CatalogPanel& owner;
        int row = -1;
        bool isSelected = false;
    };
}

juce::Component* CatalogPanel::refreshComponentForCell (int row, int column, bool selected, juce::Component* existing)
{
    if (column != notes)
    {
        jassert (existing == nullptr);
        return nullptr;
    }
    auto* cell = dynamic_cast<NotesCell*> (existing);
    if (cell == nullptr)
    {
        delete existing;
        cell = new NotesCell (*this);
    }
    const auto text = row >= 0 && row < static_cast<int> (rows.size()) ? cellText (rows[static_cast<std::size_t> (row)], notes) : juce::String();
    if (! cell->isBeingEdited())
        cell->show (row, text, selected);
    return cell;
}

void CatalogPanel::selectRowFromNotes (int row, juce::ModifierKeys mods)
{
    if (row >= 0)
        table.selectRowsBasedOnModifierKeys (row, mods, false);
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

void CatalogPanel::showFreshMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Your files and their cues, loops and notes are always kept");
    menu.addItem ("Work From a New Folder...", [this] { chooseFolderToWorkFrom(); });
    menu.addItem ("Clear the Catalog", ! rows.empty(), false, [this] { confirmClear(); });
    const auto cleared = crate.clearedCount();
    menu.addSeparator();
    menu.addItem (cleared > 0 ? "Bring Back Cleared Tracks (" + juce::String (cleared) + ")" : juce::String ("Bring Back Cleared Tracks"), cleared > 0, false,
                  [this] { crate.bringBackCleared(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (freshButton));
}

void CatalogPanel::chooseFolderToWorkFrom()
{
    chooser = std::make_unique<juce::FileChooser> ("Work from this folder: the Catalog is cleared and shows just its music",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc) {
                              const auto folder = fc.getResult();
                              if (folder.isDirectory())
                              {
                                  table.deselectAllRows();
                                  crate.workFrom (folder);
                              }
                          });
}

void CatalogPanel::confirmClear()
{
    const auto options = juce::MessageBoxOptions()
                             .withIconType (juce::MessageBoxIconType::QuestionIcon)
                             .withTitle ("Clear the Catalog?")
                             .withMessage ("Every track leaves the Catalog and DSS1 stops watching your folders. Nothing is deleted: "
                                           "your files stay where they are, and a track's cues, loops, grid and notes come back when "
                                           "you add it again (or with Bring Back Cleared Tracks).")
                             .withButton ("Clear", 1)
                             .withButton ("Cancel", 0)
                             .withAssociatedComponent (this);
    messageBox = juce::AlertWindow::showScopedAsync (options, [weak = juce::WeakReference<CatalogPanel> (this)] (int result) {
        if (result == 1 && weak != nullptr)
        {
            weak->table.deselectAllRows();
            weak->crate.clearCatalog();
        }
    });
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
        session.playPressed();
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
    const auto b = getLocalBounds().toFloat();
    const auto ear = static_cast<float> (hw::RackUnit::earWidth);
    hw::faceplate (g, b.reduced (ear, 0));
    hw::rackEar (g, b.withWidth (ear), 5);
    hw::rackEar (g, b.withLeft (b.getRight() - ear), 5);
    g.setColour (juce::Colours::black);
    g.fillRect (b.withTop (b.getBottom() - 1.0f));

    hw::silk (g, "LIBRARY", "CATALOG", addButton.getBounds().toFloat().withWidth (300).withHeight (16).translated (0, -22));
    hw::silk (g, "STATUS", {}, statusLabel.getBounds().toFloat().withHeight (16).translated (0, -22));
    hw::recess (g, statusLabel.getBounds().toFloat());
    hw::recess (g, table.getBounds().toFloat());
}

void CatalogPanel::paintOverChildrenGlass (juce::Graphics& g)
{
    hw::glass (g, statusLabel.getBounds().toFloat());
}

void CatalogPanel::paintOverChildren (juce::Graphics& g)
{
    paintOverChildrenGlass (g);
    if (! dragOver && ! rows.empty())
        return;
    const auto area = table.getBounds().reduced (24).toFloat();
    if (dragOver)
    {
        g.setColour (Colours::accent.withAlpha (0.12f));
        g.fillRoundedRectangle (area, 12.0f);
    }
    g.setColour (dragOver ? hw::colour::amber : hw::colour::amberDim);
    g.drawRoundedRectangle (area, 12.0f, 1.5f);
    hw::glowText (g, dragOver ? "DROP TO ADD TO YOUR CRATE" : "DROP MUSIC HERE: FILES, FOLDERS OR RECORD-POOL ZIPS",
                  area, hw::vfd (28.0f), dragOver ? hw::colour::amber : hw::colour::amberMid, juce::Justification::centred);
}

void CatalogPanel::resized()
{
    auto area = getLocalBounds().reduced (hw::RackUnit::earWidth, 0).reduced (30, 0);
    area.removeFromTop (38);
    area.removeFromBottom (22);

    auto top = area.removeFromTop (44);
    addButton.setBounds (top.removeFromLeft (130));
    top.removeFromLeft (8);
    foldersButton.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (8);
    freshButton.setBounds (top.removeFromLeft (140));
    top.removeFromLeft (8);
    if (findButton.isVisible())
    {
        findButton.setBounds (top.removeFromLeft (180));
        top.removeFromLeft (8);
    }
    top.removeFromLeft (24);
    statusLabel.setBounds (top.reduced (4, 3));

    area.removeFromTop (18);
    table.setBounds (area.reduced (4));
    // NOTES fills what the other columns leave.
    auto& header = table.getHeader();
    int used = 0;
    for (int i = 0; i < header.getNumColumns (true); ++i)
        if (const auto id = header.getColumnIdOfIndex (i, true); id != notes)
            used += header.getColumnWidth (id);
    const auto spare = table.getWidth() - used - table.getVerticalScrollBar().getWidth() - 4;
    header.setColumnWidth (notes, juce::jmax (160, spare));
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
