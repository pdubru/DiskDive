# DiskDive — Developer Map

Start here when you want to find your way around the code.

**What it is:** a music-library manager for DJs and music lovers. It edits audio-file tags, and it keeps its own library database of tags next to the ones stored inside the files.

## Notes
- [[Architecture Overview]]: the layers, folders and what depends on what
- [[Track Identity]]: how a track is recognised (audio hash), why, and where the code is
- [[Testing]]: how to run the tests, what they cover, how to add one
- [[Roadmap and Decisions]]: what is decided, what is open, what comes next

## Quick code map
| I want to... | Look at |
|---|---|
| Read/write tags of a file | `core/AudioFile.h`, `core/Mp3File.cpp` |
| Find out which bytes of a file are "the audio" | `AudioFile::audioRegion()` → `Mp3File` / `WavFile` / `AiffFile` |
| Walk the chunks of a WAV/AIFF | `core/ChunkScan.cpp` |
| Run or add tests | `tests/` ([[Testing]]) |
| Compute a track's ID | `core/TrackId.cpp` (uses `core/Sha256.cpp`) |
| Try things from a terminal | `cli/main.cpp` (`diskdive show / set / id`) |

## Build and run
See `CLAUDE.md` in the repo root. In short: `env.bat`, `cmake --preset windows-mingw`, `cmake --build --preset windows-mingw`.
