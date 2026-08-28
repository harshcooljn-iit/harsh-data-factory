#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "flowforge/artifacts/artifact.hpp"
#include "flowforge/cache/cache_key.hpp"
#include "flowforge/cache/cache_store.hpp"
#include "flowforge/storage/database.hpp"
#include "flowforge/storage/repositories.hpp"
#include "flowforge/storage/schema.hpp"

namespace {

namespace fs = std::filesystem;
using namespace flowforge;
using flowforge::domain::TaskDefinition;
using flowforge::domain::TaskType;

TaskDefinition sample_task() {
    TaskDefinition t;
    t.id = "step";
    t.type = TaskType::kExecutable;
    t.program = "/bin/tool";
    t.arguments = {"a", "b"};
    t.environment = {{"Z", "1"}, {"A", "2"}};
    return t;
}

TEST(CacheKey, StableAcrossEnvOrderAndInputOrder) {
    auto t1 = sample_task();
    auto t2 = sample_task();
    std::reverse(t2.environment.begin(), t2.environment.end());

    artifacts::Artifact a;
    a.logical_name = "a";
    a.path = "/x/a";
    a.exists = true;
    a.size_bytes = 10;
    a.modified_unix_ms = 111;
    artifacts::Artifact b;
    b.logical_name = "b";
    b.path = "/x/b";
    b.exists = true;
    b.size_bytes = 20;
    b.modified_unix_ms = 222;

    const auto k1 = cache::compute_cache_key(t1, {a, b});
    const auto k2 = cache::compute_cache_key(t2, {b, a});
    EXPECT_EQ(k1, k2);
    EXPECT_EQ(k1.rfind("ffcache1:", 0), 0u);
}

TEST(CacheKey, ChangesWhenArgumentsOrInputChange) {
    auto t = sample_task();
    const auto base = cache::compute_cache_key(t, {});

    auto t2 = t;
    t2.arguments.push_back("c");
    EXPECT_NE(base, cache::compute_cache_key(t2, {}));

    artifacts::Artifact in;
    in.logical_name = "in";
    in.path = "/x/in";
    in.exists = true;
    in.size_bytes = 1;
    const auto with_in = cache::compute_cache_key(t, {in});
    in.size_bytes = 2;
    EXPECT_NE(with_in, cache::compute_cache_key(t, {in}));
}

TEST(CacheKey, IgnoresNamePriorityRetryResources) {
    auto t = sample_task();
    const auto base = cache::compute_cache_key(t, {});
    t.name = "different";
    t.priority = 99;
    t.retry.max_retries = 5;
    t.resources.cpu_cores = 8;
    EXPECT_EQ(base, cache::compute_cache_key(t, {}));
}

struct CacheStoreTest : ::testing::Test {
    storage::Database db{":memory:"};
    fs::path dir;
    void SetUp() override {
        storage::migrate_to_latest(db);
        dir = fs::temp_directory_path() /
              ("ff_cache_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(dir);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    void write(const std::string& name, std::string_view content) {
        std::ofstream(dir / name, std::ios::binary) << content;
    }
    cache::CachedArtifact snapshot(const std::string& name, bool with_checksum) {
        const auto art = artifacts::probe_artifact(name, dir / name, with_checksum);
        cache::CachedArtifact s;
        s.logical_name = name;
        s.path = (dir / name).string();
        s.size_bytes = art.size_bytes;
        s.modified_unix_ms = art.modified_unix_ms;
        s.checksum = art.checksum;
        return s;
    }
};

TEST_F(CacheStoreTest, StoreThenHitThenInputChangeMiss) {
    storage::CacheRepository repo(db);
    cache::CacheStore store(repo, /*enabled=*/true);

    write("out.txt", "result-v1");
    cache::CachedOutcome outcome;
    outcome.exit_code = 0;
    outcome.outputs = {snapshot("out.txt", /*with_checksum=*/true)};

    store.store("key-A", "step", outcome, 1000);

    // Same key -> hit, outputs still valid.
    auto hit = store.lookup("key-A", 2000);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->exit_code, 0);
    ASSERT_EQ(hit->outputs.size(), 1u);
    EXPECT_EQ(repo.lookup("key-A")->hit_count, 1);

    // Output file changes on disk -> entry is stale -> miss + removed.
    write("out.txt", "tampered");
    EXPECT_FALSE(store.lookup("key-A", 3000).has_value());
    EXPECT_FALSE(repo.lookup("key-A").has_value());
}

TEST_F(CacheStoreTest, DoesNotStoreFailedExecutions) {
    storage::CacheRepository repo(db);
    cache::CacheStore store(repo, true);
    cache::CachedOutcome bad;
    bad.exit_code = 1;
    store.store("key-fail", "step", bad, 1000);
    EXPECT_FALSE(repo.lookup("key-fail").has_value());
}

TEST_F(CacheStoreTest, DisabledStoreIsInert) {
    storage::CacheRepository repo(db);
    cache::CacheStore store(repo, /*enabled=*/false);
    write("o", "x");
    cache::CachedOutcome outcome;
    outcome.outputs = {snapshot("o", false)};
    store.store("k", "step", outcome, 1);
    EXPECT_FALSE(store.lookup("k", 2).has_value());
    EXPECT_FALSE(repo.lookup("k").has_value());
}

TEST_F(CacheStoreTest, MissingOutputFileInvalidatesEntry) {
    storage::CacheRepository repo(db);
    cache::CacheStore store(repo, true);
    write("gone.txt", "here");
    cache::CachedOutcome outcome;
    outcome.exit_code = 0;
    outcome.outputs = {snapshot("gone.txt", false)};
    store.store("k", "step", outcome, 1);

    fs::remove(dir / "gone.txt");
    EXPECT_FALSE(store.lookup("k", 2).has_value());
}

TEST(CacheOutputs, SerializeRoundTrip) {
    std::vector<cache::CachedArtifact> outs;
    cache::CachedArtifact a;
    a.logical_name = "model";
    a.path = "/tmp/model.bin";
    a.size_bytes = 42;
    a.modified_unix_ms = 99;
    a.checksum = std::string(64, 'a');
    outs.push_back(a);

    const auto parsed = cache::parse_outputs(cache::serialize_outputs(outs));
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].logical_name, "model");
    EXPECT_EQ(parsed[0].size_bytes, 42);
    EXPECT_EQ(parsed[0].checksum.value(), std::string(64, 'a'));
}

}  // namespace
