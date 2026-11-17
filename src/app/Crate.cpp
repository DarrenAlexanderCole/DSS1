#include "Crate.h"

#include "core/Analysis.h"
#include "core/Hits.h"
#include "io/AudioDecoder.h"

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
            info.title = name.toStdString();
        }
    }

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
    return io::formatForExtension (f.getFileExtension().toStdString()).has_value();
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
    refreshSnapshot();
    worker = std::thread ([this] { run(); });
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

void Crate::add (const juce::Array<juce::File>& items)
{
    {
        const std::lock_guard lock (queueLock);
        jobs.push_back (items);
    }
    wake.notify_all();
}

std::vector<StoredTrack> Crate::tracks() const
{
    const std::lock_guard lock (stateLock);
    return snapshot;
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
    }
    sendChangeMessage();
}

void Crate::run()
{
    // Analyse anything left over from last time first.
    for (;;)
    {
        juce::Array<juce::File> job;
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

        if (! job.isEmpty())
        {
            importItems (job);
            continue;
        }

        if (analyseNext())
            continue;

        std::unique_lock lock (queueLock);
        wake.wait (lock, [this] { return stopping || ! jobs.empty(); });
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

    int added = 0, failed = 0;
    for (int i = 0; i < files.size(); ++i)
    {
        const auto& file = files.getReference (i);
        const auto path = file.getFullPathName().toStdString();
        {
            const std::lock_guard lock (storeLock);
            if (store->byPath (path))
                continue;  // already in the crate
        }
        setStatus ("Adding " + juce::String (i + 1) + " of " + juce::String (files.size()) + ": " + file.getFileName());
        try
        {
            const auto info = io::probe (path);
            TrackRecord record;
            record.identity.path = path;
            titleFromFileName (file, record.info);
            record.sampleRate = info.sampleRate;
            record.lengthFrames = info.lengthFrames;
            record.exportOffsetFrames = info.encoderDelayFrames;  // Serato and Rekordbox count it (see TrackRecord)
            const std::lock_guard lock (storeLock);
            store->save (record);
            ++added;
        }
        catch (const std::exception&)
        {
            ++failed;
        }
    }
    refreshSnapshot();

    juce::String summary = "Added " + juce::String (added) + (added == 1 ? " track." : " tracks.");
    if (failed > 0)
        summary << " " << failed << " couldn't be read.";
    if (skipped > 0)
        summary << " " << skipped << " weren't audio DSS1 reads yet (WAV, AIFF, FLAC, MP3).";
    setStatus (summary);
}

bool Crate::analyseNext()
{
    std::optional<StoredTrack> next;
    std::size_t remaining = 0;
    {
        const std::lock_guard lock (stateLock);
        for (const auto& t : snapshot)
            if (! t.record.info.analysed)
            {
                if (! next)
                    next = t;
                ++remaining;
            }
    }
    if (! next)
        return false;

    auto record = next->record;
    setStatus ("Analysing " + juce::String (remaining) + (remaining == 1 ? " track" : " tracks") + ": "
               + juce::String (record.info.title));
    try
    {
        const auto audio = io::decode (record.identity.path);
        const auto mono = mixDown (audio.channels);
        const auto rate = audio.info.sampleRate;
        record.info.bpm = analysis::detectBpm (mono, rate);
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
