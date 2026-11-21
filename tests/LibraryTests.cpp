#include "core/LibraryStore.h"
#include "core/TrackMatcher.h"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <cstdio>
#include <filesystem>
#include <stdexcept>

using namespace dss1;

namespace
{
    TrackRecord makeTrack (const std::string& path, const std::string& fingerprint = {})
    {
        TrackRecord t;
        t.identity = { path, fingerprint };
        t.sampleRate = 44100.0;
        t.lengthFrames = 44100 * 200;
        t.grid = BeatGrid::constant (44100.0, 124.0, 1000);
        t.hotCues[0] = HotCue { 1000, std::nullopt, "Intro", std::nullopt };
        t.savedLoops[2] = SavedLoop { 88200, 176400, "2 bars", std::nullopt, false };
        return t;
    }

    /// A database file in the temp folder, deleted afterwards.
    struct TempFile
    {
        std::filesystem::path path;

        TempFile()
            : path (std::filesystem::temp_directory_path()
                    / ("dss1-test-" + std::to_string (reinterpret_cast<std::uintptr_t> (this)) + ".sqlite"))
        {
            cleanup();
        }

        ~TempFile() { cleanup(); }

        void cleanup() const
        {
            std::error_code ec;
            for (const auto* suffix : { "", "-wal", "-shm" })
                std::filesystem::remove (path.string() + suffix, ec);
        }
    };
}

TEST_CASE ("Tracks are saved and found by id, path and fingerprint", "[library]")
{
    auto store = LibraryStore::inMemory();
    const auto track = makeTrack ("/Music/a.wav", "fp-a");
    const auto id = store.save (track);

    REQUIRE (store.byId (id));
    CHECK (store.byId (id)->record == track);
    REQUIRE (store.byPath ("/Music/a.wav"));
    CHECK (store.byPath ("/Music/a.wav")->id == id);
    REQUIRE (store.byFingerprint ("fp-a").size() == 1);
    CHECK (store.byFingerprint ("").empty());
    CHECK_FALSE (store.byPath ("/Music/missing.wav"));
    CHECK (store.count() == 1);
}

TEST_CASE ("Saving the same path again updates the track in place", "[library]")
{
    auto store = LibraryStore::inMemory();
    auto track = makeTrack ("/Music/a.wav");
    const auto id = store.save (track);

    track.hotCues[1] = HotCue { 5000, std::nullopt, "Drop", std::nullopt };
    CHECK (store.save (track) == id);
    CHECK (store.count() == 1);
    CHECK (store.byId (id)->record.hotCues[1]->name == "Drop");
}

TEST_CASE ("A moved file keeps its cues through relink", "[library]")
{
    auto store = LibraryStore::inMemory();
    const auto id = store.save (makeTrack ("/Music/old/a.wav", "fp-a"));
    store.save (makeTrack ("/Music/b.wav", "fp-b"));

    store.relink (id, "/Music/new/a.wav");
    CHECK_FALSE (store.byPath ("/Music/old/a.wav"));
    const auto moved = store.byPath ("/Music/new/a.wav");
    REQUIRE (moved);
    CHECK (moved->id == id);
    CHECK (moved->record.identity.path == "/Music/new/a.wav");
    CHECK (moved->record.hotCues[0]->name == "Intro");

    CHECK_THROWS_AS (store.relink (id, "/Music/b.wav"), std::runtime_error);
    CHECK_THROWS_AS (store.relink (9999, "/Music/c.wav"), std::runtime_error);
}

TEST_CASE ("A failed transaction leaves the library unchanged", "[library]")
{
    auto store = LibraryStore::inMemory();
    store.save (makeTrack ("/Music/a.wav"));

    CHECK_THROWS (store.transaction ([&] {
        store.save (makeTrack ("/Music/b.wav"));
        store.save (makeTrack ("/Music/c.wav"));
        throw std::runtime_error ("import cancelled");
    }));
    CHECK (store.count() == 1);
}

TEST_CASE ("Tracks are removed and listed in path order", "[library]")
{
    auto store = LibraryStore::inMemory();
    store.save (makeTrack ("/Music/c.wav"));
    const auto b = store.save (makeTrack ("/Music/b.wav"));
    store.save (makeTrack ("/Music/a.wav"));

    const auto all = store.all();
    REQUIRE (all.size() == 3);
    CHECK (all[0].record.identity.path == "/Music/a.wav");
    CHECK (all[2].record.identity.path == "/Music/c.wav");

    CHECK (store.remove (b));
    CHECK_FALSE (store.remove (b));
    CHECK (store.count() == 2);
    CHECK_THROWS_AS (store.save (makeTrack ("")), std::runtime_error);
}

TEST_CASE ("The library file keeps tracks between launches and refuses a newer schema", "[library]")
{
    TempFile file;
    {
        auto store = LibraryStore::open (file.path.string());
        store.save (makeTrack ("/Music/a.wav", "fp-a"));
    }
    {
        auto store = LibraryStore::open (file.path.string());
        REQUIRE (store.count() == 1);
        CHECK (store.byPath ("/Music/a.wav")->record == makeTrack ("/Music/a.wav", "fp-a"));
    }

    // Pretend a future DSS1 upgraded the file.
    sqlite3* db = nullptr;
    REQUIRE (sqlite3_open (file.path.string().c_str(), &db) == SQLITE_OK);
    sqlite3_exec (db, "PRAGMA user_version = 99", nullptr, nullptr, nullptr);
    sqlite3_close (db);
    CHECK_THROWS_AS (LibraryStore::open (file.path.string()), std::runtime_error);
}

TEST_CASE ("Watched folders persist, and a schema 1 library upgrades without losing tracks", "[library]")
{
    TempFile file;
    {
        // A library as DSS1 V2 test builds 1-6 made it.
        sqlite3* db = nullptr;
        REQUIRE (sqlite3_open (file.path.string().c_str(), &db) == SQLITE_OK);
        const auto json = toJson (makeTrack ("/Music/old.wav", "fp")).dump();
        const auto sql = "CREATE TABLE tracks (id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, fingerprint TEXT NOT NULL DEFAULT '', "
                         "record TEXT NOT NULL, added_at INTEGER NOT NULL DEFAULT 0, updated_at INTEGER NOT NULL DEFAULT 0);"
                         "INSERT INTO tracks (path, fingerprint, record) VALUES ('/Music/old.wav', 'fp', '" + json + "');"
                         "PRAGMA user_version = 1;";
        REQUIRE (sqlite3_exec (db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close (db);
    }
    {
        auto store = LibraryStore::open (file.path.string());
        CHECK (store.count() == 1);
        CHECK (store.watchedFolders().empty());
        store.addWatchedFolder ("/Music/B");
        store.addWatchedFolder ("/Music/A");
        store.addWatchedFolder ("/Music/A");
    }
    {
        auto store = LibraryStore::open (file.path.string());
        CHECK (store.watchedFolders() == std::vector<std::string> { "/Music/A", "/Music/B" });
        CHECK (store.removeWatchedFolder ("/Music/A"));
        CHECK_FALSE (store.removeWatchedFolder ("/Music/A"));
        CHECK (store.watchedFolders() == std::vector<std::string> { "/Music/B" });
    }
}

TEST_CASE ("Cleared tracks are set aside with their prep, and come back as they were", "[library]")
{
    TempFile file;
    {
        auto store = LibraryStore::open (file.path.string());
        store.save (makeTrack ("/Old/a.wav", "fp-a"));
        store.save (makeTrack ("/Old/b.wav", "fp-b"));
        CHECK (store.setAsideAll() == 2);
        CHECK (store.count() == 0);
        CHECK (store.all().empty());
        CHECK (store.setAsideCount() == 2);
        // Still findable, with their cues and loops.
        const auto a = store.byPath ("/Old/a.wav");
        REQUIRE (a);
        CHECK (store.isSetAside (a->id));
        CHECK (a->record == makeTrack ("/Old/a.wav", "fp-a"));
        CHECK (store.byFingerprint ("fp-b").size() == 1);
        // New music starts a fresh Catalog.
        store.save (makeTrack ("/New/c.wav", "fp-c"));
        CHECK (store.count() == 1);
        // Saving a set-aside track (an analysis finishing, say) doesn't bring it back by itself.
        store.save (makeTrack ("/Old/b.wav", "fp-b"));
        CHECK (store.count() == 1);
        // One track comes back when its file is added again.
        store.setAside (a->id, false);
        CHECK (store.count() == 2);
        CHECK_FALSE (store.isSetAside (a->id));
    }
    {
        // Set aside survives a relaunch, and Bring Back returns the rest.
        auto store = LibraryStore::open (file.path.string());
        CHECK (store.count() == 2);
        CHECK (store.bringBackAll() == 1);
        CHECK (store.count() == 3);
        CHECK (store.setAsideCount() == 0);
    }
}

// --- Matching -------------------------------------------------------------------------------------------

TEST_CASE ("looseKey ignores case and accents, composed or decomposed", "[matcher]")
{
    const std::string composed = "/Users/DJ/Caf\xC3\xA9.mp3";       // é as one code point
    const std::string decomposed = "/Users/dj/Cafe\xCC\x81.MP3";    // e + combining acute
    CHECK (looseKey (composed) == "/users/dj/cafe.mp3");
    CHECK (looseKey (decomposed) == "/users/dj/cafe.mp3");
    CHECK (looseKey ("C:\\Music\\\xC3\x85se.wav") == "c:/music/ase.wav");
    CHECK (looseKey ("\xE6\x97\xA5\xE6\x9C\xAC.wav") == "\xE6\x97\xA5\xE6\x9C\xAC.wav");  // other scripts untouched
}

TEST_CASE ("Matching tries strict rules before loose ones", "[matcher]")
{
    const std::vector<MatchCandidate> library {
        { 1, "/Music/Crate/Artist - Track.wav", 300.4 },
        { 2, "/Music/Crate/Caf\xC3\xA9 Del Mar.wav", 412.0 },
        { 3, "/Music/Other/Intro.wav", 60.0 },
        { 4, "/Music/Crate2/Intro.wav", 61.0 },
    };

    auto r = matchTrack ({ "/Music/Crate/Artist - Track.wav", 300.0 }, library);
    CHECK (r.kind == MatchKind::exactPath);
    CHECK (r.ids == std::vector<std::int64_t> { 1 });

    r = matchTrack ({ "/music/crate/Cafe\xCC\x81 del mar.wav", std::nullopt }, library);
    CHECK (r.kind == MatchKind::samePath);
    CHECK (r.ids == std::vector<std::int64_t> { 2 });

    // Same file name on another drive, same length.
    r = matchTrack ({ "/Volumes/USB/Artist - Track.wav", 300.0 }, library);
    CHECK (r.kind == MatchKind::sameFileName);
    CHECK (r.isMatch());

    // The MP3 Rekordbox knows was converted to WAV in the crate.
    r = matchTrack ({ "/Users/dj/Downloads/Artist - Track.mp3", 301.0 }, library);
    CHECK (r.kind == MatchKind::sameName);
    CHECK (r.ids == std::vector<std::int64_t> { 1 });

    // A different length rules a name match out.
    r = matchTrack ({ "/Users/dj/Downloads/Artist - Track.mp3", 200.0 }, library);
    CHECK (r.kind == MatchKind::none);
    CHECK_FALSE (r.isMatch());

    // Two "Intro.wav" files of similar length: never guess.
    r = matchTrack ({ "/Elsewhere/Intro.wav", 60.5 }, library);
    CHECK (r.kind == MatchKind::ambiguous);
    CHECK (r.ids.size() == 2);
    CHECK_FALSE (r.isMatch());
}
