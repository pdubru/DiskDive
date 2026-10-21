// Tests for the audio region and TrackId of every supported format.
// Each test runs once per format (MP3, WAV, AIFF) over synthetic files made by TestFiles.h.
#include <functional>

#include "AudioFile.h"
#include "TestFiles.h"
#include "TestFramework.h"
#include "TrackId.h"

using namespace diskdive;
using namespace testfiles;

namespace {

constexpr std::size_t kMp3Frames = 30;
const Bytes kPcm = fakeAudio(17640);  // 0.1 s of "CD quality" stereo

struct Format {
    std::string name;
    std::string extension;
    std::function<void(const fs::path&)> write;  // creates a file with known audio
    std::size_t expectedAudioBytes;              // how many bytes of that file are audio
};

const std::vector<Format>& formats() {
    static const std::vector<Format> f = {
        {"MP3", ".mp3", [](const fs::path& p) { writeMp3(p, kMp3Frames); }, kMp3Frames * kMp3FrameSize},
        {"WAV", ".wav", [](const fs::path& p) { writeWav(p, kPcm); }, kPcm.size()},
        {"AIFF", ".aiff", [](const fs::path& p) { writeAiff(p, kPcm); }, kPcm.size()},
    };
    return f;
}

// Runs `body` once per format with a fresh temp dir and a ready-made file.
void forEachFormat(const std::function<void(const Format&, const fs::path& dir, const fs::path& file)>& body) {
    for (const auto& fmt : formats()) {
        testfw::context() = fmt.name;
        TempDir dir;
        const fs::path file = dir.path / ("track" + fmt.extension);
        fmt.write(file);
        body(fmt, dir.path, file);
    }
    testfw::context().clear();
}

TrackId idOf(const fs::path& p) { return computeTrackId(*openAudioFile(p)); }
AudioRegion regionOf(const fs::path& p) { return openAudioFile(p)->audioRegion(); }

}  // namespace

// The region must be exactly the audio bytes we put in: not one byte of header, tag or padding.
TEST(region_covers_exactly_the_audio) {
    forEachFormat([](const Format& fmt, const fs::path&, const fs::path& file) {
        const AudioRegion r = regionOf(file);
        CHECK_EQ(r.length, static_cast<std::uint64_t>(fmt.expectedAudioBytes));
        if (fmt.name != "MP3") {  // for WAV/AIFF we can also compare the actual bytes with what we wrote
            const Bytes all = readAll(file);
            const Bytes slice(all.begin() + r.offset, all.begin() + r.offset + r.length);
            CHECK(slice == kPcm);
        }
    });
}

TEST(id_has_expected_shape) {
    forEachFormat([](const Format&, const fs::path&, const fs::path& file) {
        const TrackId id = idOf(file);
        CHECK_EQ(id.size(), std::size_t(7 + 64));  // "sha256:" + 64 hex chars
        CHECK_EQ(id.substr(0, 7), std::string("sha256:"));
    });
}

TEST(id_is_deterministic) {
    forEachFormat([](const Format&, const fs::path&, const fs::path& file) {
        CHECK_EQ(idOf(file), idOf(file));
    });
}

// THE core promise: editing tags changes the file (and shifts where the audio sits) but not the ID.
TEST(id_survives_retagging) {
    forEachFormat([](const Format&, const fs::path&, const fs::path& file) {
        const TrackId before = idOf(file);
        const AudioRegion regionBefore = regionOf(file);
        const auto sizeBefore = fs::file_size(file);

        {
            auto f = openAudioFile(file);
            f->setField("TITLE", {"A Brand New Title"});
            f->setField("ARTIST", {"Some Artist"});
            f->setField("ALBUM", {"An album name long enough to make the tag grow quite a bit"});
            f->save();
        }  // closing the file object releases it (required on Windows before re-opening)

        CHECK(fs::file_size(file) > sizeBefore);          // the file really changed...
        CHECK_EQ(regionOf(file).length, regionBefore.length);  // ...the audio has the same size...
        CHECK_EQ(idOf(file), before);                      // ...and the identity is untouched
    });
}

TEST(id_survives_move_and_rename) {
    forEachFormat([](const Format& fmt, const fs::path& dir, const fs::path& file) {
        const TrackId before = idOf(file);
        fs::create_directories(dir / "elsewhere");
        const fs::path moved = dir / "elsewhere" / ("totally different name" + fmt.extension);
        fs::rename(file, moved);
        CHECK_EQ(idOf(moved), before);
    });
}

TEST(copies_share_the_same_id) {
    forEachFormat([](const Format& fmt, const fs::path& dir, const fs::path& file) {
        const fs::path copy = dir / ("copy" + fmt.extension);
        fs::copy_file(file, copy);
        CHECK_EQ(idOf(copy), idOf(file));
    });
}

// Different audio must give a different ID. Flip one bit in the middle of the audio.
TEST(id_changes_when_audio_changes) {
    forEachFormat([](const Format&, const fs::path&, const fs::path& file) {
        const TrackId before = idOf(file);
        const AudioRegion r = regionOf(file);

        Bytes all = readAll(file);
        all[r.offset + r.length / 2] ^= 0x01;
        writeAll(file, all);

        CHECK(idOf(file) != before);
    });
}

TEST(tags_round_trip) {
    forEachFormat([](const Format&, const fs::path&, const fs::path& file) {
        {
            auto f = openAudioFile(file);
            f->setField("TITLE", {"Café del Mar"});  // non-ASCII on purpose: must survive as UTF-8
            f->setField("ARTIST", {"A", "B"});       // multi-valued field
            f->save();
        }
        const FieldMap fields = openAudioFile(file)->fields();
        CHECK_EQ(fields.at("TITLE").at(0), std::string("Café del Mar"));
        CHECK_EQ(fields.at("ARTIST").size(), std::size_t(2));
    });
}

TEST(unsupported_or_broken_files_are_rejected) {
    TempDir dir;
    writeAll(dir.path / "notes.txt", fakeAudio(100));
    CHECK_THROWS(openAudioFile(dir.path / "notes.txt"));

    writeAll(dir.path / "garbage.wav", fakeAudio(500));  // right extension, wrong content
    CHECK_THROWS(openAudioFile(dir.path / "garbage.wav"));

    writeAll(dir.path / "garbage.aiff", fakeAudio(500));
    CHECK_THROWS(openAudioFile(dir.path / "garbage.aiff"));
}
