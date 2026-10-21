# Testing

## Why tests, in one paragraph
A test is a small program that checks that part of the code does what we claim. We write them so that when we change something later (the database, a new format, a refactor) we find out immediately if an old promise broke. The most important promise so far is in [[Track Identity]]: **retagging or moving a file never changes its ID**. A test now guards it for every format.

## Running them
```
ctest --test-dir build/windows-mingw          # pass/fail summary
build/windows-mingw/diskdive_tests.exe        # every test, one line each
build/windows-mingw/diskdive_tests.exe wav    # only tests whose name contains "wav"
```
Exit code 0 means everything passed.

## Where things are
| File | What it is |
|---|---|
| `tests/TestFramework.h` | Our ~60-line mini framework: `TEST(name) { ... }` registers a test; `CHECK(cond)`, `CHECK_EQ(a, b)`, `CHECK_THROWS(expr)` report failures. Same style as Catch2/GoogleTest, so switching later is easy. |
| `tests/test_main.cpp` | `main()`: runs every registered test and prints the results |
| `tests/TestFiles.h` | Builds tiny valid MP3, WAV and AIFF files from scratch in a temp folder |
| `tests/test_sha256.cpp` | Checks our SHA-256 against the official NIST test vectors |
| `tests/test_track_id.cpp` | Audio region and TrackId behaviour, run once per format |

## Key idea: synthetic files
The tests do not use real songs. `TestFiles.h` writes files byte by byte, so **we know exactly which bytes are the audio** and can assert that `audioRegion()` returns exactly those. With a real song we could only say "looks plausible". It also means the tests run on any machine without sample files.

## What is covered (each format: MP3, WAV, AIFF)
- the audio region is exactly the audio (WAV/AIFF: byte for byte)
- the ID has the shape `sha256:` + 64 hex characters and is deterministic
- **retagging** changes the file size but not the audio size or the ID
- **moving/renaming** does not change the ID; **copies** share it
- changing a single bit of audio **does** change the ID
- tags (including non-ASCII and multi-valued) survive save and re-open
- unsupported or broken files are rejected with an error

## A bug the tests found
The "broken file" test showed that TagLib's `isValid()` says *yes* for any file merely **named** `.wav`/`.aiff`, even random bytes. The app would have accepted it and failed later. Fix: `WavFile`/`AiffFile` constructors also check that the file really has a `data`/`SSND` chunk. This is the kind of thing tests are for.

## Adding a test
1. Open the relevant `tests/test_*.cpp` (or add a new file and list it in `CMakeLists.txt` under `diskdive_tests`).
2. Write `TEST(some_descriptive_name) { ... CHECK(...); }`.
3. Rebuild and run.
