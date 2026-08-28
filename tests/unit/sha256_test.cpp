#include <gtest/gtest.h>

#include <fstream>

#include "flowforge/util/sha256.hpp"

namespace {

using flowforge::util::Sha256;
using flowforge::util::sha256_file;
using flowforge::util::sha256_hex;

// Reference vectors from FIPS 180-2 / common knowledge.
TEST(Sha256, KnownVectors) {
    EXPECT_EQ(sha256_hex(""),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256_hex("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256, StreamingMatchesOneShot) {
    Sha256 h;
    h.update("hello ");
    h.update("world");
    EXPECT_EQ(h.hex_digest(), sha256_hex("hello world"));
}

TEST(Sha256, ObjectIsReusableAfterDigest) {
    Sha256 h;
    h.update("abc");
    EXPECT_EQ(h.hex_digest(),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    h.update("");
    EXPECT_EQ(h.hex_digest(),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256, LargeInputCrossesBlockBoundary) {
    // 1,000,000 'a' characters -> the classic SHA-256 test vector.
    const std::string big(1'000'000, 'a');
    EXPECT_EQ(sha256_hex(big),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    // Feeding the same bytes in awkward 7-byte chunks must agree.
    Sha256 h;
    for (std::size_t i = 0; i < big.size(); i += 7) {
        h.update(big.data() + i, std::min<std::size_t>(7, big.size() - i));
    }
    EXPECT_EQ(h.hex_digest(), sha256_hex(big));
}

TEST(Sha256, FileDigestMatchesStringDigest) {
    const auto path = std::filesystem::temp_directory_path() / "flowforge_sha_test.bin";
    {
        std::ofstream out(path, std::ios::binary);
        out << "the quick brown fox";
    }
    const auto digest = sha256_file(path);
    ASSERT_TRUE(digest.has_value());
    EXPECT_EQ(*digest, sha256_hex("the quick brown fox"));
    std::filesystem::remove(path);

    EXPECT_FALSE(sha256_file(path).has_value());  // gone now
}

}  // namespace
