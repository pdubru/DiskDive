#pragma once
// Builds tiny but structurally valid audio files from scratch, so tests don't depend on real music.
// Because WE write the bytes, we know exactly which ones are "the audio" and can check the library's answer.
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace testfiles {

using Bytes = std::vector<std::uint8_t>;
namespace fs = std::filesystem;

// A fresh folder under the system temp dir, deleted when the object goes out of scope.
struct TempDir {
    fs::path path;
    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("diskdive-tests-" + std::to_string(stamp));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

inline Bytes readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline void writeAll(const fs::path& p, const Bytes& b) {
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

// Deterministic pseudo-random bytes (same seed -> same bytes) standing in for audio samples.
inline Bytes fakeAudio(std::size_t n, std::uint32_t seed = 12345) {
    Bytes out(n);
    for (auto& b : out) {
        seed = seed * 1664525u + 1013904223u;
        b = static_cast<std::uint8_t>(seed >> 24);
    }
    return out;
}

inline void put(Bytes& b, const char* s4) { b.insert(b.end(), s4, s4 + 4); }
inline void putLE32(Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
inline void putBE32(Bytes& b, std::uint32_t v) { for (int i = 3; i >= 0; --i) b.push_back(std::uint8_t(v >> (8 * i))); }
inline void putLE16(Bytes& b, std::uint16_t v) { b.push_back(std::uint8_t(v)); b.push_back(std::uint8_t(v >> 8)); }
inline void putBE16(Bytes& b, std::uint16_t v) { b.push_back(std::uint8_t(v >> 8)); b.push_back(std::uint8_t(v)); }

// WAV: RIFF header, fmt chunk, an odd-sized JUNK chunk (so the padding rule gets exercised), data chunk.
// Layout:  RIFF <size> WAVE | fmt  <16> ... | JUNK <5> ..+pad | data <n> samples
inline void writeWav(const fs::path& p, const Bytes& pcm) {
    Bytes body;
    put(body, "WAVE");
    put(body, "fmt "); putLE32(body, 16);
    putLE16(body, 1); putLE16(body, 2);           // PCM, 2 channels
    putLE32(body, 44100); putLE32(body, 44100 * 4);
    putLE16(body, 4); putLE16(body, 16);          // block align, bits per sample
    put(body, "JUNK"); putLE32(body, 5);
    for (int i = 0; i < 5; ++i) body.push_back('x');
    body.push_back(0);                            // pad byte: chunks are padded to an even size
    put(body, "data"); putLE32(body, static_cast<std::uint32_t>(pcm.size()));
    body.insert(body.end(), pcm.begin(), pcm.end());

    Bytes file;
    put(file, "RIFF"); putLE32(file, static_cast<std::uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
    writeAll(p, file);
}

// AIFF: big-endian. The SSND chunk starts with "offset" and "blockSize", and the samples begin `offset`
// bytes later. We use offset=2 so the library has to honour it.
// Layout:  FORM <size> AIFF | COMM <18> ... | SSND <8+2+n> offset=2 block=0 [2 junk bytes] samples
inline void writeAiff(const fs::path& p, const Bytes& pcm) {
    Bytes body;
    put(body, "AIFF");
    put(body, "COMM"); putBE32(body, 18);
    putBE16(body, 2);                                              // channels
    putBE32(body, static_cast<std::uint32_t>(pcm.size() / 4));    // sample frames
    putBE16(body, 16);                                             // bits per sample
    const std::uint8_t rate44100[10] = {0x40, 0x0E, 0xAC, 0x44, 0, 0, 0, 0, 0, 0};  // 80-bit extended float
    body.insert(body.end(), rate44100, rate44100 + 10);
    put(body, "SSND"); putBE32(body, static_cast<std::uint32_t>(8 + 2 + pcm.size()));
    putBE32(body, 2); putBE32(body, 0);
    body.push_back(0xEE); body.push_back(0xEE);                    // the 2 bytes skipped by offset=2
    body.insert(body.end(), pcm.begin(), pcm.end());
    if (pcm.size() % 2) body.push_back(0);

    Bytes file;
    put(file, "FORM"); putBE32(file, static_cast<std::uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
    writeAll(p, file);
}

// MP3: a run of identical-size MPEG-1 Layer III frames (128 kbps, 44.1 kHz => 417 bytes each).
// A frame header is 4 bytes: FF FB 90 00 (sync, MPEG1/Layer3/no CRC, 128kbps/44.1kHz/no padding, stereo).
constexpr std::size_t kMp3FrameSize = 417;
inline void writeMp3(const fs::path& p, std::size_t frames) {
    Bytes file;
    for (std::size_t i = 0; i < frames; ++i) {
        const Bytes body = fakeAudio(kMp3FrameSize - 4, static_cast<std::uint32_t>(i + 1));
        file.insert(file.end(), {0xFF, 0xFB, 0x90, 0x00});
        file.insert(file.end(), body.begin(), body.end());
    }
    writeAll(p, file);
}

}  // namespace testfiles
