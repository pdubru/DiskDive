# Architecture Overview

## Layers

```
cli/  (diskdive.exe)   ─┐
ui/   (Qt, planned)    ─┼──▶  library/ (planned: database, scanner, tag model)  ──▶  core/
                        └─────────────────────────────────────────────────────────────▲
```

Arrows mean "depends on". Dependencies only point **down**, never up. That is what keeps each layer replaceable.

| Folder | Status | Responsibility | Must NOT contain |
|---|---|---|---|
| `core/` | exists | Look *inside one audio file*: read/write its tags, find its audio bytes, compute its ID | database code, UI code, TagLib types in public headers |
| `library/` | planned | The app's own knowledge: SQLite database, scanning folders, app tags, sync state | UI code |
| `cli/` | exists | Thin console front-end, used to try things | business logic |
| `ui/` | planned | Qt interface | business logic |

## Why TagLib is hidden
TagLib is a *private* dependency of `core`. Public headers (`AudioFile.h`, `TrackId.h`) never mention it. Each format class (e.g. `Mp3File`) uses the **pimpl idiom**: the header declares `struct Impl;` and only the `.cpp` knows what is in it. Benefit: we could replace TagLib one day, and nothing outside `core` would notice.

## Adding a format
Subclass `AudioFile`, implement its virtual functions (including `audioRegion()`, see [[Track Identity]]), register the extension in `openAudioFile()` in `core/AudioFile.cpp`.

Status: MP3 done. WAV and AIFF planned.
