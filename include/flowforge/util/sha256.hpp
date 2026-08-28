#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace flowforge::util {

// ---------------------------------------------------------------------------
// Small dependency-free SHA-256. FlowForge uses content hashes for cache keys
// and (optionally) artifact identity; it does not need a crypto library for
// that. Streaming API so large files hash without being read into memory.
// ---------------------------------------------------------------------------
class Sha256 {
  public:
    Sha256() noexcept { reset(); }

    void reset() noexcept;
    void update(const void* data, std::size_t len) noexcept;
    void update(std::string_view text) noexcept { update(text.data(), text.size()); }

    /// Finalise and return the lowercase hex digest (64 chars). The object is
    /// reset afterwards and can be reused.
    [[nodiscard]] std::string hex_digest() noexcept;

  private:
    void process_block(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t bit_len_ = 0;
    std::size_t buffer_len_ = 0;
};

/// Convenience: hex digest of a string.
[[nodiscard]] std::string sha256_hex(std::string_view text);

/// Hex digest of a file's contents, or nullopt if it cannot be read.
[[nodiscard]] std::optional<std::string> sha256_file(const std::filesystem::path& path);

}  // namespace flowforge::util
