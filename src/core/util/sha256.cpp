#include "flowforge/util/sha256.hpp"

#include <cstdio>
#include <cstring>

namespace flowforge::util {

namespace {

constexpr std::array<std::uint32_t, 64> kK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

constexpr std::uint32_t rotr(std::uint32_t x, std::uint32_t n) noexcept {
    return (x >> n) | (x << (32 - n));
}

}  // namespace

void Sha256::reset() noexcept {
    state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    bit_len_ = 0;
    buffer_len_ = 0;
}

void Sha256::process_block(const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + ch + kK[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const void* data, std::size_t len) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    bit_len_ += static_cast<std::uint64_t>(len) * 8;
    while (len > 0) {
        const std::size_t take = std::min<std::size_t>(len, 64 - buffer_len_);
        std::memcpy(buffer_.data() + buffer_len_, bytes, take);
        buffer_len_ += take;
        bytes += take;
        len -= take;
        if (buffer_len_ == 64) {
            process_block(buffer_.data());
            buffer_len_ = 0;
        }
    }
}

std::string Sha256::hex_digest() noexcept {
    // Padding: 0x80, then zeros, then 64-bit big-endian bit length.
    const std::uint64_t total_bits = bit_len_;
    std::uint8_t pad = 0x80;
    update(&pad, 1);
    pad = 0x00;
    while (buffer_len_ != 56) {
        update(&pad, 1);
    }
    std::array<std::uint8_t, 8> len_be{};
    for (std::size_t i = 0; i < 8; ++i) {
        len_be[i] = static_cast<std::uint8_t>((total_bits >> (56 - i * 8)) & 0xff);
    }
    update(len_be.data(), len_be.size());

    std::string out;
    out.resize(64);
    for (std::size_t i = 0; i < 8; ++i) {
        std::snprintf(out.data() + i * 8, 9, "%08x", state_[i]);
    }
    reset();
    return out;
}

std::string sha256_hex(std::string_view text) {
    Sha256 h;
    h.update(text);
    return h.hex_digest();
}

std::optional<std::string> sha256_file(const std::filesystem::path& path) {
    std::FILE* fp = std::fopen(path.string().c_str(), "rb");
    if (fp == nullptr) {
        return std::nullopt;
    }
    Sha256 h;
    std::array<std::uint8_t, 64 * 1024> chunk{};
    std::size_t n = 0;
    while ((n = std::fread(chunk.data(), 1, chunk.size(), fp)) > 0) {
        h.update(chunk.data(), n);
    }
    const bool read_error = std::ferror(fp) != 0;
    std::fclose(fp);
    if (read_error) {
        return std::nullopt;
    }
    return h.hex_digest();
}

}  // namespace flowforge::util
