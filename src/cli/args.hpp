#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace flowforge::cli {

// ---------------------------------------------------------------------------
// Tiny option parser. Accepts:  --flag   --key value   --key=value
// A "--key" whose next token starts with '-' (or is absent) is treated as a
// boolean flag. Everything else is a positional argument. Deliberately small;
// FlowForge does not need a full getopt.
// ---------------------------------------------------------------------------
class Args {
public:
    Args(int argc, char** argv, int start);

    [[nodiscard]] const std::vector<std::string>& positionals() const noexcept {
        return positionals_;
    }
    [[nodiscard]] std::optional<std::string> positional(std::size_t index) const;

    [[nodiscard]] bool flag(const std::string& name) const;
    [[nodiscard]] std::optional<std::string> value(const std::string& name) const;
    [[nodiscard]] std::optional<long> int_value(const std::string& name) const;

    /// Option names that were supplied but never queried -- lets a command
    /// reject typos instead of silently ignoring them.
    [[nodiscard]] std::vector<std::string> unconsumed() const;

private:
    std::vector<std::string> positionals_;
    std::unordered_map<std::string, std::string> options_;
    mutable std::unordered_set<std::string> consumed_;
};

}  // namespace flowforge::cli
