#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace flowforge::util {

/// Trim ASCII whitespace from both ends.
[[nodiscard]] std::string_view trim(std::string_view text) noexcept;

/// Split @p text on @p delimiter. Empty fields are preserved.
[[nodiscard]] std::vector<std::string> split(std::string_view text, char delimiter);

/// Join @p parts with @p separator between elements.
[[nodiscard]] std::string join(const std::vector<std::string>& parts,
                               std::string_view separator);

/// True if @p text starts / ends with the given affix.
[[nodiscard]] bool starts_with(std::string_view text, std::string_view prefix) noexcept;
[[nodiscard]] bool ends_with(std::string_view text, std::string_view suffix) noexcept;

/// Lower-case a copy of @p text (ASCII only).
[[nodiscard]] std::string to_lower(std::string_view text);

/// Render @p seconds as a compact human string, e.g. "1.23s" or "2m05.1s".
[[nodiscard]] std::string format_duration(double seconds);

}  // namespace flowforge::util
