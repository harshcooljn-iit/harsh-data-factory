#include "flowforge/util/string_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace flowforge::util {

namespace {
bool is_space(char c) noexcept {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}
}  // namespace

std::string_view trim(std::string_view text) noexcept {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && is_space(text[begin])) {
        ++begin;
    }
    while (end > begin && is_space(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::vector<std::string> split(std::string_view text, char delimiter) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = text.find(delimiter, start);
        if (pos == std::string_view::npos) {
            out.emplace_back(text.substr(start));
            break;
        }
        out.emplace_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view separator) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            out.append(separator);
        }
        out.append(parts[i]);
    }
    return out;
}

bool starts_with(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(std::string_view text, std::string_view suffix) noexcept {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string to_lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string format_duration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) {
        return "0.00s";
    }
    char buffer[64];
    if (seconds < 60.0) {
        std::snprintf(buffer, sizeof(buffer), "%.2fs", seconds);
        return buffer;
    }
    const int minutes = static_cast<int>(seconds / 60.0);
    const double rem = seconds - (minutes * 60.0);
    std::snprintf(buffer, sizeof(buffer), "%dm%04.1fs", minutes, rem);
    return buffer;
}

}  // namespace flowforge::util
