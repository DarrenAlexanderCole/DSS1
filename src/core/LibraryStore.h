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

/// The library database: one SQLite file holding every track's record (grid, cues, loops) as JSON, found by
/// path or by audio fingerprint. A file that moves keeps its data through `relink`.
///
/// The file carries a schema number. Opening a database made by a newer DSS1 throws instead of risking
/// damage; opening an older one upgrades it in place. All methods throw std::runtime_error on failure.
class LibraryStore
{
public:
    static constexpr int schemaVersion = 1;

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
    /// Every track, ordered by path.
    std::vector<StoredTrack> all() const;
    std::size_t count() const;

    /// The file moved: give the track its new path, keeping everything else. Throws if another track
    /// already has that path.
    void relink (std::int64_t id, const std::string& newPath);

    /// Returns false if there was no such track.
    bool remove (std::int64_t id);

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
