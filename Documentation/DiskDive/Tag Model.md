# Tag Model (step 5)

Status: **agreed in discussion (2026-11-15), pending a final read-through by the user.** Open points from that read-through are settled at the start of the next session, then development starts with the first slice below.

## Glossary
- **Slot**: an *address* in the file's ID3v2 tag where a value lives, e.g. `TPUB`, `TXXX:peak-time`. For standard fields the slot name equals the frame ID; for `TXXX` the slot adds the description, because many `TXXX` frames can coexist in one file.
- **Frame**: the *data* stored at a slot in a given file. Frames are written, overwritten or removed. Removing a frame leaves the slot empty.
- **Metadata**: the fixed fields every track has (list below).
- **Tag**: an extra piece of information the user adds.
- **Unmanaged slot**: a text slot found in a file that no metadata field or tag owns (typically written by other software).
- **Adopt**: turn an unmanaged slot into a DiskDive tag bound to that slot.
- **Your value / intent**: the value the user wants a field to have, as opposed to what the file currently contains.

## Metadata (fixed)
| Field | Slot (ID3v2.3) | Notes |
|---|---|---|
| Title | `TIT2` | |
| Artist | `TPE1` | |
| Album | `TALB` | |
| BPM | `TBPM` | |
| Key | `TKEY` | see "Musical key" |
| Label | `TPUB` | |
| Genre | `TCON` | added 2026-11-15 |
| Year | `TYER` | `TDRC` in v2.4; the slot registry handles the difference |

- Always shown for every track, filled with whatever the file contains on import.
- Values can be edited or cleared. **Clearing removes the frame from that track's file only**; other tracks are unaffected.
- **Cannot be renamed, deleted or moved to another slot**: they stay in their standard slots so every music library program finds them.
- Always written to the file, never local-only.

## Tags (the user's own)
- **Local** (DiskDive only) or **in file** (stored in a custom slot `TXXX:<name>`, unprefixed so it looks natural in other software). In-file tags carry a **mark** in the track view; later the mark can show the sync state.
- **Emptying** a tag on a track removes its frame from that file.
- **Renaming** a tag moves it to a new slot in every file (`TXXX:peak-time` → `TXXX:Peak Time`), after a preview, to keep files consistent.
- **Deleting** a tag asks the user what to do with the files (remove the frames or leave them), after a preview.
- Names are unique case-insensitively and cannot equal a metadata field name.

> To confirm next session: emptying a tag on one track removes the frame **without a prompt**, while deleting the whole tag **asks**. This is the interpretation of the answers to Q2a and Q5.

## Tags from other software (unmanaged slots)
- The scan records **every text slot**, including those written by other programs (e.g. Mixed In Key's `TXXX:EnergyLevel`).
- **Hidden by default**, **searchable** (it is a plain DB query, no rescan), shown on request.
- **Read-only** until the user **adopts** one.
- DiskDive **never** writes to a slot it does not own, and never stores or touches binary frames (`APIC` artwork, `GEOB`/`PRIV` Serato/Rekordbox data).

## Sync
- Per field and per file DiskDive compares three values: **your value**, the **file value**, the **last agreed value**:

| Your value | File value | Last agreed | State |
|---|---|---|---|
| 128 | 127 | 127 | ↑ you changed it → write to file |
| 127 | 126 | 127 | ↓ another program changed the file → pull in |
| 128 | 126 | 127 | ⚠ conflict, the user decides |
| 127 | 127 | 127 | ✓ in sync |

- **What syncs is persistent**: all metadata plus every in-file tag. An optional **"just this time" filter** narrows one sync run (e.g. write only BPM today).
- Every sync is **preview, then apply**. Each file is saved once per run; running it twice is harmless (`file == yours` counts as in sync).
- The sync also refreshes `size`/`mtime` in `locations` so the scanner does not rehash the file.
- **Format: ID3v2.3** for maximum compatibility with library software. Multi-value fields (e.g. two genres) use the v2.3 separator.

## Musical key
- **Written to files in musical notation** (`Am`, `F#`), the ID3 standard.
- **Display toggle** Camelot (`8A`) ↔ musical (`A minor`): changes only what the user sees, **never touches files**.
- Existing values are never rewritten just because of their notation. Keys are stored parsed (`tonic 0-11`, `mode`) plus the raw text, and compared by meaning: a file saying `8A` while DiskDive says `A minor` is in sync.

## Copies of the same song
- Your values are stored per **track** (the audio, shared by all copies). File contents and sync history are stored per **location** (file).
- If copies disagree on import, your value is seeded from the **first copy found**; the others show ↓ or ⚠. Open for review once it can be seen working.

## Why "facts" and "intent" are separated
If the scan only stored the slots mapped at that moment, what the DB holds would depend on configuration: mapping a new tag or changing a mapping would need a rescan, and other software's tags would stay invisible. So:
- the **scan records facts** (every text slot of every file), knowing nothing about tags;
- **metadata/tags are the user's vocabulary and values**;
- the **binding** (tag → slot) is configuration, changeable in the DB without touching files.

Sync history belongs to the **slot**, not the tag: "what was last agreed in this file" is a fact about an address. A renamed tag lands in a new slot with no history, handled through the preview.

## Database (migration v2)
```
tags        (id, name UNIQUE NOCASE, kind, slot NULL UNIQUE, multi)  -- vocabulary + binding
track_tags  (track_id, tag_id, value)                                -- your values, per track
file_slots  (location_id, slot, value)                               -- scan snapshot, per file
slot_sync   (location_id, slot, synced_value)                        -- last agreed, per file
```
| Table | Aim | Written by |
|---|---|---|
| `tags` | Every metadata field and tag: name, `kind` (`metadata`/`tag`), slot (NULL = local) | migration (metadata rows), user actions |
| `track_tags` | What the user wants each song's values to be | user edits, first-import seeding |
| `file_slots` | What each file actually contains, every text slot | scan, sync |
| `slot_sync` | Last value both sides agreed on, to tell *who* changed what | sync |

- Metadata and tags share one table because the sync machinery is identical; `kind` switches on the rules (metadata: no rename/delete/unbind, fixed slot).
- A field's state for one file is one SQL join of three values, no file access.
- `file_slots` staleness uses the existing `(size, mtime)` of `locations`.

## Implementation notes
- Write custom `TXXX` frames through TagLib's `ID3v2::UserTextIdentificationFrame`, not `setProperties`: `PropertyMap` upper-cases keys and would turn `peak-time` into `PEAK-TIME`.
- Unprefixed `TXXX` names may already exist in files (written by other software); adopting them or binding onto them goes through the preview.

## First slice (no sync, no UI yet)
1. **Slot registry**: metadata slots (v2.3/v2.4 aware), custom `TXXX:<name>`, never-touched frames.
2. **Migration v2**: the four tables, with the 8 metadata rows pre-created.
3. **Scan** records every text slot into `file_slots` and seeds your values on first import.
4. **TagLib tests**:
   - clearing a metadata field removes the frame and the file stays valid and readable;
   - `TXXX` names keep their exact case;
   - saving as v2.3 keeps Serato/Rekordbox binary frames intact.
5. **Unit tests** for the schema and the scan.
