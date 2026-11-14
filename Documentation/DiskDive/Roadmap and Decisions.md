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
- **The DB is filled from the first scan**: the user sees the file's current tags immediately (`app = synced = file`). *How* the scan records them is being reconsidered, see "Proposal under review" below (the earlier "scan reads only the mapped tags" idea is superseded by it).

## Open
- Relink/alias feature for when a track's audio changes (re-encode, normalise): not designed yet.
- Revisit BLAKE3 / parallel hashing only if a real library makes the first scan painful.
- Scanner limitations listed in [[Library and Scanner]] (quick-check heuristic, per-root move detection, no root removal).

## Proposal under review: tags, slots and bindings (step 5)
Status: **proposed, not yet agreed**. Present it again to the user, explain it, and get agreement before writing code.

### The problem it solves
If the scan only records the slots that are mapped *right now*, what the scan stores depends on configuration. Consequences: mapping a new tag needs data the scan never collected; changing a mapping would require a rescan; tags written by other software (e.g. Mixed In Key's `TXXX:EnergyLevel`) stay invisible.

### The idea: separate facts from intent
- **The scan records facts about files**: every ID3v2 *text* slot it finds (standard text frames such as `TIT2`, `TCON`, `COMM`, and every `TXXX` with its description). Binary frames (`GEOB`, `PRIV`, `APIC`) are not stored. The scan knows nothing about tags or mappings.
- **Tags are the user's vocabulary**: what the user decided, keyed by track.
- **A binding** links a tag to a slot. It is pure configuration: changing it is a DB operation and never needs the files.

### Three cases
| Case | Example | Behavior |
|---|---|---|
| Tag, no slot (*local*) | "needs-edit" | Lives only in DiskDive |
| Tag bound to a slot (*mirrored*) | Title → `TIT2`, "peak-time" → `TXXX:peak-time` | 4 sync states: in sync, app changed, file changed, conflict |
| Slot, no tag (*unmanaged*) | `TXXX:EnergyLevel` from Mixed In Key | Visible read-only; an **adopt** action creates a tag bound to it |

This gives the "see what's in the file from the first scan" behavior, including other software's tags, and makes DiskDive seamless with other apps in both directions.

### Sync history is keyed by slot, not by tag
"What was last agreed in this file" is a fact about a *slot*. If a tag is rebound (e.g. from `COMM` to `TXXX:peak-time`), the new slot simply has no history, which is the "mapping onto existing data" case (preview, then overwrite / adopt / mark conflict).

### Tags live in files, intent lives in tracks
The same audio (same TrackId) can exist at two paths with different tags. So the user's value is per **track**, while the file snapshot and sync history are per **location**. Sync states are computed per location.

### Proposed schema (migration v2)
```
tags        (id, name UNIQUE NOCASE, slot NULL UNIQUE, multi)   -- vocabulary + binding
track_tags  (track_id, tag_id, value)                           -- user intent, per track
file_slots  (location_id, slot, value)                          -- scan snapshot, per file
slot_sync   (location_id, slot, synced_value)                   -- last agreed, per file
```
- Staleness of `file_slots` uses the existing `(size, mtime)` in `locations`: a plan only `stat`s each file and re-reads it if changed.
- A bound tag's state for one file compares `track_tags.value`, `file_slots[slot]`, `slot_sync[slot]`: one SQL join, no file access.
- One writer per table: scan → `file_slots`; bindings → `tags`; sync → the file, `file_slots`, `slot_sync`.

### Implementation notes
- Write custom `TXXX` frames through TagLib's `ID3v2::UserTextIdentificationFrame`, not `setProperties`: the PropertyMap upper-cases keys, which would turn "peak-time" into `PEAK-TIME` and break the unprefixed, natural-looking names. Confirm with a test first.
- Unprefixed `TXXX` names may already exist in files (written by other software); the binding preview handles it.

### Open questions to settle with the user
1. Does the tags / slots / binding concept match their mental model?
2. Should unmanaged slots show automatically in the UI, or only on request ("other tags in this file")?
3. Seeding when copies disagree: proposal is to seed the track's value from the first location found; other locations then show *file changed* / *conflict* (links to the open relink/duplicates topic).
4. Is the "which tags to sync" selection persistent (= the binding) or chosen per run? Recommendation: persistent, with an optional per-run filter.
5. Canonical musical key: recommendation is `(tonic 0-11, mode)` plus a raw-string fallback, parse on read, write in the user's chosen notation (Camelot or musical), and compare *parsed* values so `Am` vs `8A` is not a false conflict.

### First implementation slice (once agreed)
Slot registry (reserved / open standard / custom, version-aware), migration v2 with the four tables, scan snapshot into `file_slots`, tests.

## Next session
Re-present the proposal above to the user, explain it step by step (they want to understand and review it before agreeing), resolve the open questions, then start the first slice.

Suggested opening prompt: *"Read `Documentation/DiskDive/Roadmap and Decisions.md` and show me the tags/slots/binding proposal again."*

## Steps
1. [x] Track ID for MP3: `audioRegion()`, `computeTrackId()`, `diskdive id`
2. [x] WAV and AIFF support (`audioRegion()` for both)
3. [x] First tests: hash vectors, "retag/move does not change ID", per format. See [[Testing]]
4. [x] `library/`: SQLite schema (`tracks`, `locations`, `library_roots`), folder scanner, `diskdive scan` / `list`. See [[Library and Scanner]]
5. [ ] Tag model and sync states
6. [ ] CLI commands to inspect all of the above, then UI
