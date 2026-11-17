#pragma once

#include "core/LibraryStore.h"

#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace dss1::ui
{

/// The user's music library: imports files, folders and zips into the library database, then analyses each
/// track (BPM, key, loudness) in the background. Listeners hear a change message whenever tracks or status
/// change. All public methods are for the message thread.
class Crate : public juce::ChangeBroadcaster
{
public:
    Crate();
    ~Crate() override;

    /// Adds audio files, folders (searched inside) and zips (unpacked to Music/DSS1 Imports). Returns at once;
    /// the work happens in the background.
    void add (const juce::Array<juce::File>& items);

    std::vector<StoredTrack> tracks() const;
    juce::String status() const;

    /// Where the library database lives.
    static juce::File dataFolder();

    static bool isAudioFile (const juce::File& f);

private:
    void run();
    void importItems (const juce::Array<juce::File>& items);
    bool analyseNext();
    void setStatus (const juce::String& s);
    void refreshSnapshot();

    mutable std::mutex storeLock;   ///< SQLite connection: one thread at a time
    std::unique_ptr<LibraryStore> store;

    mutable std::mutex stateLock;
    std::vector<StoredTrack> snapshot;
    juce::String statusText;

    std::mutex queueLock;
    std::condition_variable wake;
    std::deque<juce::Array<juce::File>> jobs;
    bool stopping = false;
    std::thread worker;
};

} // namespace dss1::ui
