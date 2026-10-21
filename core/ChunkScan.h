#pragma once
// INTERNAL to core: finds a chunk inside a RIFF (WAV) or IFF (AIFF) file.
//
// Both formats are a sequence of chunks:  [4-byte id][4-byte size][size bytes of payload][pad byte if size is odd]
// RIFF/WAV stores sizes little-endian; IFF/AIFF stores them big-endian.
#include <cstdint>
#include <filesystem>
#include <optional>

namespace diskdive::detail {

struct Chunk {
    std::uint64_t payloadOffset = 0;  // where the payload starts in the file
    std::uint64_t payloadSize = 0;    // as declared in the chunk header (may exceed the file if it is truncated)
};

enum class ChunkLayout {
    Riff,  // "RIFF" ... "WAVE", little-endian sizes
    Iff    // "FORM" ... "AIFF" or "AIFC", big-endian sizes
};

// Returns nullopt if the file isn't of the given layout, or has no chunk with this 4-character id.
std::optional<Chunk> findChunk(const std::filesystem::path& path, ChunkLayout layout, const char (&id)[5]);

}  // namespace diskdive::detail
