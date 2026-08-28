#include "args.hpp"

#include <cstdlib>

namespace flowforge::cli {

namespace {
bool looks_like_option(const std::string& s) {
    return s.size() >= 2 && s[0] == '-' && s[1] == '-';
}
}  // namespace

Args::Args(int argc, char** argv, int start) {
    for (int i = start; i < argc; ++i) {
        std::string tok = argv[i];
        if (!looks_like_option(tok)) {
            positionals_.push_back(std::move(tok));
            continue;
        }
        std::string name = tok.substr(2);
        const auto eq = name.find('=');
        if (eq != std::string::npos) {
            options_[name.substr(0, eq)] = name.substr(eq + 1);
            continue;
        }
        // Look ahead: consume a value unless the next token is another option.
        if (i + 1 < argc) {
            const std::string next = argv[i + 1];
            if (!looks_like_option(next)) {
                options_[name] = next;
                ++i;
                continue;
            }
        }
        options_[name] = "true";
    }
}

std::optional<std::string> Args::positional(std::size_t index) const {
    if (index >= positionals_.size()) {
        return std::nullopt;
    }
    return positionals_[index];
}

bool Args::flag(const std::string& name) const {
    consumed_.insert(name);
    const auto it = options_.find(name);
    if (it == options_.end()) {
        return false;
    }
    return it->second == "true" || it->second == "1" || it->second == "yes";
}

std::optional<std::string> Args::value(const std::string& name) const {
    consumed_.insert(name);
    const auto it = options_.find(name);
    if (it == options_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<long> Args::int_value(const std::string& name) const {
    const auto v = value(name);
    if (!v) {
        return std::nullopt;
    }
    char* end = nullptr;
    const long parsed = std::strtol(v->c_str(), &end, 10);
    if (end == v->c_str() || *end != '\0') {
        return std::nullopt;
    }
    return parsed;
}

std::vector<std::string> Args::unconsumed() const {
    std::vector<std::string> out;
    for (const auto& [key, value] : options_) {
        (void)value;
        if (consumed_.find(key) == consumed_.end()) {
            out.push_back(key);
        }
    }
    return out;
}

}  // namespace flowforge::cli
