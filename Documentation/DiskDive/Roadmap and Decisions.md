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

- **Database location**: one DB per user (`%APPDATA%\DiskDive\library.db`, `$XDG_DATA_HOME/diskdive/` elsewhere), music anywhere. Paths are stored relative to a library root so a portable mode can be added later. Chosen partly because SQLite files inside cloud-synced folders (OneDrive) can corrupt. See [[Library and Scanner]].
- **Hash: keep SHA-256.** Measured ~185 MB/s (51 MB WAV in 274 ms). Only the first scan hashes everything; later scans skip unchanged files by `(path, size, mtime)`. The `sha256:` prefix keeps a later switch (BLAKE3) unambiguous.
- **Missing, not deleted**: a vanished file keeps its track row; its location is flagged `missing`.

## Open
- Relink/alias feature for when a track's audio changes (re-encode, normalise): not designed yet.
- Revisit BLAKE3 / parallel hashing only if a real library makes the first scan painful.
- Scanner limitations listed in [[Library and Scanner]] (quick-check heuristic, per-root move detection, no root removal).

## Next session: step 5, tag model and sync states
Goal: store tracked fields (title, artist, album/EP, BPM, key, label) per track as *file value / app value / last-synced value* and derive the state (in sync, app changed, file changed, conflict). Migration v2 will add the tag tables. Decide first: how to canonically store the musical key, and whether the app value is pulled from the file automatically on first scan.

Suggested opening prompt: *"Read `Documentation/DiskDive/Home.md` and `Roadmap and Decisions.md`, then start step 5. Explain each decision as we go."*

## Steps
1. [x] Track ID for MP3: `audioRegion()`, `computeTrackId()`, `diskdive id`
2. [x] WAV and AIFF support (`audioRegion()` for both)
3. [x] First tests: hash vectors, "retag/move does not change ID", per format. See [[Testing]]
4. [x] `library/`: SQLite schema (`tracks`, `locations`, `library_roots`), folder scanner, `diskdive scan` / `list`. See [[Library and Scanner]]
5. [ ] Tag model and sync states
6. [ ] CLI commands to inspect all of the above, then UI
