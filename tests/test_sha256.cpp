// Checks our SHA-256 against the official test vectors from FIPS 180-4 / NIST.
// If these pass, our digests will match any other SHA-256 tool in the world.
#include <string>

#include "Sha256.h"
#include "TestFramework.h"

using diskdive::Sha256;

static std::string sha(const std::string& s) {
    Sha256 h;
    h.update(s.data(), s.size());
    return h.finalHex();
}

TEST(sha256_empty_string) {
    CHECK_EQ(sha(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

TEST(sha256_abc) {
    CHECK_EQ(sha("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

// 56 bytes: the padding no longer fits in the same 64-byte block, so a second block is needed.
TEST(sha256_two_block_message) {
    CHECK_EQ(sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

TEST(sha256_one_million_a) {
    CHECK_EQ(sha(std::string(1000000, 'a')),
             std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

// We feed files in 1 MiB chunks, so splitting the input arbitrarily must never change the result.
TEST(sha256_chunking_does_not_matter) {
    const std::string data(1000, 'z');
    Sha256 h;
    std::size_t pos = 0;
    for (std::size_t step : {1u, 7u, 64u, 65u, 300u}) {
        const std::size_t n = std::min<std::size_t>(step, data.size() - pos);
        h.update(data.data() + pos, n);
        pos += n;
    }
    h.update(data.data() + pos, data.size() - pos);
    CHECK_EQ(h.finalHex(), sha(data));
}
