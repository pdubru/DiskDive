#include "ChunkScan.h"

#include <cstring>
#include <fstream>

namespace diskdive::detail {

namespace {

std::uint32_t readU32(const unsigned char* p, bool bigEndian) {
    return bigEndian ? (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3]
                     : (std::uint32_t(p[3]) << 24) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[1]) << 8) | p[0];
}

}  // namespace

std::optional<Chunk> findChunk(const std::filesystem::path& path, ChunkLayout layout, const char (&id)[5]) {
    const bool big = (layout == ChunkLayout::Iff);

    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    const std::uint64_t fileSize = std::filesystem::file_size(path);

    // 12-byte file header: container magic, total size, form type.
    unsigned char head[12];
    if (!in.read(reinterpret_cast<char*>(head), 12)) return std::nullopt;
    if (big) {
        if (std::memcmp(head, "FORM", 4) != 0) return std::nullopt;
        if (std::memcmp(head + 8, "AIFF", 4) != 0 && std::memcmp(head + 8, "AIFC", 4) != 0) return std::nullopt;
    } else {
        if (std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVE", 4) != 0) return std::nullopt;
    }

    std::uint64_t pos = 12;
    while (pos + 8 <= fileSize) {
        in.seekg(static_cast<std::streamoff>(pos));
        unsigned char hdr[8];
        if (!in.read(reinterpret_cast<char*>(hdr), 8)) return std::nullopt;
        const std::uint64_t size = readU32(hdr + 4, big);
        if (std::memcmp(hdr, id, 4) == 0) return Chunk{pos + 8, size};
        pos += 8 + size + (size & 1);  // chunks are padded to an even length
    }
    return std::nullopt;
}

}  // namespace diskdive::detail
