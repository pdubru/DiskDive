#pragma once
// Minimal streaming SHA-256 (FIPS 180-4). Self-contained so the core has no extra
// dependency just for identifying tracks. Feed data in any chunk size with update(),
// then call finalHex() once.
#include <cstddef>
#include <cstdint>
#include <string>

namespace diskdive {

class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t size);
    std::string finalHex();  // 64 lower-case hex chars; the object must not be reused afterwards

private:
    void processBlock(const std::uint8_t* block);

    std::uint32_t state_[8];
    std::uint8_t buffer_[64];
    std::size_t bufferLen_ = 0;
    std::uint64_t totalBytes_ = 0;
};

}  // namespace diskdive
