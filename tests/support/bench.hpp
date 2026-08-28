#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace flowforge::test {

// Minimal benchmark helper: runs `body` `iterations` times, prints the median
// and total wall time, and returns the median in milliseconds. No statistical
// pretence -- just enough to record honest ballpark numbers.
inline double bench(const std::string& name, int iterations,
                    const std::function<void()>& body) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(iterations));
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        const auto a = std::chrono::steady_clock::now();
        body();
        const auto b = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(b - a).count());
    }
    const auto t1 = std::chrono::steady_clock::now();
    std::sort(samples.begin(), samples.end());
    const double median = samples[samples.size() / 2];
    const double total = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("  %-46s median %9.3f ms   total %9.3f ms   (n=%d)\n", name.c_str(), median,
                total, iterations);
    return median;
}

}  // namespace flowforge::test
