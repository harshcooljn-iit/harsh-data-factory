#include "flowforge/process/process_spec.hpp"

#include <cstdlib>
#include <unordered_map>

#include "flowforge/process/process_result.hpp"

extern "C" char** environ;

namespace flowforge::process {

std::string_view to_string(ProcessOutcome outcome) noexcept {
    switch (outcome) {
        case ProcessOutcome::kExited:
            return "exited";
        case ProcessOutcome::kSignalled:
            return "signalled";
        case ProcessOutcome::kTimedOut:
            return "timed_out";
        case ProcessOutcome::kCancelled:
            return "cancelled";
        case ProcessOutcome::kSpawnFailed:
            return "spawn_failed";
    }
    return "unknown";
}

std::optional<std::string> ProcessSpec::validate() const {
    if (program.empty()) {
        return "process spec has an empty program";
    }
    if (argv.empty()) {
        return "process spec has an empty argv (argv[0] is required)";
    }
    if (timeout && timeout->count() < 0) {
        return "process spec has a negative timeout";
    }
    return std::nullopt;
}

std::vector<std::string> build_environment_block(const std::vector<EnvEntry>& overrides,
                                                 bool inherit_current) {
    // Preserve first-seen order for reproducibility; overrides replace in place.
    std::vector<std::string> keys;
    std::unordered_map<std::string, std::string> values;

    auto put = [&](const std::string& key, const std::string& value) {
        if (values.find(key) == values.end()) {
            keys.push_back(key);
        }
        values[key] = value;
    };

    if (inherit_current && environ != nullptr) {
        for (char** e = environ; *e != nullptr; ++e) {
            const std::string entry(*e);
            const auto eq = entry.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            put(entry.substr(0, eq), entry.substr(eq + 1));
        }
    }
    for (const auto& [key, value] : overrides) {
        if (key.empty() || key.find('=') != std::string::npos) {
            continue;  // skip malformed keys rather than corrupt the block
        }
        put(key, value);
    }

    std::vector<std::string> block;
    block.reserve(keys.size());
    for (const auto& key : keys) {
        block.push_back(key + "=" + values[key]);
    }
    return block;
}

}  // namespace flowforge::process
