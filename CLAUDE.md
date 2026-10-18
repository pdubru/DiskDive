# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

DiskDive is a C++17 audio-tag editor in early development. Currently there is a core library plus a console tool; a Qt UI (`ui/`) and unit tests (`tests/`) are planned (see the trailing comment in `CMakeLists.txt`). Only MP3/ID3 is implemented.

## Build

Toolchain is vendored under `tools/` (gitignored: `tools/mingw64/`, `tools/cmake/`; `tools/vcpkg` is a git submodule). TagLib comes from vcpkg manifest mode (`vcpkg.json`), installed automatically at configure time.

```
env.bat                              # once per cmd terminal: sets VCPKG_ROOT, adds mingw64 + cmake to PATH
cmake --preset windows-mingw         # configure (MinGW Makefiles, triplet x64-mingw-static)
cmake --build --preset windows-mingw # build -> build/windows-mingw/diskdive.exe
```

A `linux` preset (Ninja) also exists. Output goes to `build/<preset>/`. There are no tests or linter configured yet.

Run the CLI:
```
diskdive show <file.mp3>
diskdive set <file.mp3> KEY=VALUE [KEY=VALUE ...] [--v23]   # empty VALUE removes the field
```
(The usage strings in `cli/main.cpp` still say `tagcli`.)

## Architecture

- `core/` → static library `diskdive_core`. Public API is `core/AudioFile.h`: an abstract `AudioFile` (format-agnostic `FieldMap` of upper-case keys → vector of UTF-8 values, raw frame listing, audio properties, in-memory edits flushed by `save()`), plus the `openAudioFile(path)` factory that picks an implementation by extension and throws `TagError`.
- `core/Mp3File.*` is the only implementation; it uses the pimpl idiom so **TagLib stays a PRIVATE dependency and must never appear in public headers** (and no UI headers in core either). New formats should subclass `AudioFile` and be registered in the factory.
- `cli/main.cpp` → `diskdive` executable; the thin consumer of the core API (re-opens the file after `set` to verify what was written).
- CMake finds TagLib via CONFIG (vcpkg/TagLib 2.x) and falls back to pkg-config (distro TagLib 1.x), so code must compile against both.

## Git workflow

After any major change or modification made by Claude: save the files locally, commit, and push to the remote repo. This is standing authorization from the user for such changes; don't ask again each time.

- Commit message: a clear subject line, then a detailed body explaining what was modified and why (files/components touched, behavior changes, anything notable). Follow the attribution line required by the session's commit instructions.
- Stage only the files relevant to the change by name. Don't use `git add -A`: the repo has untracked local files (e.g. `*.mp3` samples) and unrelated modifications (e.g. `CMakePresets.json`) that shouldn't be swept into commits unless they are part of the change.
- The git remote is named `DiskDive` (not `origin`) and `master` tracks `DiskDive/master`, so push with `git push DiskDive master`. Report any push failure instead of forcing.
- Never print or copy the remote URL into commits, docs or output: it has a credential embedded in it (see `git remote -v`).
- Never force-push or rewrite history without explicit approval.
