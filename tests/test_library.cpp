// Tests for the SQLite library layer and the folder scanner. The database is ":memory:" (gone when the
// Library object dies), and the music folder is a TempDir of synthetic files, so nothing touches real data.
#include "Library.h"
#include "TestFiles.h"
#include "TestFramework.h"

using namespace diskdive;
using namespace testfiles;

namespace {

const Bytes kPcmA = fakeAudio(8000, 1);
const Bytes kPcmB = fakeAudio(8000, 2);

}  // namespace

TEST(library_migrates_to_latest_schema) {
    Library lib(":memory:");
    CHECK_EQ(lib.schemaVersion(), 1);
}

TEST(library_add_root_is_idempotent) {
    TempDir dir;
    Library lib(":memory:");
    const auto a = lib.addRoot(dir.path);
    const auto b = lib.addRoot(dir.path);
    CHECK_EQ(a, b);
}

TEST(scan_adds_new_tracks_and_skips_unsupported_files) {
    TempDir dir;
    fs::create_directories(dir.path / "sub");
    writeWav(dir.path / "a.wav", kPcmA);
    writeAiff(dir.path / "sub" / "b.aiff", kPcmB);
    writeAll(dir.path / "notes.txt", {'h', 'i'});

    Library lib(":memory:");
    const auto r = lib.scan(lib.addRoot(dir.path));
    CHECK_EQ(r.added, 2);
    CHECK_EQ(r.failures.size(), std::size_t(0));
    CHECK_EQ(lib.trackCount(), 2);
    const auto rows = lib.locations();
    CHECK_EQ(rows.size(), std::size_t(2));
    CHECK_EQ(rows[0].relativePath, std::string("a.wav"));
    CHECK_EQ(rows[1].relativePath, std::string("sub/b.aiff"));  // '/' separators on every OS
}

TEST(rescan_of_unchanged_files_skips_hashing) {
    TempDir dir;
    writeWav(dir.path / "a.wav", kPcmA);
    Library lib(":memory:");
    const auto root = lib.addRoot(dir.path);
    lib.scan(root);
    const auto r = lib.scan(root);
    CHECK_EQ(r.unchanged, 1);
    CHECK_EQ(r.added, 0);
    CHECK_EQ(r.updated, 0);
}

TEST(moved_file_keeps_its_track_and_updates_the_location) {
    TempDir dir;
    writeWav(dir.path / "old.wav", kPcmA);
    Library lib(":memory:");
    const auto root = lib.addRoot(dir.path);
    lib.scan(root);
    const auto idBefore = lib.locations()[0].trackId;

    fs::create_directories(dir.path / "sorted");
    fs::rename(dir.path / "old.wav", dir.path / "sorted" / "new.wav");
    const auto r = lib.scan(root);

    CHECK_EQ(r.moved, 1);
    CHECK_EQ(r.added, 0);
    CHECK_EQ(r.missing, 0);
    CHECK_EQ(lib.trackCount(), 1);
    const auto rows = lib.locations();
    CHECK_EQ(rows.size(), std::size_t(1));  // updated in place, not a second row
    CHECK_EQ(rows[0].relativePath, std::string("sorted/new.wav"));
    CHECK_EQ(rows[0].trackId, idBefore);
}

TEST(copy_of_a_known_track_is_a_duplicate_location) {
    TempDir dir;
    writeWav(dir.path / "a.wav", kPcmA);
    fs::copy_file(dir.path / "a.wav", dir.path / "copy.wav");
    Library lib(":memory:");
    const auto r = lib.scan(lib.addRoot(dir.path));
    CHECK_EQ(r.added + r.duplicate, 2);
    CHECK_EQ(r.duplicate, 1);
    CHECK_EQ(lib.trackCount(), 1);  // one track...
    CHECK_EQ(lib.locations().size(), std::size_t(2));  // ...two locations
}

TEST(deleted_file_is_marked_missing_but_track_is_kept) {
    TempDir dir;
    writeWav(dir.path / "a.wav", kPcmA);
    Library lib(":memory:");
    const auto root = lib.addRoot(dir.path);
    lib.scan(root);
    fs::remove(dir.path / "a.wav");
    const auto r = lib.scan(root);

    CHECK_EQ(r.missing, 1);
    CHECK_EQ(lib.trackCount(), 1);  // tags/identity survive
    CHECK(lib.locations()[0].missing);

    // ...and it comes back un-missing if the file reappears.
    writeWav(dir.path / "a.wav", kPcmA);
    lib.scan(root);
    CHECK(!lib.locations()[0].missing);
}

TEST(changed_content_at_same_path_is_rehashed) {
    TempDir dir;
    writeWav(dir.path / "a.wav", kPcmA);
    Library lib(":memory:");
    const auto root = lib.addRoot(dir.path);
    lib.scan(root);
    const auto before = lib.locations()[0].trackId;

    writeWav(dir.path / "a.wav", fakeAudio(9000, 3));  // different size, so the size check notices
    const auto r = lib.scan(root);
    CHECK_EQ(r.updated, 1);
    CHECK(lib.locations()[0].trackId != before);
}

TEST(unreadable_audio_is_reported_not_fatal) {
    TempDir dir;
    writeWav(dir.path / "good.wav", kPcmA);
    writeAll(dir.path / "bad.wav", {'n', 'o', 't', ' ', 'a', ' ', 'w', 'a', 'v'});
    Library lib(":memory:");
    const auto r = lib.scan(lib.addRoot(dir.path));
    CHECK_EQ(r.added, 1);
    CHECK_EQ(r.failures.size(), std::size_t(1));
}
