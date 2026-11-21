#include "core/LibraryStore.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <stdexcept>

namespace dss1
{

namespace
{
    [[noreturn]] void fail (sqlite3* db, const std::string& what)
    {
        throw std::runtime_error ("Library database: " + what + ": " + (db != nullptr ? sqlite3_errmsg (db) : "out of memory"));
    }

    /// A prepared statement that finalises itself.
    class Statement
    {
    public:
        Statement (sqlite3* db, const char* sql) : db_ (db)
        {
            if (sqlite3_prepare_v2 (db, sql, -1, &stmt_, nullptr) != SQLITE_OK)
                fail (db, "couldn't prepare a query");
        }

        ~Statement() { sqlite3_finalize (stmt_); }
        Statement (const Statement&) = delete;
        Statement& operator= (const Statement&) = delete;

        void bind (int index, const std::string& text)
        {
            if (sqlite3_bind_text (stmt_, index, text.data(), static_cast<int> (text.size()), SQLITE_TRANSIENT) != SQLITE_OK)
                fail (db_, "couldn't bind a value");
        }

        void bind (int index, std::int64_t value)
        {
            if (sqlite3_bind_int64 (stmt_, index, value) != SQLITE_OK)
                fail (db_, "couldn't bind a value");
        }

        /// True while there are rows.
        bool step()
        {
            const auto rc = sqlite3_step (stmt_);
            if (rc == SQLITE_ROW)
                return true;
            if (rc == SQLITE_DONE)
                return false;
            fail (db_, "a query failed");
        }

        std::int64_t integer (int column) const { return sqlite3_column_int64 (stmt_, column); }

        std::string text (int column) const
        {
            const auto* p = reinterpret_cast<const char*> (sqlite3_column_text (stmt_, column));
            return p != nullptr ? std::string (p, static_cast<std::size_t> (sqlite3_column_bytes (stmt_, column))) : std::string();
        }

    private:
        sqlite3* db_;
        sqlite3_stmt* stmt_ = nullptr;
    };

    StoredTrack readRow (const Statement& s)
    {
        StoredTrack t;
        t.id = s.integer (0);
        t.record = trackRecordFromJson (nlohmann::json::parse (s.text (1)));
        t.record.identity.path = s.text (2);  // the path column is the truth (relink updates it)
        return t;
    }

    std::vector<StoredTrack> readAll (Statement& s)
    {
        std::vector<StoredTrack> out;
        while (s.step())
            out.push_back (readRow (s));
        return out;
    }

    sqlite3* openRaw (const std::string& file)
    {
        sqlite3* db = nullptr;
        const auto rc = sqlite3_open_v2 (file.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        if (rc != SQLITE_OK)
        {
            const std::string message = db != nullptr ? sqlite3_errmsg (db) : "out of memory";
            sqlite3_close (db);
            throw std::runtime_error ("Library database: couldn't open " + file + ": " + message);
        }
        return db;
    }
} // namespace

void LibraryStore::Closer::operator() (sqlite3* db) const noexcept
{
    sqlite3_close (db);
}

LibraryStore::LibraryStore (sqlite3* db) : db_ (db)
{
    exec ("PRAGMA foreign_keys = ON");
    migrate();
}

LibraryStore::LibraryStore (LibraryStore&&) noexcept = default;
LibraryStore& LibraryStore::operator= (LibraryStore&&) noexcept = default;
LibraryStore::~LibraryStore() = default;

LibraryStore LibraryStore::open (const std::string& file)
{
    LibraryStore store (openRaw (file));
    store.exec ("PRAGMA journal_mode = WAL");
    return store;
}

LibraryStore LibraryStore::inMemory()
{
    return LibraryStore (openRaw (":memory:"));
}

void LibraryStore::exec (const char* sql) const
{
    char* error = nullptr;
    if (sqlite3_exec (db_.get(), sql, nullptr, nullptr, &error) != SQLITE_OK)
    {
        const std::string message = error != nullptr ? error : "unknown error";
        sqlite3_free (error);
        throw std::runtime_error (std::string ("Library database: ") + message);
    }
}

void LibraryStore::migrate()
{
    Statement version (db_.get(), "PRAGMA user_version");
    version.step();
    const auto current = static_cast<int> (version.integer (0));

    if (current > schemaVersion)
        throw std::runtime_error ("Library database was made by a newer version of DSS1 (schema "
                                  + std::to_string (current) + "); update DSS1 to open it");

    if (current < 1)
    {
        transaction ([this] {
            exec (R"SQL(
                CREATE TABLE tracks (
                    id          INTEGER PRIMARY KEY,
                    path        TEXT NOT NULL UNIQUE,
                    fingerprint TEXT NOT NULL DEFAULT '',
                    record      TEXT NOT NULL,
                    added_at    INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
                    updated_at  INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
                );
                CREATE INDEX tracks_by_fingerprint ON tracks (fingerprint) WHERE fingerprint <> '';
                PRAGMA user_version = 1;
            )SQL");
        });
    }
    if (current < 2)
    {
        // 2: folders DSS1 keeps an eye on for new music.
        transaction ([this] {
            exec (R"SQL(
                CREATE TABLE watched_folders (
                    path     TEXT PRIMARY KEY,
                    added_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
                );
                PRAGMA user_version = 2;
            )SQL");
        });
    }
    if (current < 3)
    {
        // 3: tracks can be set aside (cleared from the Catalog without losing their prep).
        transaction ([this] {
            exec (R"SQL(
                ALTER TABLE tracks ADD COLUMN aside INTEGER NOT NULL DEFAULT 0;
                PRAGMA user_version = 3;
            )SQL");
        });
    }
    if (current < 4)
    {
        // 4: crates, the folders you work from (every watched folder becomes one).
        transaction ([this] {
            exec (R"SQL(
                CREATE TABLE crates (
                    path      TEXT PRIMARY KEY,
                    name      TEXT NOT NULL,
                    last_used INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
                    removed   INTEGER NOT NULL DEFAULT 0
                );
                INSERT OR IGNORE INTO crates (path, name)
                    SELECT path, replace(path, rtrim(path, replace(path, '/', '')), '') FROM watched_folders;
                PRAGMA user_version = 4;
            )SQL");
        });
    }
    // Later schema versions add their upgrade steps here, one `if (current < N)` each.
}

namespace
{
    /// SQL that's true for a track inside the folder bound as ?1 (any depth).
    constexpr const char* insideFolder = "(substr(path, 1, length(?1) + 1) = ?1 || '/')";

    std::string withoutTrailingSlash (std::string p)
    {
        while (p.size() > 1 && (p.back() == '/' || p.back() == '\\'))
            p.pop_back();
        return p;
    }
}

std::size_t LibraryStore::showOnly (const std::string& folder)
{
    const auto f = withoutTrailingSlash (folder);
    transaction ([&] {
        Statement s (db_.get(), (std::string ("UPDATE tracks SET aside = CASE WHEN ") + insideFolder + " THEN 0 ELSE 1 END").c_str());
        s.bind (1, f);
        s.step();
    });
    return count();
}

std::size_t LibraryStore::setAsideIn (const std::string& folder)
{
    Statement s (db_.get(), (std::string ("UPDATE tracks SET aside = 1 WHERE aside = 0 AND ") + insideFolder).c_str());
    s.bind (1, withoutTrailingSlash (folder));
    s.step();
    return static_cast<std::size_t> (sqlite3_changes (db_.get()));
}

void LibraryStore::addCrate (const std::string& path, const std::string& name)
{
    Statement s (db_.get(), R"SQL(
        INSERT INTO crates (path, name) VALUES (?1, ?2)
        ON CONFLICT (path) DO UPDATE SET removed = 0, last_used = strftime('%s', 'now'))SQL");
    s.bind (1, withoutTrailingSlash (path));
    s.bind (2, name);
    s.step();
}

bool LibraryStore::removeCrate (const std::string& path)
{
    Statement s (db_.get(), "UPDATE crates SET removed = 1 WHERE path = ?1 AND removed = 0");
    s.bind (1, withoutTrailingSlash (path));
    s.step();
    return sqlite3_changes (db_.get()) > 0;
}

void LibraryStore::renameCrate (const std::string& path, const std::string& name)
{
    Statement s (db_.get(), "UPDATE crates SET name = ?2 WHERE path = ?1");
    s.bind (1, withoutTrailingSlash (path));
    s.bind (2, name);
    s.step();
}

void LibraryStore::touchCrate (const std::string& path)
{
    Statement s (db_.get(), "UPDATE crates SET last_used = strftime('%s', 'now') WHERE path = ?1");
    s.bind (1, withoutTrailingSlash (path));
    s.step();
}

std::vector<CrateFolder> LibraryStore::crates() const
{
    Statement s (db_.get(), "SELECT path, name, last_used FROM crates WHERE removed = 0 ORDER BY name COLLATE NOCASE, path");
    std::vector<CrateFolder> out;
    while (s.step())
        out.push_back ({ s.text (0), s.text (1), s.integer (2) });
    return out;
}

std::size_t LibraryStore::bringBackAllCrates()
{
    std::size_t n = 0;
    transaction ([&] {
        const auto before = count();
        exec ("UPDATE tracks SET aside = 0 WHERE aside = 1");
        Statement removed (db_.get(), "SELECT path FROM crates WHERE removed = 1");
        std::vector<std::string> gone;
        while (removed.step())
            gone.push_back (removed.text (0));
        for (const auto& p : gone)
            setAsideIn (p);
        const auto after = count();
        n = after > before ? after - before : 0;
    });
    return n;
}

void LibraryStore::addWatchedFolder (const std::string& path)
{
    Statement s (db_.get(), "INSERT OR IGNORE INTO watched_folders (path) VALUES (?1)");
    s.bind (1, path);
    s.step();
}

bool LibraryStore::removeWatchedFolder (const std::string& path)
{
    Statement s (db_.get(), "DELETE FROM watched_folders WHERE path = ?1");
    s.bind (1, path);
    s.step();
    return sqlite3_changes (db_.get()) > 0;
}

std::vector<std::string> LibraryStore::watchedFolders() const
{
    Statement s (db_.get(), "SELECT path FROM watched_folders ORDER BY path");
    std::vector<std::string> out;
    while (s.step())
        out.push_back (s.text (0));
    return out;
}

void LibraryStore::transaction (const std::function<void()>& work)
{
    if (sqlite3_get_autocommit (db_.get()) == 0)
    {
        work();  // already inside one: join it
        return;
    }
    exec ("BEGIN IMMEDIATE");
    try
    {
        work();
        exec ("COMMIT");
    }
    catch (...)
    {
        sqlite3_exec (db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

std::int64_t LibraryStore::save (const TrackRecord& record)
{
    if (record.identity.path.empty())
        throw std::runtime_error ("Library database: a track needs a path to be saved");

    {
        Statement s (db_.get(), R"SQL(
            INSERT INTO tracks (path, fingerprint, record) VALUES (?1, ?2, ?3)
            ON CONFLICT (path) DO UPDATE SET fingerprint = excluded.fingerprint,
                                             record = excluded.record,
                                             updated_at = strftime('%s', 'now'))SQL");
        s.bind (1, record.identity.path);
        s.bind (2, record.identity.fingerprint);
        s.bind (3, toJson (record).dump());
        s.step();
    }

    Statement id (db_.get(), "SELECT id FROM tracks WHERE path = ?1");
    id.bind (1, record.identity.path);
    if (! id.step())
        fail (db_.get(), "a saved track went missing");
    return id.integer (0);
}

std::optional<StoredTrack> LibraryStore::byId (std::int64_t id) const
{
    Statement s (db_.get(), "SELECT id, record, path FROM tracks WHERE id = ?1");
    s.bind (1, id);
    if (! s.step())
        return std::nullopt;
    return readRow (s);
}

std::optional<StoredTrack> LibraryStore::byPath (const std::string& path) const
{
    Statement s (db_.get(), "SELECT id, record, path FROM tracks WHERE path = ?1");
    s.bind (1, path);
    if (! s.step())
        return std::nullopt;
    return readRow (s);
}

std::vector<StoredTrack> LibraryStore::byFingerprint (const std::string& fingerprint) const
{
    if (fingerprint.empty())
        return {};
    Statement s (db_.get(), "SELECT id, record, path FROM tracks WHERE fingerprint = ?1 ORDER BY path");
    s.bind (1, fingerprint);
    return readAll (s);
}

std::vector<StoredTrack> LibraryStore::all() const
{
    Statement s (db_.get(), "SELECT id, record, path FROM tracks WHERE aside = 0 ORDER BY path");
    return readAll (s);
}

std::size_t LibraryStore::count() const
{
    Statement s (db_.get(), "SELECT COUNT(*) FROM tracks WHERE aside = 0");
    s.step();
    return static_cast<std::size_t> (s.integer (0));
}

void LibraryStore::setAside (std::int64_t id, bool aside)
{
    Statement s (db_.get(), "UPDATE tracks SET aside = ?1 WHERE id = ?2");
    s.bind (1, static_cast<std::int64_t> (aside ? 1 : 0));
    s.bind (2, id);
    s.step();
}

std::size_t LibraryStore::setAsideAll()
{
    const auto n = count();
    exec ("UPDATE tracks SET aside = 1 WHERE aside = 0");
    return n;
}

std::size_t LibraryStore::bringBackAll()
{
    const auto n = setAsideCount();
    exec ("UPDATE tracks SET aside = 0 WHERE aside = 1");
    return n;
}

bool LibraryStore::isSetAside (std::int64_t id) const
{
    Statement s (db_.get(), "SELECT aside FROM tracks WHERE id = ?1");
    s.bind (1, id);
    return s.step() && s.integer (0) != 0;
}

std::size_t LibraryStore::setAsideCount() const
{
    Statement s (db_.get(), "SELECT COUNT(*) FROM tracks WHERE aside = 1");
    s.step();
    return static_cast<std::size_t> (s.integer (0));
}

void LibraryStore::relink (std::int64_t id, const std::string& newPath)
{
    if (newPath.empty())
        throw std::runtime_error ("Library database: a track needs a path");

    transaction ([&] {
        auto track = byId (id);
        if (! track)
            throw std::runtime_error ("Library database: no track with id " + std::to_string (id));
        if (const auto other = byPath (newPath); other && other->id != id)
            throw std::runtime_error ("Library database: another track already uses " + newPath);

        track->record.identity.path = newPath;
        Statement s (db_.get(), "UPDATE tracks SET path = ?1, record = ?2, updated_at = strftime('%s', 'now') WHERE id = ?3");
        s.bind (1, newPath);
        s.bind (2, toJson (track->record).dump());
        s.bind (3, id);
        s.step();
    });
}

bool LibraryStore::remove (std::int64_t id)
{
    Statement s (db_.get(), "DELETE FROM tracks WHERE id = ?1");
    s.bind (1, id);
    s.step();
    return sqlite3_changes (db_.get()) > 0;
}

} // namespace dss1
