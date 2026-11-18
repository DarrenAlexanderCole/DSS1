#include "Crate.h"

#include "core/Analysis.h"
#include "core/Hits.h"
#include "core/TrackMatcher.h"
#include "io/AudioDecoder.h"
#include "io/TagReader.h"

#include <chrono>

namespace dss1::ui
{

namespace
{
    /// "Artist - Title.mp3" → artist and title; otherwise the whole name is the title.
    void titleFromFileName (const juce::File& file, TrackInfo& info)
    {
        const auto name = file.getFileNameWithoutExtension();
        const auto dash = name.indexOf (" - ");
        if (dash > 0)
        {
            info.artist = name.substring (0, dash).trim().toStdString();
            info.title = name.substring (dash + 3).trim().toStdString();
        }
        else
        {
            info.artist.clear();
            info.title = name.toStdString();
        }
    }

    /// Title, artist, album and genre from the file's tags; the file name fills in a missing title or artist.
    void readNames (const juce::File& file, TrackInfo& info)
    {
        TrackInfo fromName;
        titleFromFileName (file, fromName);
        const auto tags = io::readTags (file.getFullPathName().toStdString());
        info.title = tags.title.empty() ? fromName.title : tags.title;
        info.artist = tags.artist.empty() ? fromName.artist : tags.artist;
        info.album = tags.album;
        info.genre = tags.genre;
    }

    /// True if the names look like they came from the file name only (test builds 1-6 didn't read tags).
    bool namesFromFileNameOnly (const juce::File& file, const TrackInfo& info)
    {
        TrackInfo fromName;
        titleFromFileName (file, fromName);
        return info.album.empty() && info.genre.empty() && info.title == fromName.title && info.artist == fromName.artist;
    }

    juce::String plural (std::size_t n, const char* one, const char* many)
    {
        return juce::String (n) + " " + (n == 1 ? one : many);
    }

    constexpr auto rescanEvery = std::chrono::minutes (3);

    /// Unpacks the audio files in a zip into Music/DSS1 Imports/<zip name>/ and returns them. Only file names
    /// are used (folders inside the zip are flattened), so nothing can be written outside that folder.
    juce::Array<juce::File> unzip (const juce::File& zipFile)
    {
        juce::Array<juce::File> out;
        juce::ZipFile zip (zipFile);
        const auto target = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                .getChildFile ("DSS1 Imports")
                                .getChildFile (juce::File::createLegalFileName (zipFile.getFileNameWithoutExtension()));
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto* entry = zip.getEntry (i);
            if (entry == nullptr || entry->isSymbolicLink)
                continue;
            const auto name = juce::File::createLegalFileName (entry->filename.fromLastOccurrenceOf ("/", false, false));
            if (name.isEmpty() || name.startsWith (".") || ! Crate::isAudioFile (juce::File ("/" + name)))
                continue;
            target.createDirectory();
            const auto destination = target.getChildFile (name);
            if (! destination.existsAsFile())
            {
                const std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
                juce::FileOutputStream file (destination);
                if (in == nullptr || ! file.openedOk())
                    continue;
                file.writeFromInputStream (*in, -1);
            }
            out.add (destination);
        }
        return out;
    }
} // namespace

juce::File Crate::dataFolder()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/DSS1");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("DSS1");
   #endif
}

bool Crate::isAudioFile (const juce::File& f)
{
    return ! f.getFileName().startsWith (".") && io::formatForExtension (f.getFileExtension().toStdString()).has_value();
}

Crate::Crate()
{
    dataFolder().createDirectory();
    try
    {
        store = std::make_unique<LibraryStore> (LibraryStore::open (dataFolder().getChildFile ("library.sqlite").getFullPathName().toStdString()));
    }
    catch (const std::exception& e)
    {
        // Keep working for this session; nothing will be saved.
        store = std::make_unique<LibraryStore> (LibraryStore::inMemory());
        statusText = "Couldn't open the library (" + juce::String (e.what()) + "). Changes won't be saved.";
    }
    for (const auto& folder : store->watchedFolders())
        watched.add (juce::String::fromUTF8 (folder.c_str()));
    refreshSnapshot();
    worker = std::thread ([this] { run(); });
    rescan();
}

Crate::~Crate()
{
    {
        const std::lock_guard lock (queueLock);
        stopping = true;
    }
    wake.notify_all();
    worker.join();
}

void Crate::post (Job job)
{
    {
        const std::lock_guard lock (queueLock);
        jobs.push_back (std::move (job));
    }
    wake.notify_all();
}

void Crate::add (const juce::Array<juce::File>& items)
{
    post ([this, items] { importItems (items); });
}

void Crate::rescan()
{
    post ([this] { scanWatched (false); });
}

void Crate::findMissingIn (const juce::File& folder)
{
    post ([this, folder] {
        checkMissing();
        const auto before = missing().size();
        if (before == 0)
        {
            setStatus ("No tracks are missing.");
            return;
        }
        setStatus ("Looking in " + folder.getFileName() + " for " + plural (before, "missing track", "missing tracks") + "...");
        juce::Array<juce::File> files;
        for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles))
            if (isAudioFile (entry.getFile()))
                files.add (entry.getFile());
        const auto found = relinkFrom (files, nullptr);
        refreshSnapshot();
        const auto after = missing().size();
        juce::String text = "Found " + juce::String (found) + " of " + plural (before, "missing track.", "missing tracks.");
        if (after > 0)
            text << " " << plural (after, "is", "are") << " still missing.";
        setStatus (text);
    });
}

void Crate::save (const TrackRecord& record)
{
    post ([this, record] {
        try
        {
            const std::lock_guard lock (storeLock);
            store->save (record);
        }
        catch (const std::exception& e)
        {
            setStatus ("Couldn't save " + juce::String::fromUTF8 (record.info.title.c_str()) + ": " + e.what());
            return;
        }
        // Update just this track in the snapshot (re-reading the whole library for every cue would be slow).
        {
            const std::lock_guard lock (stateLock);
            for (auto& t : snapshot)
                if (t.record.identity.path == record.identity.path)
                    t.record = record;
        }
        sendChangeMessage();
    });
}

void Crate::analyseAgain (const std::vector<std::int64_t>& ids)
{
    post ([this, ids] {
        {
            const std::lock_guard lock (storeLock);
            store->transaction ([&] {
                for (const auto id : ids)
                    if (auto t = store->byId (id))
                    {
                        t->record.info.analysed = false;
                        t->record.grid.reset();
                        store->save (t->record);
                    }
            });
        }
        refreshSnapshot();
        setStatus ("Analysing " + plural (ids.size(), "track", "tracks") + " again...");
    });
}

void Crate::remove (const std::vector<std::int64_t>& ids)
{
    post ([this, ids] {
        {
            const std::lock_guard lock (storeLock);
            store->transaction ([&] {
                for (const auto id : ids)
                    store->remove (id);
            });
        }
        refreshSnapshot();
        setStatus ("Removed " + plural (ids.size(), "track", "tracks") + " from DSS1. The files are still on disk.");
    });
}

void Crate::stopWatching (const juce::String& folder)
{
    post ([this, folder] {
        {
            const std::lock_guard lock (storeLock);
            store->removeWatchedFolder (folder.toStdString());
        }
        {
            const std::lock_guard lock (stateLock);
            watched.removeString (folder);
        }
        setStatus ("Stopped watching " + folder + ". Its tracks stay in DSS1.");
    });
}

void Crate::setSelection (std::vector<std::int64_t> ids)
{
    if (ids == selected)
        return;
    selected = std::move (ids);
    sendChangeMessage();
}

std::vector<StoredTrack> Crate::tracks() const
{
    const std::lock_guard lock (stateLock);
    return snapshot;
}

std::set<std::int64_t> Crate::missing() const
{
    const std::lock_guard lock (stateLock);
    return missingIds;
}

juce::StringArray Crate::watchedFolders() const
{
    const std::lock_guard lock (stateLock);
    return watched;
}

juce::String Crate::status() const
{
    const std::lock_guard lock (stateLock);
    return statusText;
}

void Crate::setStatus (const juce::String& s)
{
    {
        const std::lock_guard lock (stateLock);
        statusText = s;
    }
    sendChangeMessage();
}

void Crate::refreshSnapshot()
{
    std::vector<StoredTrack> all;
    {
        const std::lock_guard lock (storeLock);
        all = store->all();
    }
    {
        const std::lock_guard lock (stateLock);
        snapshot = std::move (all);
        // Forget missing marks for tracks that are gone or relinked; checkMissing() sets new ones.
        std::set<std::int64_t> stillMissing;
        for (const auto& t : snapshot)
            if (missingIds.count (t.id) != 0 && ! juce::File (juce::String::fromUTF8 (t.record.identity.path.c_str())).existsAsFile())
                stillMissing.insert (t.id);
        missingIds = std::move (stillMissing);
    }
    sendChangeMessage();
}

void Crate::run()
{
    auto nextScan = std::chrono::steady_clock::now() + rescanEvery;
    for (;;)
    {
        Job job;
        {
            std::unique_lock lock (queueLock);
            if (stopping)
                return;
            if (! jobs.empty())
            {
                job = std::move (jobs.front());
                jobs.pop_front();
            }
        }

        if (job)
        {
            job();
            continue;
        }

        if (std::chrono::steady_clock::now() >= nextScan)
        {
            nextScan = std::chrono::steady_clock::now() + rescanEvery;
            scanWatched (true);
            continue;
        }

        if (analyseNext())
            continue;

        std::unique_lock lock (queueLock);
        wake.wait_until (lock, nextScan, [this] { return stopping || ! jobs.empty(); });
    }
}

bool Crate::importFile (const juce::File& file)
{
    const auto path = file.getFullPathName().toStdString();
    try
    {
        const auto info = io::probe (path);
        TrackRecord record;
        record.identity.path = path;
        readNames (file, record.info);
        record.sampleRate = info.sampleRate;
        record.lengthFrames = info.lengthFrames;
        record.exportOffsetFrames = info.encoderDelayFrames;  // Serato and Rekordbox count it (see TrackRecord)
        const std::lock_guard lock (storeLock);
        store->save (record);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

void Crate::importItems (const juce::Array<juce::File>& items)
{
    juce::Array<juce::File> files;
    int skipped = 0;
    for (const auto& item : items)
    {
        if (item.isDirectory())
        {
            const auto folder = item.getFullPathName();
            {
                const std::lock_guard lock (storeLock);
                store->addWatchedFolder (folder.toStdString());
            }
            {
                const std::lock_guard lock (stateLock);
                watched.addIfNotAlreadyThere (folder);
                watched.sort (true);
            }
            for (const auto& entry : juce::RangedDirectoryIterator (item, true, "*", juce::File::findFiles))
                if (isAudioFile (entry.getFile()))
                    files.add (entry.getFile());
        }
        else if (item.hasFileExtension ("zip"))
        {
            setStatus ("Unpacking " + item.getFileName() + "...");
            files.addArray (unzip (item));
        }
        else if (isAudioFile (item))
        {
            files.add (item);
        }
        else
        {
            ++skipped;
        }
    }

    // Leave out what's already in the crate; files that are missing tracks moved somewhere new are relinked.
    juce::Array<juce::File> fresh;
    for (const auto& file : files)
    {
        const std::lock_guard lock (storeLock);
        if (! store->byPath (file.getFullPathName().toStdString()))
            fresh.add (file);
    }
    checkMissing();
    juce::Array<juce::File> unmatched;
    const auto relinked = relinkFrom (fresh, &unmatched);

    int added = 0, failed = 0;
    for (int i = 0; i < unmatched.size(); ++i)
    {
        const auto& file = unmatched.getReference (i);
        setStatus ("Adding " + juce::String (i + 1) + " of " + juce::String (unmatched.size()) + ": " + file.getFileName());
        if (importFile (file))
            ++added;
        else
            ++failed;
    }
    refreshSnapshot();

    juce::String summary = "Added " + plural (static_cast<std::size_t> (added), "track.", "tracks.");
    if (relinked > 0)
        summary << " Found " << plural (static_cast<std::size_t> (relinked), "missing track.", "missing tracks.");
    if (failed > 0)
        summary << " " << failed << " couldn't be read.";
    if (skipped > 0)
        summary << " " << skipped << " weren't audio DSS1 reads yet (WAV, AIFF, FLAC, MP3).";
    setStatus (summary);
}

void Crate::scanWatched (bool quiet)
{
    checkMissing();

    // Tracks added before DSS1 read tags get their tags now (cheap: only the tag bytes are read).
    for (const auto& t : tracks())
    {
        const juce::File file (juce::String::fromUTF8 (t.record.identity.path.c_str()));
        if (! file.existsAsFile() || ! namesFromFileNameOnly (file, t.record.info))
            continue;
        auto record = t.record;
        readNames (file, record.info);
        if (record.info != t.record.info)
        {
            const std::lock_guard lock (storeLock);
            store->save (record);
        }
    }

    juce::Array<juce::File> fresh;
    for (const auto& folder : watchedFolders())
    {
        const juce::File dir (folder);
        if (! dir.isDirectory())
            continue;  // unplugged drive: try again next time
        for (const auto& entry : juce::RangedDirectoryIterator (dir, true, "*", juce::File::findFiles))
        {
            if (! isAudioFile (entry.getFile()))
                continue;
            const std::lock_guard lock (storeLock);
            if (! store->byPath (entry.getFile().getFullPathName().toStdString()))
                fresh.add (entry.getFile());
        }
    }

    juce::Array<juce::File> unmatched;
    const auto relinked = relinkFrom (fresh, &unmatched);
    int added = 0;
    for (const auto& file : unmatched)
        if (importFile (file))
            ++added;
    refreshSnapshot();

    if (added > 0 || relinked > 0)
    {
        juce::String text;
        if (added > 0)
            text << "Added " << plural (static_cast<std::size_t> (added), "new track", "new tracks") << " from watched folders. ";
        if (relinked > 0)
            text << "Found " << plural (static_cast<std::size_t> (relinked), "moved track.", "moved tracks.");
        setStatus (text.trim());
    }
    else if (! quiet)
    {
        const auto gone = missing().size();
        setStatus (gone > 0 ? plural (gone, "track is missing.", "tracks are missing.") + " Use Find Missing to point DSS1 at them."
                            : juce::String());
    }
}

void Crate::checkMissing()
{
    std::set<std::int64_t> gone;
    for (const auto& t : tracks())
        if (! juce::File (juce::String::fromUTF8 (t.record.identity.path.c_str())).existsAsFile())
            gone.insert (t.id);
    bool changed = false;
    {
        const std::lock_guard lock (stateLock);
        changed = gone != missingIds;
        missingIds = std::move (gone);
    }
    if (changed)
        sendChangeMessage();
}

int Crate::relinkFrom (const juce::Array<juce::File>& files, juce::Array<juce::File>* unmatched)
{
    // Candidates: the missing tracks. A file only counts as one of them if it has the same file name (accents and
    // case aside) and the same length, so a different track with a common name ("Intro.mp3") isn't taken for it.
    std::vector<MatchCandidate> candidates;
    std::set<std::string> names;
    {
        const auto gone = missing();
        for (const auto& t : tracks())
            if (gone.count (t.id) != 0)
            {
                const auto& r = t.record;
                candidates.push_back ({ t.id, r.identity.path,
                                        r.sampleRate > 0 ? std::optional<double> (static_cast<double> (r.lengthFrames) / r.sampleRate) : std::nullopt });
                names.insert (looseKey (juce::File (juce::String::fromUTF8 (r.identity.path.c_str())).getFileName().toStdString()));
            }
    }

    int relinked = 0;
    for (const auto& file : files)
    {
        const auto path = file.getFullPathName().toStdString();
        if (candidates.empty() || names.count (looseKey (file.getFileName().toStdString())) == 0)
        {
            if (unmatched != nullptr)
                unmatched->add (file);
            continue;
        }
        std::optional<double> length;
        try
        {
            const auto info = io::probe (path);
            length = static_cast<double> (info.lengthFrames) / info.sampleRate;
        }
        catch (const std::exception&) {}

        const auto result = matchTrack ({ path, length }, candidates);
        // Only same-file moves: a converted copy (MP3 → WAV) has other frame positions and isn't the same file.
        if (result.isMatch() && (result.kind == MatchKind::sameFileName || result.kind == MatchKind::samePath))
        {
            const auto id = result.ids.front();
            try
            {
                const std::lock_guard lock (storeLock);
                store->relink (id, path);
                ++relinked;
                candidates.erase (std::remove_if (candidates.begin(), candidates.end(), [id] (const MatchCandidate& c) { return c.id == id; }),
                                  candidates.end());
                continue;
            }
            catch (const std::exception&) {}
        }
        if (unmatched != nullptr)
            unmatched->add (file);
    }
    if (relinked > 0)
        refreshSnapshot();
    return relinked;
}

bool Crate::analyseNext()
{
    std::optional<StoredTrack> next;
    std::size_t remaining = 0;
    {
        const std::lock_guard lock (stateLock);
        for (const auto& t : snapshot)
            if ((! t.record.info.analysed || (t.record.info.bpm && ! t.record.grid && gridTried.count (t.id) == 0))
                && missingIds.count (t.id) == 0)
            {
                if (! next)
                    next = t;
                ++remaining;
            }
    }
    if (! next)
        return false;

    auto record = next->record;
    gridTried.insert (next->id);  // tracks analysed before DSS1 placed beats get one more pass, once
    if (! juce::File (juce::String::fromUTF8 (record.identity.path.c_str())).existsAsFile())
    {
        // Gone since the last check: mark it missing and analyse it once it's found.
        {
            const std::lock_guard lock (stateLock);
            missingIds.insert (next->id);
        }
        sendChangeMessage();
        return true;
    }
    setStatus ("Analysing " + juce::String (remaining) + (remaining == 1 ? " track" : " tracks") + ": "
               + juce::String::fromUTF8 (record.info.title.c_str()));
    try
    {
        const auto audio = io::decode (record.identity.path);
        const auto mono = mixDown (audio.channels);
        const auto rate = audio.info.sampleRate;
        record.info.bpm = analysis::detectBpm (mono, rate);
        // A first grid from the analysis, unless the track already has one (set by hand or imported).
        if (record.info.bpm && ! record.grid)
            if (const auto beats = analysis::placeBeats (mono, rate, *record.info.bpm))
            {
                record.info.bpm = beats->bpm;  // refined against every beat of the track
                record.grid = BeatGrid::constant (rate, beats->bpm, static_cast<Frame> (std::llround (beats->firstDownbeat)));
            }
        if (const auto key = analysis::detectKey (mono, rate))
        {
            record.info.key = key->name();
            record.info.camelot = key->camelot();
        }
        record.info.loudnessLufs = analysis::integratedLoudness (audio.channels, rate);
        record.sampleRate = rate;
        record.lengthFrames = audio.info.lengthFrames;
    }
    catch (const std::exception&)
    {
        // Missing or unreadable: mark it done so the queue moves on; the crate shows no values for it.
    }
    record.info.analysed = true;
    {
        const std::lock_guard lock (storeLock);
        store->save (record);
    }
    refreshSnapshot();
    if (remaining == 1)
        setStatus ("All tracks analysed.");
    return true;
}

} // namespace dss1::ui
