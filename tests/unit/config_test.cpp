#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "flowforge/engine/config.hpp"

namespace {

namespace fs = std::filesystem;
using flowforge::engine::Config;

TEST(Config, DefaultsAreSane) {
    const auto c = Config::defaults();
    EXPECT_GE(c.max_concurrency, 1);
    EXPECT_GE(c.resource_capacity.total_cpu_cores, 1);
    EXPECT_EQ(c.python_interpreter, "python3");
    EXPECT_TRUE(c.cache_enabled);
}

TEST(Config, ResolvedDatabasePathHonoursOverride) {
    Config c = Config::defaults();
    c.state_dir = "/tmp/ff-state";
    EXPECT_EQ(c.resolved_database_path(), "/tmp/ff-state/flowforge.sqlite");
    c.database_path = "/var/db/custom.sqlite";
    EXPECT_EQ(c.resolved_database_path(), "/var/db/custom.sqlite");
}

TEST(Config, LoadsFromJsonFile) {
    const auto dir = fs::temp_directory_path() / "ff_cfg_test";
    fs::create_directories(dir);
    {
        std::ofstream out(dir / "flowforge.json");
        out << R"({"max_concurrency": 2, "python_interpreter": "python3.12",
                   "cache_enabled": false, "resources": {"cpu_cores": 3}})";
    }
    Config::LoadOptions opts;
    opts.working_dir = dir.string();
    const auto c = Config::load(opts);
    EXPECT_EQ(c.max_concurrency, 2);
    EXPECT_EQ(c.python_interpreter, "python3.12");
    EXPECT_FALSE(c.cache_enabled);
    EXPECT_EQ(c.resource_capacity.total_cpu_cores, 3);
    fs::remove_all(dir);
}

TEST(Config, EnvironmentOverridesFile) {
    const auto dir = fs::temp_directory_path() / "ff_cfg_env_test";
    fs::create_directories(dir);
    {
        std::ofstream out(dir / "flowforge.json");
        out << R"({"max_concurrency": 2})";
    }
    ::setenv("FLOWFORGE_MAX_CONCURRENCY", "9", 1);
    ::setenv("FLOWFORGE_PYTHON", "/opt/py/bin/python3", 1);
    Config::LoadOptions opts;
    opts.working_dir = dir.string();
    const auto c = Config::load(opts);
    EXPECT_EQ(c.max_concurrency, 9);
    EXPECT_EQ(c.python_interpreter, "/opt/py/bin/python3");
    ::unsetenv("FLOWFORGE_MAX_CONCURRENCY");
    ::unsetenv("FLOWFORGE_PYTHON");
    fs::remove_all(dir);
}

TEST(Config, MalformedFileIsIgnored) {
    const auto dir = fs::temp_directory_path() / "ff_cfg_bad_test";
    fs::create_directories(dir);
    { std::ofstream(dir / "flowforge.json") << "{ this is not json"; }
    Config::LoadOptions opts;
    opts.working_dir = dir.string();
    const auto c = Config::load(opts);  // must not throw
    EXPECT_GE(c.max_concurrency, 1);
    fs::remove_all(dir);
}

}  // namespace
