#include "TrackId.h"

#include <algorithm>
#include <fstream>
#include <vector>

#include "Sha256.h"

namespace diskdive {

TrackId computeTrackId(const AudioFile& file) {
    const AudioRegion region = file.audioRegion();

    std::ifstream in(file.path(), std::ios::binary);
    if (!in) throw TagError("Cannot read for hashing: " + file.path().string());
    in.seekg(static_cast<std::streamoff>(region.offset));

    Sha256 hash;
    std::vector<char> chunk(1 << 20);  // 1 MiB: big enough to be fast, small enough to stay out of the way
    std::uint64_t remaining = region.length;
    while (remaining > 0) {
        const auto want = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, chunk.size()));
        in.read(chunk.data(), want);
        const std::streamsize got = in.gcount();
        if (got != want) throw TagError("Short read while hashing: " + file.path().string());
        hash.update(chunk.data(), static_cast<std::size_t>(got));
        remaining -= static_cast<std::uint64_t>(got);
    }
    return "sha256:" + hash.finalHex();
}

}  // namespace diskdive
