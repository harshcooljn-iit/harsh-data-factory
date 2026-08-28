#pragma once

#include <chrono>
#include <cstdint>

namespace flowforge::domain {

// ---------------------------------------------------------------------------
// RetryPolicy
//
// Attempts are 1-based. max_retries is the number of *additional* attempts
// after the first, so total attempts = max_retries + 1.
//
// Backoff between attempt N and N+1:
//   delay = base_delay * backoff_multiplier^(N-1), capped at max_delay.
//
// Only failures the executor classifies as retryable are retried (a process
// that ran and exited non-zero, a timeout, a crash). Deterministic
// configuration errors -- missing interpreter, missing declared input -- are
// never retried regardless of this policy. See docs/error-handling.md.
// ---------------------------------------------------------------------------
struct RetryPolicy {
    int max_retries = 0;
    std::chrono::milliseconds base_delay{0};
    double backoff_multiplier = 1.0;
    std::chrono::milliseconds max_delay{std::chrono::minutes{5}};

    [[nodiscard]] static RetryPolicy none() noexcept { return RetryPolicy{}; }

    /// Total number of attempts permitted (>= 1).
    [[nodiscard]] int max_attempts() const noexcept {
        return max_retries < 0 ? 1 : max_retries + 1;
    }

    /// True if another attempt is allowed after @p attempts_made have completed.
    [[nodiscard]] bool should_retry(int attempts_made) const noexcept {
        return attempts_made >= 1 && attempts_made < max_attempts();
    }

    /// Backoff delay to apply before attempt number @p next_attempt (2-based:
    /// the delay before the 2nd attempt uses next_attempt == 2).
    [[nodiscard]] std::chrono::milliseconds delay_before(int next_attempt) const noexcept;

    [[nodiscard]] bool operator==(const RetryPolicy&) const = default;
};

}  // namespace flowforge::domain
