#include "Library.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_set>

#include "AudioFile.h"

namespace fs = std::filesystem;

namespace diskdive {

// ---------------------------------------------------------------- schema

// Each entry upgrades the DB by one version: kMigrations[0] takes version 0 -> 1, and so on.
// NEVER edit an entry that has shipped; append a new one. PRAGMA user_version records how far we got.
static const char* const kMigrations[] = {
    // v1
    R"sql(
    CREATE TABLE library_roots (
        id   INTEGER PRIMARY KEY,
        path TEXT NOT NULL UNIQUE
    );
    CREATE TABLE tracks (
        id TEXT PRIMARY KEY            -- the "sha256:..." TrackId
    );
    CREATE TABLE locations (
        id            INTEGER PRIMARY KEY,
        track_id      TEXT    NOT NULL REFERENCES tracks(id),
        root_id       INTEGER NOT NULL REFERENCES library_roots(id),
        relative_path TEXT    NOT NULL,
        size          INTEGER NOT NULL,
        mtime         INTEGER NOT NULL,
        missing       INTEGER NOT NULL DEFAULT 0,
        UNIQUE (root_id, relative_path)  -- one row per path; a TRACK may still have several locations
    );
    CREATE INDEX locations_by_track ON locations(track_id);
    )sql",
};
static const int kLatestVersion = static_cast<int>(sizeof(kMigrations) / sizeof(kMigrations[0]));

static void migrate(Database& db) {
    int v = db.userVersion();
    if (v > kLatestVersion)
        throw DbError("database was created by a newer DiskDive (schema v" + std::to_string(v) + ")");
    for (; v < kLatestVersion; ++v) {
        Transaction tx(db);  // a half-applied migration must never be left behind
        db.exec(kMigrations[v]);
        db.setUserVersion(v + 1);
        tx.commit();
    }
}

// ---------------------------------------------------------------- helpers

fs::path defaultDatabasePath() {
    fs::path dir;
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA")) dir = fs::path(appdata) / "DiskDive";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) dir = fs::path(xdg) / "diskdive";
    else if (const char* home = std::getenv("HOME")) dir = fs::path(home) / ".local/share/diskdive";
#endif
    if (dir.empty()) throw DbError("cannot determine a per-user data folder");
    fs::create_directories(dir);
    return dir / "library.db";
}

static bool isSupported(const fs::path& p) {
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == ".mp3" || ext == ".wav" || ext == ".aif" || ext == ".aiff";
}

// Modification time as an integer. The unit is platform-specific, which is fine: we only ever compare it
// with a value we stored ourselves on the same machine ("did it change?"), never interpret it.
static std::int64_t mtimeOf(const fs::path& p) {
    return static_cast<std::int64_t>(fs::last_write_time(p).time_since_epoch().count());
}

// UTF-8 <-> path. generic_string() would use the Windows ANSI code page and mangle non-ASCII names, so go
// through u8string(). (Written for C++17, where u8string() returns std::string; C++20 changes that type.)
static std::string utf8(const fs::path& p) { return p.u8string(); }

// ---------------------------------------------------------------- Library

Library::Library(const fs::path& dbFile) : db_(dbFile) { migrate(db_); }

std::int64_t Library::addRoot(const fs::path& folder) {
    // Canonical absolute path, so "music", ".\music" and "C:\x\music" are the same root.
    const std::string path = utf8(fs::weakly_canonical(folder));
    auto find = db_.prepare("SELECT id FROM library_roots WHERE path = ?");
    find.bind(1, path);
    if (find.step()) return find.columnInt64(0);
    auto ins = db_.prepare("INSERT INTO library_roots(path) VALUES (?)");
    ins.bind(1, path);
    ins.step();
    return db_.lastInsertRowId();
}

ScanResult Library::scan(std::int64_t rootId) {
    ScanResult result;

    std::string rootPath;
    {
        auto st = db_.prepare("SELECT path FROM library_roots WHERE id = ?");
        st.bind(1, rootId);
        if (!st.step()) throw DbError("unknown library root " + std::to_string(rootId));
        rootPath = st.columnText(0);
    }
    const fs::path root = fs::u8path(rootPath);

    auto byPath = db_.prepare("SELECT id, size, mtime FROM locations WHERE root_id = ? AND relative_path = ?");
    auto insertTrack = db_.prepare("INSERT OR IGNORE INTO tracks(id) VALUES (?)");
    auto insertLoc = db_.prepare(
        "INSERT INTO locations(track_id, root_id, relative_path, size, mtime) VALUES (?, ?, ?, ?, ?)");
    auto updateLoc = db_.prepare("UPDATE locations SET track_id = ?, size = ?, mtime = ?, missing = 0 WHERE id = ?");
    auto clearMissing = db_.prepare("UPDATE locations SET missing = 0 WHERE id = ?");
    auto sameTrack = db_.prepare(
        "SELECT id, relative_path FROM locations WHERE track_id = ? AND root_id = ? AND relative_path != ?");
    auto moveLoc = db_.prepare("UPDATE locations SET relative_path = ?, size = ?, mtime = ?, missing = 0 WHERE id = ?");

    std::unordered_set<std::int64_t> seen;  // location ids confirmed present during this scan

    Transaction tx(db_);
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied);
         it != fs::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file() || !isSupported(it->path())) continue;

        std::string rel = utf8(fs::relative(it->path(), root));
        std::replace(rel.begin(), rel.end(), '\\', '/');  // one spelling in the DB, whatever the OS
        try {
            const std::int64_t size = static_cast<std::int64_t>(it->file_size());
            const std::int64_t mtime = mtimeOf(it->path());

            // Rule 1: known path with the same size and mtime -> skip, no hashing.
            std::int64_t locId = 0;
            bool pathKnown = false;
            byPath.reset();
            byPath.bind(1, rootId).bind(2, rel);
            if (byPath.step()) {
                pathKnown = true;
                locId = byPath.columnInt64(0);
                if (byPath.columnInt64(1) == size && byPath.columnInt64(2) == mtime) {
                    clearMissing.reset();
                    clearMissing.bind(1, locId);
                    clearMissing.step();
                    seen.insert(locId);
                    ++result.unchanged;
                    continue;
                }
            }

            // Rule 2: otherwise hash the audio.
            const TrackId id = computeTrackId(*openAudioFile(it->path()));
            insertTrack.reset();
            insertTrack.bind(1, id);
            insertTrack.step();

            if (pathKnown) {
                // Same path, new content (or just touched): refresh the row.
                updateLoc.reset();
                updateLoc.bind(1, id).bind(2, size).bind(3, mtime).bind(4, locId);
                updateLoc.step();
                seen.insert(locId);
                ++result.updated;
                continue;
            }

            // Rule 3: new path. Is this a known track whose old path vanished (a move), or a copy?
            std::int64_t movedFrom = 0;
            bool otherStillExists = false;
            sameTrack.reset();
            sameTrack.bind(1, id).bind(2, rootId).bind(3, rel);
            while (sameTrack.step()) {
                const std::int64_t otherId = sameTrack.columnInt64(0);
                if (fs::exists(root / fs::u8path(sameTrack.columnText(1)))) otherStillExists = true;
                else if (!movedFrom && !seen.count(otherId)) movedFrom = otherId;
            }

            if (movedFrom) {
                moveLoc.reset();
                moveLoc.bind(1, rel).bind(2, size).bind(3, mtime).bind(4, movedFrom);
                moveLoc.step();
                seen.insert(movedFrom);
                ++result.moved;
            } else {
                insertLoc.reset();
                insertLoc.bind(1, id).bind(2, rootId).bind(3, rel).bind(4, size).bind(5, mtime);
                insertLoc.step();
                seen.insert(db_.lastInsertRowId());
                if (otherStillExists) ++result.duplicate;
                else ++result.added;
            }
        } catch (const DbError&) {
            throw;  // a database problem is fatal; a bad audio file is not
        } catch (const std::exception& e) {
            result.failures.push_back(rel + ": " + e.what());
        }
    }

    // Rule 4: anything in this root we did not see is missing. Keep the row (and the track): tags must survive.
    auto all = db_.prepare("SELECT id FROM locations WHERE root_id = ?");
    all.bind(1, rootId);
    std::vector<std::int64_t> gone;
    while (all.step())
        if (!seen.count(all.columnInt64(0))) gone.push_back(all.columnInt64(0));
    auto markMissing = db_.prepare("UPDATE locations SET missing = 1 WHERE id = ?");
    for (auto id : gone) {
        markMissing.reset();
        markMissing.bind(1, id);
        markMissing.step();
    }
    result.missing = static_cast<int>(gone.size());

    tx.commit();
    return result;
}

std::vector<LocationRow> Library::locations() {
    std::vector<LocationRow> rows;
    auto st = db_.prepare(
        "SELECT l.track_id, r.path, l.relative_path, l.size, l.missing "
        "FROM locations l JOIN library_roots r ON r.id = l.root_id ORDER BY r.path, l.relative_path");
    while (st.step()) {
        LocationRow row;
        row.trackId = st.columnText(0);
        row.root = st.columnText(1);
        row.relativePath = st.columnText(2);
        row.size = st.columnInt64(3);
        row.missing = st.columnInt64(4) != 0;
        rows.push_back(std::move(row));
    }
    return rows;
}

int Library::trackCount() {
    auto st = db_.prepare("SELECT COUNT(*) FROM tracks");
    st.step();
    return static_cast<int>(st.columnInt64(0));
}

}  // namespace diskdive
