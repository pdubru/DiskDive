# Track Identity

## The problem
The app's database stores things *about* a track (tags, ratings, ...). It needs a way to say "this record belongs to that track" that **survives the user moving, renaming or retagging the file**.

## The decision
A track's ID is the **SHA-256 of its audio bytes only**:

```
sha256:86d02f988e4ab6d403569234cd82ea7aabf8cce2ec1f1bd61d243d415b505342
```

"Audio bytes" means the file *without* its tags: no ID3 tags, no container metadata. Those are exactly the parts that change when the user edits tags, so leaving them out is what makes the ID stable.

**The path is not the identity.** The path is only an attribute: "where is this track right now". The database will keep them in separate tables (see below).

### Why this solves the "moved files" worry
1. User moves `A/song.mp3` to `B/song.mp3`.
2. Next scan finds a file at `B/song.mp3` → computes its ID → the ID already exists in the database.
3. The app just updates the track's *location*. All its tags are still attached to the same ID. Nothing is lost.

If we had used the path as the key, step 3 would have found an unknown file, and the old record would have been orphaned.

### Why SHA-256 and not something fancier
- A hash is deterministic: same bytes → same ID, on any machine, with no registry needed.
- SHA-256 is standard, so anyone can double-check our result with other tools (this is how it was verified, see below).
- We implemented it ourselves (`core/Sha256.cpp`, ~100 lines) to avoid adding a dependency just for this. It is isolated behind `computeTrackId()`, so swapping in a faster algorithm later (BLAKE3) is a local change.
- The ID has an **algorithm prefix** (`sha256:`) so IDs from different algorithms can never be confused.

## How it works in code
```
computeTrackId(file)              core/TrackId.cpp
   └─ file.audioRegion()          virtual in core/AudioFile.h → Mp3File::audioRegion()
   └─ read that byte range in 1 MiB chunks
   └─ Sha256::update(...) per chunk, Sha256::finalHex() at the end
```

**Why `audioRegion()` is per-format:** every format hides its audio differently. The region is `{offset, length}` inside the file.

### MP3 (`Mp3File::audioRegion`)
- **Start**: first MPEG frame (TagLib's `firstFrameOffset()`), which is after any ID3v2 tag.
- **Skip the Xing/Info frame**: VBR files begin with a frame holding a seek table, not sound. Some tools rewrite it, so it is excluded.
- **End**: start of the last MPEG frame + that frame's length (a frame header says how long its frame is). This excludes ID3v1 and APE tags at the end.

### WAV and AIFF (`WavFile::audioRegion`, `AiffFile::audioRegion`)
Both formats are a list of **chunks**: `[4-char id][4-byte size][payload][1 pad byte if size is odd]`. WAV stores sizes little-endian, AIFF big-endian. `core/ChunkScan.cpp` walks that list (hopping from chunk header to chunk header, never reading the payloads) until it finds the one we want.

- **WAV**: the audio is the payload of the `data` chunk. Other chunks (`fmt `, `LIST`, `id3 `, ...) are metadata.
- **AIFF**: the audio is in the `SSND` chunk. Its payload begins with two numbers, `offset` and `blockSize`, and the samples start `8 + offset` bytes into the payload. Honouring `offset` matters: ignoring it would include padding bytes in the hash.
- **Truncated files**: a header can claim more bytes than the file has (interrupted download, streamed recording). We clamp the length to what really exists.
- **Tags**: written as an ID3v2 chunk inside the file (`id3 ` in WAV, `ID3 ` in AIFF), which TagLib handles. WAV can also carry `LIST/INFO` tags, which we read but do not rewrite. Not every DJ program reads ID3 in WAV, which is a known limitation of the WAV format.

Class layout: the three format classes share helper code (`core/TagLibUtil.h`) so the tag logic exists only once.

## What changes the ID, and what doesn't
| Action | ID changes? |
|---|---|
| Edit tags, add cover art | No |
| Move or rename the file | No |
| Copy the file | No (both copies share the ID: this is how duplicates are detected) |
| Re-encode, trim, or normalise with a tool that edits the audio (e.g. mp3gain) | **Yes**: it is different audio |
| Convert MP3 → WAV | **Yes** |

The "Yes" cases are handled later by a **relink/alias** feature, so tags are not orphaned: the app will suggest "this looks like the same track, merge?".

## Planned database shape (for the `library/` layer)
Two tables, because identity and location are different things:
- `tracks(id)`: one row per distinct audio.
- `locations(track_id, path, size, mtime)`: where we found it. One track may have several paths (duplicates). `size`+`mtime` let a re-scan skip re-hashing files that have not changed, because hashing means reading the whole file.

## Verified behaviour (2026-10-21)
Test file `UmbersRmx.mp3` (initially no tags):
- Independent check: SHA-256 of the same byte range computed with .NET gave the identical digest.
- After `diskdive set` added tags: file grew by 1163 bytes, audio offset moved 0 → 1163, **ID unchanged**.
- After moving and renaming to another folder: **ID unchanged**.

Try it yourself: `diskdive id <file>`.

These checks are now automated for MP3, WAV and AIFF, see [[Testing]].

> Note: the MP3 checks above used real files. The WAV/AIFF checks use synthetic files built by the tests; they have not yet been tried on real WAV/AIFF exports from DJ software.
