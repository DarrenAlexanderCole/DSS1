#pragma once

#include "core/TrackRecord.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace dss1
{

/// A track as the library holds it.
struct StoredTrack
{
    std::int64_t id = 0;
    TrackRecord record;
};

/// A crate: a folder of music you work from, named after the folder (you can rename it).
struct CrateFolder
{
    std::string path, name;
    std::int64_t lastUsed = 0;  ///< seconds since 1970
};

/// The library database: one SQLite file holding every track's record (grid, cues, loops) as JSON, found by
/// path or by audio fingerprint. A file that moves keeps its data through `relink`.
///
/// The file carries a schema number. Opening a database made by a newer DSS1 throws instead of risking
/// damage; opening an older one upgrades it in place. All methods throw std::runtime_error on failure.
class LibraryStore
{
public:
    static constexpr int schemaVersion = 4;

    /// Opens (creating if needed) the database at `file`.
    static LibraryStore open (const std::string& file);
    /// A throwaway database in memory, for tests and previews.
    static LibraryStore inMemory();

    LibraryStore (LibraryStore&&) noexcept;
    LibraryStore& operator= (LibraryStore&&) noexcept;
    ~LibraryStore();

    /// Adds or updates the track at `record.identity.path`. Returns its id.
    std::int64_t save (const TrackRecord& record);

    std::optional<StoredTrack> byId (std::int64_t id) const;
    std::optional<StoredTrack> byPath (const std::string& path) const;
    /// Every track with this fingerprint (normally one; copies of the same audio share it).
    std::vector<StoredTrack> byFingerprint (const std::string& fingerprint) const;
    /// Every track in the Catalog (not the ones set aside), ordered by path.
    std::vector<StoredTrack> all() const;
    /// How many tracks are in the Catalog (not counting the ones set aside).
    std::size_t count() const;

    /// Sets tracks aside: they leave the Catalog but keep everything (grid, cues, loops, notes), so they come back
    /// as they were when their files are added again. byId, byPath and byFingerprint still find them.
    void setAside (std::int64_t id, bool aside);
    /// Sets every track in the Catalog aside. Returns how many.
    std::size_t setAsideAll();
    /// Brings every set-aside track back. Returns how many.
    std::size_t bringBackAll();
    bool isSetAside (std::int64_t id) const;
    std::size_t setAsideCount() const;
    /// Sets aside every track that isn't inside `folder` and brings back every one that is. Returns how many are
    /// in the Catalog afterwards.
    std::size_t showOnly (const std::string& folder);
    /// Sets aside the tracks inside `folder`. Returns how many.
    std::size_t setAsideIn (const std::string& folder);

    /// Crates: folders you've worked from. Adding one that exists (or was removed) makes it current and visible.
    void addCrate (const std::string& path, const std::string& name);
    /// Takes a crate off the list (its tracks are kept, set aside). Returns false if there was none.
    bool removeCrate (const std::string& path);
    void renameCrate (const std::string& path, const std::string& name);
    /// Marks a crate as just used.
    void touchCrate (const std::string& path);
    /// The crates on the list, by name.
    std::vector<CrateFolder> crates() const;
    /// Brings back every set-aside track except those inside removed crates. Returns how many came back.
    std::size_t bringBackAllCrates();

    /// The file moved: give the track its new path, keeping everything else. Throws if another track
    /// already has that path.
    void relink (std::int64_t id, const std::string& newPath);

    /// Returns false if there was no such track.
    bool remove (std::int64_t id);

    /// Folders DSS1 watches for new music (paths as given, in path order).
    void addWatchedFolder (const std::string& path);
    bool removeWatchedFolder (const std::string& path);
    std::vector<std::string> watchedFolders() const;

    /// Runs `work` in one transaction: all of its changes are kept, or none if it throws.
    void transaction (const std::function<void()>& work);

private:
    explicit LibraryStore (sqlite3* db);
    void migrate();
    void exec (const char* sql) const;

    struct Closer { void operator() (sqlite3*) const noexcept; };
    std::unique_ptr<sqlite3, Closer> db_;
};

} // namespace dss1
