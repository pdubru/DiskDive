# Library and Scanner

The `library/` layer is the app's own memory: **which tracks exist and where they were last seen**. It sits between `core/` (which understands one audio file) and the front-ends (`cli/`, later `ui/`). See [[Architecture Overview]] for the layering and [[Track Identity]] for the ID it stores.

Files: `library/Database.h/.cpp` (thin SQLite wrapper), `library/Library.h/.cpp` (schema, migrations, scanner), `tests/test_library.cpp`.

## 1. The big picture

```
 folder on disk                      SQLite file (%APPDATA%\DiskDive\library.db)
 ├─ a.wav        ── scan() ──▶       library_roots   (the folders we manage)
 ├─ sub/b.aiff                       tracks          (one row per distinct audio: the ID)
 └─ notes.txt (ignored)              locations       (where each track was seen: path, size, mtime)
```

Three tables, one idea: **a track is *what* it is (its audio hash); a location is *where* it is now.** Because they are separate tables, one track can have many locations (duplicates), and a track keeps its row (and, later, its tags) even when its file disappears.

## 2. Decisions made this session (and why)

| Decision | Reason |
|---|---|
| **One database per user**, at `%APPDATA%\DiskDive\library.db` | Simplest. Also avoids a real danger: this project lives in OneDrive, and SQLite files in cloud-synced folders can be corrupted by the sync client touching them mid-write ([How To Corrupt An SQLite Database](https://www.sqlite.org/howtocorrupt.html)). Paths are stored *relative to a library root*, so a portable mode can be added later without a schema change. |
| **Keep SHA-256** (BLAKE3 stays a "maybe later") | Measured: 51 MB WAV hashed in 274 ms (~185 MB/s, including process start). A 10,000-track library of ~10 MB MP3s is ~100 GB, so the *first* scan is roughly 9 minutes, and **every later scan skips unchanged files without hashing** (rule 1 below). Cost is paid once. The `sha256:` prefix on the ID means switching algorithms later is possible without ambiguity. |
| **SQLite wrapper hides the C API** | Same rule as TagLib in `core/`: `sqlite3.h` is included only in `Database.cpp`, so the rest of the code never depends on SQLite types. |
| **Missing, not deleted** | A file that vanished (unplugged drive, mid-move) must not destroy the track's identity or tags. We set `missing = 1` instead of deleting the row. |

## 3. Walkthrough: `Database.h/.cpp` (the wrapper)

SQLite is a C library: you get opaque pointers (`sqlite3*`, `sqlite3_stmt*`) and must call `close`/`finalize` yourself. A wrapper turns that into safe C++.

### 3.1 Pimpl and RAII

```cpp
class Database {
    ...
private:
    struct Impl;                  // declared here, defined only in Database.cpp
    std::unique_ptr<Impl> impl_;
};
```
```cpp
struct Database::Impl { sqlite3* db = nullptr; };   // only the .cpp knows sqlite3
Database::~Database() { if (impl_ && impl_->db) sqlite3_close(impl_->db); }
```
- **Pimpl** ("pointer to implementation"): the header says *nothing* about SQLite. Anyone including `Database.h` does not need SQLite's headers, and we could swap SQLite out without touching callers. We already do this in `core/` for TagLib.
- **RAII** (resource acquisition is initialization): the object that owns a resource releases it in its destructor. You cannot forget to close the database or finalize a statement, even if an exception is thrown. This is the single most important C++ idea in this file.
- `Statement` defines its move constructor/assignment `= default` *in the .cpp*. That matters: `unique_ptr<Impl>` needs the full `Impl` definition to destroy it, so the special members must be defined where `Impl` is complete.

### 3.2 Errors become exceptions

```cpp
static std::string errorOf(sqlite3* db, const std::string& what) {
    return what + ": " + sqlite3_errmsg(db);
}
```
SQLite reports failure with integer codes (`SQLITE_OK`, `SQLITE_ROW`, `SQLITE_DONE`, ...). Every call is checked and turned into a `DbError` (derived from `std::runtime_error`) carrying SQLite's own message. Callers never have to remember to check a return code.

### 3.3 Prepared statements and binding

```cpp
auto st = db.prepare("SELECT id FROM library_roots WHERE path = ?");
st.bind(1, path);
while (st.step()) { ... st.columnInt64(0) ... }
```
- `prepare` compiles the SQL once into a bytecode program. `bind` fills the `?` placeholders (**1-based**), `step()` runs it: `true` = a row is ready, `false` = finished. Columns are **0-based**. (Both are SQLite's conventions, kept deliberately.)
- **Never build SQL by string concatenation with user data.** A file named `x'; DROP TABLE tracks;--.mp3` must be just a string, not SQL. Binding keeps data and code separate. See [SQL injection](https://owasp.org/www-community/attacks/SQL_Injection).
- `SQLITE_TRANSIENT` in `bind(int, const std::string&)` tells SQLite to *copy* the text immediately, because our `std::string` may be destroyed right after the call.
- `reset()` rewinds a statement so it can be re-run with new values. The scanner prepares each statement **once** outside the loop and reuses it per file. That is much faster than re-parsing SQL thousands of times.
- Methods return `Statement&`, enabling chaining: `st.bind(1, rootId).bind(2, rel);`

### 3.4 Transaction (RAII again)

```cpp
Transaction::Transaction(Database& db) : db_(db) { db_.exec("BEGIN"); }
Transaction::~Transaction() { if (!done_) try { db_.exec("ROLLBACK"); } catch (...) {} }
void Transaction::commit() { db_.exec("COMMIT"); done_ = true; }
```
If the scope exits **without** `commit()` (an exception, an early return), the destructor rolls back. So a scan that dies halfway leaves the database exactly as it was: that is **atomicity**, the "A" in [ACID](https://en.wikipedia.org/wiki/ACID). The `catch (...)` is there because *destructors must not throw*.

Second benefit is speed: without a transaction SQLite syncs to disk after **every** statement; wrapping a scan in one transaction means one sync at the end. Thousands of inserts go from minutes to well under a second.

### 3.5 Small details worth noticing
- `PRAGMA foreign_keys = ON` in the constructor: SQLite ships with foreign-key enforcement **off** for backward-compatibility, and it is per-connection, so we must enable it every time we open.
- Paths go through `u8string()` because SQLite expects UTF-8, while Windows' narrow `std::string` paths use the ANSI code page and would mangle `é` or `ñ`.
- `":memory:"` opens a database that lives only in RAM. We use it in every test: fast, and nothing touches disk.
- `PRAGMA user_version = N` can't take a bound parameter, so `setUserVersion` builds that one statement with `std::to_string` (safe: it's an `int`, never user text).

## 4. Walkthrough: schema and migrations (`Library.cpp`)

### 4.1 The schema (version 1)

```sql
CREATE TABLE library_roots (id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE);
CREATE TABLE tracks        (id TEXT PRIMARY KEY);            -- "sha256:..."
CREATE TABLE locations (
    id INTEGER PRIMARY KEY,
    track_id TEXT NOT NULL REFERENCES tracks(id),
    root_id  INTEGER NOT NULL REFERENCES library_roots(id),
    relative_path TEXT NOT NULL,
    size INTEGER NOT NULL, mtime INTEGER NOT NULL,
    missing INTEGER NOT NULL DEFAULT 0,
    UNIQUE (root_id, relative_path)
);
CREATE INDEX locations_by_track ON locations(track_id);
```
- `INTEGER PRIMARY KEY` makes the column an alias of SQLite's internal [rowid](https://www.sqlite.org/rowidtable.html): fastest possible lookup, auto-assigned.
- `tracks.id` is the hash *text* itself, a **natural key**: the identity is the data, so no lookup is needed to know which track a hash is. (Locations use a numeric surrogate key because a path is not an identity.)
- `REFERENCES` = [foreign key](https://www.sqlite.org/foreignkeys.html): you cannot create a location for a track or root that does not exist.
- `UNIQUE (root_id, relative_path)` = one row per path, and it automatically creates an index, which is what makes the "do we know this path?" lookup fast.
- `locations_by_track` index: the move/duplicate check asks "all locations of track X". Without the index that is a full table scan. (Read [Use The Index, Luke](https://use-the-index-luke.com/) to understand why.)
- `missing` is `INTEGER 0/1`: SQLite has [no boolean type](https://www.sqlite.org/datatype3.html).
- This split (tracks / locations) is basic [normalization](https://en.wikipedia.org/wiki/Database_normalization): each fact stored once.

### 4.2 Migrations via `PRAGMA user_version`

```cpp
static const char* const kMigrations[] = { /* v1 */ R"sql( CREATE TABLE ... )sql", };
static void migrate(Database& db) {
    int v = db.userVersion();
    if (v > kLatestVersion) throw DbError("database was created by a newer DiskDive ...");
    for (; v < kLatestVersion; ++v) {
        Transaction tx(db);
        db.exec(kMigrations[v]);
        db.setUserVersion(v + 1);
        tx.commit();
    }
}
```
- Every database file stores one integer, [`user_version`](https://www.sqlite.org/pragma.html#pragma_user_version), reserved for the application. A new DB reads 0.
- `kMigrations[i]` upgrades version *i* → *i+1*. On open we run every migration we have not applied yet. A user on v1 who installs a build with 3 migrations gets v2 and v3 applied in order.
- **Golden rule: never edit a migration that has shipped; append a new one.** Otherwise databases that already applied the old text diverge from fresh ones.
- Each migration and its version bump share **one transaction**, so a crash cannot leave "tables created but version still 0".
- Refusing a DB *newer* than we understand protects the user's data from an old binary.
- `R"sql( ... )sql"` is a [raw string literal](https://en.cppreference.com/w/cpp/language/string_literal): no escaping, multi-line SQL stays readable.
- Concept to read: [Evolutionary Database Design](https://martinfowler.com/articles/evodb.html).

## 5. Walkthrough: the scanner (`Library::scan`)

For every supported file (`.mp3 .wav .aif .aiff`, case-insensitive) under the root:

```
 same (path,size,mtime) known? ── yes ─▶ RULE 1: unchanged. Skip, NO hashing.
        │ no
        ▼
 RULE 2: hash the audio  (computeTrackId)
        │
        ├─ path already in DB (content changed) ─▶ update that row            → "updated"
        │
        └─ path is new ─▶ RULE 3: other locations of this ID in this root?
                              ├─ one whose file is gone ─▶ move it here     → "moved"
                              ├─ one whose file still exists ─▶ new row     → "duplicate"
                              └─ none ─▶ new track + new row                → "added"
 after the walk:
 RULE 4: every location in this root we did not see ─▶ missing = 1          → "missing"
```

### 5.1 Rule 1, the cheap check
```cpp
byPath.bind(1, rootId).bind(2, rel);
if (byPath.step()) { ... if (size == stored && mtime == stored) { ++unchanged; continue; } }
```
Hashing is the expensive part (a full read of the file), so we first ask the filesystem two cheap questions: *same size? same modification time?* If both match we **assume** the content is unchanged. This "quick check" is the same trick used by `rsync` and `make`. It is a heuristic, see Limitations.

### 5.2 `mtime` as a plain integer
```cpp
static std::int64_t mtimeOf(const fs::path& p) {
    return static_cast<std::int64_t>(fs::last_write_time(p).time_since_epoch().count());
}
```
`std::filesystem::file_time_type` has an unspecified epoch and unit that differs between platforms ([cppreference](https://en.cppreference.com/w/cpp/filesystem/file_time_type)). We never *interpret* it (no "date shown to user"), only compare it with a value we stored ourselves on the same machine, so the raw tick count is enough.

### 5.3 Relative paths with `/`
```cpp
std::string rel = utf8(fs::relative(it->path(), root));
std::replace(rel.begin(), rel.end(), '\\', '/');
```
Stored paths are relative to the root (the portability decision) and always use `/`, so the same library row is valid on Windows and Linux.

### 5.4 Rule 3: telling a move from a copy
```cpp
while (sameTrack.step()) {
    if (fs::exists(root / fs::u8path(sameTrack.columnText(1)))) otherStillExists = true;
    else if (!movedFrom && !seen.count(otherId)) movedFrom = otherId;
}
```
We have a file at a **new** path whose ID we already know. Two stories fit:
- the old path no longer exists → the user **moved/renamed** it → *re-point the existing row* (`moveLoc`, an `UPDATE`) so history is preserved;
- the old path still exists → the user **copied** it → add a second location to the same track.

`seen` is a `std::unordered_set` of location ids already confirmed in this scan, so one old row is never "moved" onto two new files.

### 5.5 `INSERT OR IGNORE` for tracks
`INSERT OR IGNORE INTO tracks(id) VALUES (?)` means "create the track if unknown, do nothing if it exists", an idempotent [conflict clause](https://www.sqlite.org/lang_conflict.html). It lets Rules 2/3 avoid a separate "does it exist?" query.

### 5.6 Rule 4: missing, not deleted
After the walk, any location in this root not in `seen` gets `missing = 1`. This is a *soft delete* (a "[tombstone](https://en.wikipedia.org/wiki/Tombstone_(data_store))"). If the file reappears (drive reconnected), Rule 1 or 2 resets `missing = 0`.

### 5.7 Error policy
```cpp
} catch (const DbError&) { throw; }                  // database trouble: abort, roll back
  catch (const std::exception& e) { failures.push_back(rel + ": " + e.what()); }  // bad file: report, go on
```
One corrupt MP3 must not abort a 10,000-file scan, but a failing *database* is unrecoverable, and rethrowing lets the `Transaction` roll back. Distinguishing the two with different exception types is why `DbError` exists.

## 6. Tests (`tests/test_library.cpp`)
Each scanner rule has a test, all on `:memory:` databases and a `TempDir` of synthetic files (see [[Testing]]): schema version; `addRoot` idempotence; supported/unsupported files; unchanged rescan; **moved file keeps its track**; copy = duplicate (1 track, 2 locations); deleted = missing, track kept, and restored on reappearance; changed content re-hashed; unreadable audio reported not fatal.

## 7. Using it from the CLI
```
diskdive scan <folder> [--db <file>]   # registers the folder (if new) and scans it
diskdive list [--db <file>]            # every known location, "[missing]" flagged
```
Default DB is `defaultDatabasePath()`; `--db` overrides it (handy for experiments).
Real run on `samples/`: first scan `added 4`, second `unchanged 4` (no hashing).

## 8. Known limitations (honest list)
- **Quick check is a heuristic.** A file edited to the *same size* inside the filesystem's timestamp resolution would be missed. Acceptable for now; a "verify" command that force-hashes is a possible fix. Cloud sync (OneDrive) and some tools can also rewrite mtimes without changing content, which just causes a harmless re-hash.
- **Moves are detected per root only** (the `sameTrack` query filters on `root_id`). Moving a file from root A to root B does not create a second track (same ID, `INSERT OR IGNORE`), but B's scan reports it as `added`, and the old location in A only turns `missing` when A is scanned again.
- **A known file that becomes unreadable** (corrupted) is reported in `failures`, is not seen, and therefore also gets marked `missing`.
- **`u8string()` changes type in C++20** (returns `std::u8string`). The `utf8()` helper in `Library.cpp` is the single place to adapt when we move to C++20.
- Scan is single-threaded and hashes one file at a time. Fine until libraries are large; hashing is the part to parallelize first.
- No `library_roots` removal, no listing by track yet.

## 9. Where to improve (exercises)
1. Add a `diskdive scan --verify` that ignores Rule 1 and re-hashes everything.
2. Write migration v2 adding a nullable `title TEXT` to `tracks`, and a test that opens a v1 DB and checks it becomes v2 (you will need a way to create a v1 file in a test).
3. Hash in a thread pool; measure with the numbers in section 2.
4. Enable [WAL mode](https://www.sqlite.org/wal.html) (`PRAGMA journal_mode=WAL`) and read about what it changes for a UI that reads while a scan writes.
5. Replace `INSERT OR IGNORE` + the `Statement` plumbing with a tiny helper like `db.run(sql, args...)` and see what gets simpler.

## 10. Concepts to read more about
**SQLite**
- [SQLite documentation index](https://www.sqlite.org/docs.html), and [About SQLite](https://www.sqlite.org/about.html)
- [Prepared statements (C API)](https://www.sqlite.org/c3ref/stmt.html) and [binding values](https://www.sqlite.org/c3ref/bind_blob.html)
- [Transactions](https://www.sqlite.org/lang_transaction.html), [ACID](https://en.wikipedia.org/wiki/ACID)
- [In-memory databases](https://www.sqlite.org/inmemorydb.html)
- [Datatypes (type affinity)](https://www.sqlite.org/datatype3.html)
- [Query planner and indexes](https://www.sqlite.org/queryplanner.html), [Use The Index, Luke](https://use-the-index-luke.com/)
- [WAL mode](https://www.sqlite.org/wal.html), [How to corrupt a database](https://www.sqlite.org/howtocorrupt.html)

**Modern C++ used here**
- [RAII](https://en.cppreference.com/w/cpp/language/raii)
- [Pimpl idiom](https://en.cppreference.com/w/cpp/language/pimpl)
- [`std::filesystem`](https://en.cppreference.com/w/cpp/filesystem), [`path::u8string`](https://en.cppreference.com/w/cpp/filesystem/path/u8string)
- [Move semantics](https://en.cppreference.com/w/cpp/language/move_constructor), [`std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr)
- [Raw string literals](https://en.cppreference.com/w/cpp/language/string_literal)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) (resource management section, "R")

**Design concepts**
- [Content-addressable storage](https://en.wikipedia.org/wiki/Content-addressable_storage) (what our TrackId is)
- [Database normalization](https://en.wikipedia.org/wiki/Database_normalization)
- [Evolutionary database design / migrations](https://martinfowler.com/articles/evodb.html)
- [Tombstones / soft delete](https://en.wikipedia.org/wiki/Tombstone_(data_store))
- [Time-of-check to time-of-use](https://en.wikipedia.org/wiki/Time-of-check_to_time-of-use) (why a file can change between "looked at it" and "hashed it")
- [BLAKE3](https://github.com/BLAKE3-team/BLAKE3), the faster hash candidate
