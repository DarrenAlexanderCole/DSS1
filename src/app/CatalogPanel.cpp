#include "CatalogPanel.h"
#include "Theme.h"

#include "core/TestExport.h"
#include "io/AudioDecoder.h"

#include <algorithm>
#include <thread>

namespace dss1::ui
{

namespace
{
    enum Column { title = 1, artist, time, bpm, key, loudness };

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

CatalogPanel::CatalogPanel (Crate& c, AudioEngine& a) : crate (c), audio (a), transport (a.deck())
{
    auto& header = table.getHeader();
    const auto columnFlags = juce::TableHeaderComponent::defaultFlags;
    header.addColumn ("Title", title, 340, 120, -1, columnFlags);
    header.addColumn ("Artist", artist, 220, 80, -1, columnFlags);
    header.addColumn ("Time", time, 70, 50, 120, columnFlags);
    header.addColumn ("BPM", bpm, 80, 50, 120, columnFlags);
    header.addColumn ("Key", key, 120, 60, 200, columnFlags);
    header.addColumn ("Loudness", loudness, 100, 60, 160, columnFlags);
    header.setSortColumnId (title, true);
    header.setColour (juce::TableHeaderComponent::backgroundColourId, Colours::panel);
    header.setColour (juce::TableHeaderComponent::textColourId, Colours::dimText);
    table.setColour (juce::ListBox::backgroundColourId, Colours::background);
    table.setRowHeight (28);
    table.setMultipleSelectionEnabled (false);
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

    statusLabel.setColour (juce::Label::textColourId, Colours::dimText);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    addAndMakeVisible (transport);

    if (audio.problem().isNotEmpty())
        statusLabel.setText ("Audio: " + audio.problem(), juce::dontSendNotification);

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
    sortRows();
    table.updateContent();

    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].id == selectedId)
            table.selectRow (static_cast<int> (i), true, true);

    auto status = crate.status();
    if (status.isEmpty())
        status = rows.empty() ? "Drop music here, or click Add Music: files, folders or record-pool zips."
                              : juce::String (rows.size()) + (rows.size() == 1 ? " track" : " tracks");
    if (audio.problem().isNotEmpty())
        status = "Audio: " + audio.problem() + "  |  " + status;
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
    g.setColour (column == title ? Colours::text : Colours::text.withAlpha (0.8f));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (cellText (t, column), 8, 0, width - 16, height,
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

bool CatalogPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        transport.togglePlay();
        return true;
    }
    return false;
}

void CatalogPanel::play (int row)
{
    if (row < 0 || row >= static_cast<int> (rows.size()))
        return;
    const auto track = rows[static_cast<std::size_t> (row)];
    const auto name = juce::String::fromUTF8 (track.record.info.title.c_str());
    transport.showLoading (name);
    audio.deck().pause();

    const auto generation = ++loadGeneration;
    std::thread ([weak = juce::WeakReference<CatalogPanel> (this), track, name, generation] {
        std::shared_ptr<const io::DecodedAudio> decoded;
        std::vector<float> peaks;
        juce::String problem;
        try
        {
            auto audioData = std::make_shared<io::DecodedAudio> (io::decode (track.record.identity.path));
            peaks = overviewPeaks (audioData->channels, 1400);
            decoded = std::move (audioData);
        }
        catch (const std::exception& e)
        {
            problem = e.what();
        }
        juce::MessageManager::callAsync ([weak, decoded, peaks = std::move (peaks), track, name, problem, generation]() mutable {
            auto* panel = weak.get();
            if (panel == nullptr || generation != panel->loadGeneration)
                return;  // closed, or another track was chosen meanwhile
            if (! decoded)
            {
                panel->transport.showTrack (name, problem, {});
                return;
            }
            const auto& info = track.record.info;
            juce::String details = juce::String::fromUTF8 (info.artist.c_str());
            if (info.bpm)
                details << (details.isEmpty() ? "" : "  |  ") << juce::String (*info.bpm, 1) << " BPM";
            if (! info.camelot.empty())
                details << "  |  " << info.camelot << " " << info.key;
            panel->audio.deck().load (decoded);
            panel->audio.deck().play();
            panel->transport.showTrack (name, details, std::move (peaks));
        });
    }).detach();
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
    transport.setBounds (area.removeFromBottom (130));

    auto top = area.removeFromTop (52).reduced (16, 10);
    addButton.setBounds (top.removeFromLeft (130));
    top.removeFromLeft (16);
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
