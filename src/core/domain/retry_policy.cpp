#include "flowforge/domain/retry_policy.hpp"

#include <algorithm>
#include <cmath>

namespace flowforge::domain {

std::chrono::milliseconds RetryPolicy::delay_before(int next_attempt) const noexcept {
    if (next_attempt <= 1 || base_delay.count() <= 0) {
        return std::chrono::milliseconds{0};
    }
    const int exponent = next_attempt - 2;  // no backoff growth before attempt 2
    double factor = 1.0;
    if (backoff_multiplier > 0.0 && exponent > 0) {
        factor = std::pow(backoff_multiplier, static_cast<double>(exponent));
    }
    const double millis =
        std::round(static_cast<double>(base_delay.count()) * std::max(1.0, factor));
    const auto capped =
        std::min(static_cast<double>(max_delay.count()), std::max(0.0, millis));
    return std::chrono::milliseconds{static_cast<std::int64_t>(capped)};
}

}  // namespace flowforge::domain
