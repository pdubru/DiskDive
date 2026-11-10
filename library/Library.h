#pragma once
// The library: which tracks exist and where they were last seen. Depends on core (to compute TrackIds);
// core never depends on this. No Qt/UI headers here either.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Database.h"
#include "TrackId.h"

namespace diskdive {

// Per-user database location: %APPDATA%\DiskDive\library.db on Windows, $XDG_DATA_HOME or ~/.local/share
// elsewhere. Creates the folder. (Decision: one DB per user, music can live anywhere.)
std::filesystem::path defaultDatabasePath();

struct ScanResult {
    int added = 0;      // new track (unknown ID)
    int moved = 0;      // known ID that appeared at a new path, old path gone
    int duplicate = 0;  // known ID at a new path while the old path still exists
    int updated = 0;    // same path, but size/mtime changed so the file was re-hashed
    int unchanged = 0;  // (path, size, mtime) matched: skipped without hashing
    int missing = 0;    // locations whose file no longer exists
    std::vector<std::string> failures;  // "relative/path: reason" for unreadable files
};

struct LocationRow {
    TrackId trackId;
    std::string root;          // library root path
    std::string relativePath;  // generic ('/') separators, relative to the root
    std::int64_t size = 0;
    bool missing = false;
};

class Library {
public:
    // Opens the database and migrates its schema to the latest version.
    explicit Library(const std::filesystem::path& dbFile);

    // Registers a folder to manage (idempotent). Returns the root id.
    std::int64_t addRoot(const std::filesystem::path& folder);

    // Walks the root recursively and brings the DB up to date. Supported extensions only (.mp3 .wav .aif .aiff).
    ScanResult scan(std::int64_t rootId);

    std::vector<LocationRow> locations();  // every known location, ordered by root then path
    int trackCount();

    int schemaVersion() { return db_.userVersion(); }

private:
    Database db_;
};

}  // namespace diskdive
