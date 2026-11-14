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
- **One tag concept (supersedes the "two kinds of tags" idea)**: every tag is a `tag_def` with an optional `export_target` (a slot in the file). Built-in fields (title, artist, ...) are just tags that ship pre-mapped. A tag with no target is *local only*; a tag with a target gets the file/app/last-synced state comparison.
- **Single write target: ID3v2 for every format.** MP3 (ID3v2 at file start), WAV (`id3 ` chunk) and AIFF (`ID3 ` chunk) all carry a full ID3v2 tag; RIFF INFO, AIFF text chunks and ID3v1 cannot hold BPM/key/label/custom fields, so they are never written (an optional INFO mirror of title/artist/album may come later).
- **Slots and collisions**: the unit of ownership is a *slot* (one writable location in the ID3v2 tag), and a slot has exactly one owner (`UNIQUE` on `export_target`).
  - *Reserved* slots (`TIT2`, `TPE1`, `TALB`, `TBPM`, `TKEY`, `TPUB`) belong to built-in tags only.
  - *Open standard* slots (`TCON`, `COMM`, `TIT1`, `TMOO` for v2.4) may be claimed by one user tag, for compatibility with other apps.
  - *Custom* slot: `TXXX:<tag name>`, **unprefixed** so files look natural in other software.
  - *Never touched*: `GEOB`, `PRIV`, `APIC` and any frame we don't own (Serato/Rekordbox data).
  - Names are unique case-insensitively and cannot equal a reserved key (`TITLE`, `ARTIST`, `BPM`, ...).
- **Sync is bulk and once per file**: `planSync` (read-only, produces a preview) then `applySync` (one `save()` per file plus one DB transaction that also refreshes size/mtime in `locations` so the scanner does not rehash). Tags never change the TrackId. `file_val == app_val` counts as in sync, which makes sync idempotent and crash-safe. The trigger/UI is deliberately left for later.
- **Scan reads the file's mapped tags** so the DB is filled from the first scan (`app = synced = file` for pre-mapped fields).

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
