# Roadmap and Decisions

## Decided
- **Track identity**: SHA-256 of the audio bytes, with the path stored separately as a location. See [[Track Identity]].
- **Tag model ("option C", tracked mirror)**: for each tracked field the app knows the *file value*, the *app value* and the *last-synced value*. Comparing the three gives the state shown to the user:
  - in sync
  - app changed (can be pushed to the file)
  - file changed (can be pulled into the app; e.g. another program edited the file)
  - conflict (both changed)
- **Tracked standard fields**: title, artist, album/EP (when applicable), BPM, key, label.
- **Key notation**: stored in one canonical form; the user chooses to *display* Camelot (e.g. `8A`) or musical notation (e.g. `A minor`).
- **Database**: SQLite, accessed through a thin wrapper, schema versioned with `PRAGMA user_version`.
- **Audio from other software**: unknown frames written by Serato/Rekordbox must be preserved when saving.

## Open
- **Where the database file lives** (decide first thing in the next session):
  - *Per-user*: one DB in the app-data folder (e.g. `%APPDATA%\DiskDive\library.db`), music anywhere. Simplest. Recommended default.
  - *Portable*: a DB inside each music folder (e.g. `<root>\.diskdive\library.db`), tags travel with the files. Needs relative paths; risky inside cloud-synced folders (concurrent SQLite writes can corrupt).
  - Either way, store paths **relative to a library root**, so portability can be added later. Because tracks are keyed by ID, a DB can re-link to files on another machine.
- Which hash for large libraries if SHA-256 proves slow (BLAKE3 is the candidate).
- Relink/alias feature for when a track's audio changes (re-encode, normalise): not designed yet.

## Next session: step 4, the SQLite layer
Goal: a new `library/` static library (depends on `core`; `core` must stay DB-free and Qt-free) that can scan a folder and remember which tracks live where.

Planned schema (version 1, via `PRAGMA user_version` migrations):
- `tracks(id TEXT PRIMARY KEY)`: the `sha256:...` TrackId. Later: cached audio properties.
- `locations(track_id, root_id, relative_path, size, mtime)`: where a track was seen. One track may have several rows (duplicates). `size` + `mtime` let a re-scan skip re-hashing unchanged files.
- `library_roots(id, path)`: the folders being managed.

Planned behaviour of the scanner, per file found:
1. If `(path, size, mtime)` matches a known location: skip (no hashing).
2. Else compute the TrackId.
3. Known ID at a new path: update the location (this is the "file was moved" case). Unknown ID: insert a track + location.
4. Locations whose file no longer exists: mark missing, do not delete the track or its tags.

Order of work for the session: pick DB location, then add the SQLite dependency (vcpkg `sqlite3`) and a thin wrapper, then schema + migrations, then scanner, then tests (in-memory SQLite `:memory:`), then CLI `diskdive scan <folder>` / `diskdive list`, then a vault note.

Suggested opening prompt: *"Read `Documentation/DiskDive/Home.md` and `Roadmap and Decisions.md`, then start step 4. I want the database per-user (or portable). Explain each decision as we go."*

## Steps
1. [x] Track ID for MP3: `audioRegion()`, `computeTrackId()`, `diskdive id`
2. [x] WAV and AIFF support (`audioRegion()` for both)
3. [x] First tests: hash vectors, "retag/move does not change ID", per format. See [[Testing]]
4. [ ] `library/`: SQLite schema (`tracks`, `locations`) and folder scanner
5. [ ] Tag model and sync states
6. [ ] CLI commands to inspect all of the above, then UI
