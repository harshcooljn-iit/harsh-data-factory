#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace flowforge::util {

using Clock = std::chrono::system_clock;
using TimePoint = Clock::time_point;

/// Milliseconds since the Unix epoch for @p tp.
[[nodiscard]] std::int64_t to_unix_millis(TimePoint tp) noexcept;

/// Inverse of to_unix_millis().
[[nodiscard]] TimePoint from_unix_millis(std::int64_t millis) noexcept;

/// ISO-8601 UTC timestamp with millisecond precision, e.g.
/// "2026-08-29T12:34:56.789Z". Used for logs and CLI output.
[[nodiscard]] std::string to_iso8601(TimePoint tp);

/// Current wall-clock time.
[[nodiscard]] inline TimePoint now() noexcept {
    return Clock::now();
}

/// Seconds elapsed between two time points (never negative).
[[nodiscard]] double seconds_between(TimePoint start, TimePoint end) noexcept;

}  // namespace flowforge::util
