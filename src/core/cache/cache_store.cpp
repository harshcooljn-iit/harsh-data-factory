#include "flowforge/cache/cache_store.hpp"

#include <nlohmann/json.hpp>

#include "flowforge/artifacts/artifact.hpp"
#include "flowforge/storage/repositories.hpp"

using nlohmann::json;

namespace flowforge::cache {

std::string serialize_outputs(const std::vector<CachedArtifact>& outputs) {
    json arr = json::array();
    for (const auto& o : outputs) {
        json j;
        j["name"] = o.logical_name;
        j["path"] = o.path;
        j["size"] = o.size_bytes;
        j["mtime"] = o.modified_unix_ms;
        if (o.checksum) {
            j["sha256"] = *o.checksum;
        }
        arr.push_back(std::move(j));
    }
    return json{{"outputs", std::move(arr)}}.dump();
}

std::vector<CachedArtifact> parse_outputs(const std::string& json_text) {
    std::vector<CachedArtifact> out;
    const json doc = json::parse(json_text, nullptr, false);
    if (doc.is_discarded() || !doc.contains("outputs") || !doc["outputs"].is_array()) {
        return out;
    }
    for (const auto& j : doc["outputs"]) {
        CachedArtifact a;
        a.logical_name = j.value("name", std::string{});
        a.path = j.value("path", std::string{});
        a.size_bytes = j.value("size", std::int64_t{0});
        a.modified_unix_ms = j.value("mtime", std::int64_t{0});
        if (j.contains("sha256") && j["sha256"].is_string()) {
            a.checksum = j["sha256"].get<std::string>();
        }
        out.push_back(std::move(a));
    }
    return out;
}

namespace {
bool output_still_valid(const CachedArtifact& snap) {
    const auto probed =
        artifacts::probe_artifact(snap.logical_name, snap.path, snap.checksum.has_value());
    if (!probed.exists) {
        return false;
    }
    if (snap.checksum) {
        return probed.checksum && *probed.checksum == *snap.checksum;
    }
    return probed.size_bytes == snap.size_bytes;
}
}  // namespace

std::optional<CachedOutcome> CacheStore::lookup(const std::string& cache_key,
                                                std::int64_t now_ms) {
    if (!enabled_) {
        return std::nullopt;
    }
    const auto rec = repo_->lookup(cache_key);
    if (!rec) {
        return std::nullopt;
    }

    CachedOutcome outcome;
    outcome.exit_code = rec->exit_code;
    outcome.outputs = parse_outputs(rec->outputs_json);

    for (const auto& snap : outcome.outputs) {
        if (!output_still_valid(snap)) {
            // Stale: a declared output was deleted or changed since the entry
            // was written. Drop it so it is never reused.
            repo_->remove(cache_key);
            return std::nullopt;
        }
    }

    repo_->touch(cache_key, now_ms);
    return outcome;
}

void CacheStore::store(const std::string& cache_key,
                       const std::string& task_id,
                       const CachedOutcome& outcome,
                       std::int64_t now_ms) {
    if (!enabled_ || outcome.exit_code != 0) {
        return;
    }
    repo_->store(cache_key, task_id, outcome.exit_code, serialize_outputs(outcome.outputs),
                 now_ms);
}

}  // namespace flowforge::cache
