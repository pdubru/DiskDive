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
- Where the database file lives (per-user app data vs. portable next to the music).
- Which hash for large libraries if SHA-256 proves slow (BLAKE3 is the candidate).

## Steps
1. [x] Track ID for MP3: `audioRegion()`, `computeTrackId()`, `diskdive id`
2. [x] WAV and AIFF support (`audioRegion()` for both)
3. [x] First tests: hash vectors, "retag/move does not change ID", per format. See [[Testing]]
4. [ ] `library/`: SQLite schema (`tracks`, `locations`) and folder scanner
5. [ ] Tag model and sync states
6. [ ] CLI commands to inspect all of the above, then UI
