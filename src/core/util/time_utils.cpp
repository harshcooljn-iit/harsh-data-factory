#include "flowforge/util/time_utils.hpp"

#include <array>
#include <cstdio>
#include <ctime>

namespace flowforge::util {

std::int64_t to_unix_millis(TimePoint tp) noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch())
        .count();
}

TimePoint from_unix_millis(std::int64_t millis) noexcept {
    return TimePoint{std::chrono::milliseconds{millis}};
}

std::string to_iso8601(TimePoint tp) {
    const auto millis = to_unix_millis(tp);
    const std::time_t secs = static_cast<std::time_t>(millis / 1000);
    const int ms = static_cast<int>(millis % 1000);

    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &secs);
#else
    gmtime_r(&secs, &tm_utc);
#endif

    std::array<char, 32> buf{};
    std::snprintf(buf.data(), buf.size(), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday, tm_utc.tm_hour,
                  tm_utc.tm_min, tm_utc.tm_sec, ms);
    return std::string(buf.data());
}

double seconds_between(TimePoint start, TimePoint end) noexcept {
    if (end <= start) {
        return 0.0;
    }
    return std::chrono::duration<double>(end - start).count();
}

}  // namespace flowforge::util
