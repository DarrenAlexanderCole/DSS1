#pragma once

#include "core/LibraryStore.h"

#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <thread>

namespace dss1::ui
{

/// The user's music library. Imports files, folders and zips into the library database, reads their tags, and
/// analyses each track (BPM, key, loudness) in the background. Folders the user adds are watched: DSS1 looks in
/// them again at launch, every few minutes and on request, adding new music and following files that moved.
/// Listeners hear a change message whenever tracks or status change. All public methods are for the message
/// thread; the work happens on one background thread, in order.
class Crate : public juce::ChangeBroadcaster
{
public:
    Crate();
    ~Crate() override;

    /// Adds audio files, folders (searched inside, then watched) and zips (unpacked to Music/DSS1 Imports).
    void add (const juce::Array<juce::File>& items);

    /// Looks in the watched folders now, and checks which tracks are missing.
    void rescan();

    /// Searches `folder` (and inside it) for tracks that are missing, by file name and length.
    void findMissingIn (const juce::File& folder);

    /// Saves a track's record (its path says which track). The snapshot updates once it's written.
    void save (const TrackRecord& record);

    /// Analyses tracks again: BPM, key, loudness and a new grid (the old grid is replaced).
    void analyseAgain (const std::vector<std::int64_t>& ids);

    /// Takes tracks out of the library. Files on disk are never touched.
    void remove (const std::vector<std::int64_t>& ids);

    void stopWatching (const juce::String& folder);

    /// The tracks selected in Catalog (message thread only). Deliver can export just these.
    void setSelection (std::vector<std::int64_t> ids);
    const std::vector<std::int64_t>& selection() const noexcept { return selected; }

    std::vector<StoredTrack> tracks() const;
    std::set<std::int64_t> missing() const;
    juce::StringArray watchedFolders() const;
    juce::String status() const;

    /// Where the library database lives.
    static juce::File dataFolder();

    static bool isAudioFile (const juce::File& f);

private:
    using Job = std::function<void()>;

    void post (Job job);
    void run();
    void importItems (const juce::Array<juce::File>& items);
    void scanWatched (bool quiet);
    void checkMissing();
    int relinkFrom (const juce::Array<juce::File>& files, juce::Array<juce::File>* unmatched);
    bool importFile (const juce::File& file);
    bool analyseNext();
    void setStatus (const juce::String& s);
    void refreshSnapshot();

    mutable std::mutex storeLock;   ///< SQLite connection: one thread at a time
    std::unique_ptr<LibraryStore> store;

    mutable std::mutex stateLock;
    std::vector<StoredTrack> snapshot;
    std::set<std::int64_t> missingIds;
    juce::StringArray watched;
    juce::String statusText;
    std::set<std::int64_t> gridTried;
    std::vector<std::int64_t> selected;  ///< message thread only  ///< worker thread only

    std::mutex queueLock;
    std::condition_variable wake;
    std::deque<Job> jobs;
    bool stopping = false;
    std::thread worker;
};

} // namespace dss1::ui
