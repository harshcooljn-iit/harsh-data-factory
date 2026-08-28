#include "flowforge/engine/config.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;
using nlohmann::json;

namespace flowforge::engine {

namespace {

int detect_cpu_count() {
    const unsigned hc = std::thread::hardware_concurrency();
    return hc == 0 ? 4 : static_cast<int>(hc);
}

std::optional<std::string> env(const char* name) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') {
        return std::nullopt;
    }
    return std::string(v);
}

std::optional<int> env_int(const char* name) {
    const auto v = env(name);
    if (!v) {
        return std::nullopt;
    }
    try {
        return std::stoi(*v);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<bool> env_bool(const char* name) {
    auto v = env(name);
    if (!v) {
        return std::nullopt;
    }
    std::transform(v->begin(), v->end(), v->begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (*v == "1" || *v == "true" || *v == "yes" || *v == "on") {
        return true;
    }
    if (*v == "0" || *v == "false" || *v == "no" || *v == "off") {
        return false;
    }
    return std::nullopt;
}

void apply_json(Config& cfg, const json& doc) {
    if (!doc.is_object()) {
        return;
    }
    if (doc.contains("state_dir") && doc["state_dir"].is_string()) {
        cfg.state_dir = doc["state_dir"].get<std::string>();
    }
    if (doc.contains("max_concurrency") && doc["max_concurrency"].is_number_integer()) {
        cfg.max_concurrency = doc["max_concurrency"].get<int>();
    }
    if (doc.contains("python_interpreter") && doc["python_interpreter"].is_string()) {
        cfg.python_interpreter = doc["python_interpreter"].get<std::string>();
    }
    if (doc.contains("database_path") && doc["database_path"].is_string()) {
        cfg.database_path = doc["database_path"].get<std::string>();
    }
    if (doc.contains("cache_enabled") && doc["cache_enabled"].is_boolean()) {
        cfg.cache_enabled = doc["cache_enabled"].get<bool>();
    }
    if (doc.contains("compute_checksums") && doc["compute_checksums"].is_boolean()) {
        cfg.compute_checksums = doc["compute_checksums"].get<bool>();
    }
    if (doc.contains("verify_outputs") && doc["verify_outputs"].is_boolean()) {
        cfg.verify_outputs = doc["verify_outputs"].get<bool>();
    }
    if (doc.contains("log_level") && doc["log_level"].is_string()) {
        cfg.log_level = doc["log_level"].get<std::string>();
    }
    if (doc.contains("resources") && doc["resources"].is_object()) {
        const auto& r = doc["resources"];
        if (r.contains("cpu_cores") && r["cpu_cores"].is_number_integer()) {
            cfg.resource_capacity.total_cpu_cores = r["cpu_cores"].get<int>();
        }
        if (r.contains("memory_mb") && r["memory_mb"].is_number_integer()) {
            cfg.resource_capacity.total_memory_mb = r["memory_mb"].get<std::int64_t>();
        }
        if (r.contains("gpu_count") && r["gpu_count"].is_number_integer()) {
            cfg.resource_capacity.total_gpu_count = r["gpu_count"].get<int>();
        }
    }
}

}  // namespace

Config Config::defaults() {
    Config cfg;
    const int cpus = detect_cpu_count();
    cfg.max_concurrency = std::min(cpus, 8);
    cfg.resource_capacity.total_cpu_cores = cpus;
    // Memory is modelled, not measured: default to a generous ceiling so tasks
    // that declare memory still schedule unless the pipeline lowers it.
    cfg.resource_capacity.total_memory_mb = 1LL << 20;  // 1 TiB nominal
    cfg.resource_capacity.total_gpu_count = 0;
    return cfg;
}

Config Config::load(const LoadOptions& options) {
    Config cfg = defaults();

    // --- config file ---
    std::vector<fs::path> candidates;
    if (options.explicit_config_file) {
        candidates.emplace_back(*options.explicit_config_file);
    } else {
        candidates.emplace_back(fs::path(options.working_dir) / cfg.state_dir / "config.json");
        candidates.emplace_back(fs::path(options.working_dir) / "flowforge.json");
    }
    for (const auto& path : candidates) {
        std::ifstream in(path);
        if (!in) {
            continue;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        const json doc = json::parse(ss.str(), nullptr, false);
        if (!doc.is_discarded()) {
            apply_json(cfg, doc);
        }
        break;
    }

    // --- environment ---
    if (const auto v = env("FLOWFORGE_STATE_DIR")) {
        cfg.state_dir = *v;
    }
    if (const auto v = env_int("FLOWFORGE_MAX_CONCURRENCY")) {
        cfg.max_concurrency = *v;
    }
    if (const auto v = env("FLOWFORGE_PYTHON")) {
        cfg.python_interpreter = *v;
    }
    if (const auto v = env("FLOWFORGE_DB")) {
        cfg.database_path = *v;
    }
    if (const auto v = env_bool("FLOWFORGE_CACHE")) {
        cfg.cache_enabled = *v;
    }
    if (const auto v = env_bool("FLOWFORGE_CHECKSUMS")) {
        cfg.compute_checksums = *v;
    }
    if (const auto v = env("FLOWFORGE_LOG_LEVEL")) {
        cfg.log_level = *v;
    }
    if (const auto v = env_int("FLOWFORGE_CPU_CORES")) {
        cfg.resource_capacity.total_cpu_cores = *v;
    }

    if (cfg.max_concurrency < 1) {
        cfg.max_concurrency = 1;
    }
    if (cfg.resource_capacity.total_cpu_cores < 1) {
        cfg.resource_capacity.total_cpu_cores = 1;
    }
    return cfg;
}

std::string Config::resolved_database_path() const {
    if (!database_path.empty()) {
        return database_path;
    }
    return (fs::path(state_dir) / "flowforge.sqlite").string();
}

std::string Config::cancel_dir() const {
    return (fs::path(state_dir) / "cancel").string();
}

}  // namespace flowforge::engine
