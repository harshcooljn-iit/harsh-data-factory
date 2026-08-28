#pragma once

#include <optional>
#include <string>

#include "flowforge/domain/resource_requirements.hpp"

namespace flowforge::engine {

// ---------------------------------------------------------------------------
// Config -- resolved engine settings.
//
// Resolution order (lowest precedence first), documented in
// docs/development.md:
//   1. built-in defaults (Config::defaults())
//   2. a JSON config file: <state_dir>/config.json, else ./flowforge.json
//   3. environment variables (FLOWFORGE_*)
//   4. explicit CLI flags (applied by the CLI after load())
// ---------------------------------------------------------------------------
struct Config {
    /// Directory for all FlowForge local state (database, cache sentinel dir).
    std::string state_dir = ".flowforge";

    int max_concurrency = 4;
    std::string python_interpreter = "python3";

    std::string database_path;  ///< empty => <state_dir>/flowforge.sqlite
    bool cache_enabled = true;
    bool compute_checksums = false;
    bool verify_outputs = true;

    domain::ResourcePool resource_capacity{};  ///< machine capacity for scheduling
    std::string log_level = "info";            ///< trace|debug|info|warn|error|off

    [[nodiscard]] static Config defaults();

    struct LoadOptions {
        std::string working_dir = ".";
        std::optional<std::string> explicit_config_file;
    };

    /// Apply file + environment overrides on top of defaults(). Never throws;
    /// unreadable / malformed config files are ignored (a warning is the CLI's
    /// job) and defaults stand.
    [[nodiscard]] static Config load(const LoadOptions& options);

    /// Absolute-ish path to the SQLite file (respects database_path override).
    [[nodiscard]] std::string resolved_database_path() const;

    /// Directory used for cross-process cancellation sentinels.
    [[nodiscard]] std::string cancel_dir() const;
};

}  // namespace flowforge::engine
