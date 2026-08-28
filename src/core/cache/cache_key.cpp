#include "flowforge/cache/cache_key.hpp"

#include <algorithm>

#include "flowforge/domain/enums.hpp"
#include "flowforge/util/sha256.hpp"

namespace flowforge::cache {

std::string compute_cache_key(const domain::TaskDefinition& task,
                              const std::vector<artifacts::Artifact>& inputs) {
    // Build a canonical, newline-delimited blob and hash it. Every field is
    // length-free but unambiguous because keys and values sit on their own
    // lines with a fixed tag.
    std::string blob;
    auto line = [&](std::string_view tag, std::string_view value) {
        blob.append(tag);
        blob.push_back('\t');
        blob.append(value);
        blob.push_back('\n');
    };

    line("v", kCacheKeyPrefix);
    line("type", domain::to_string(task.type));
    line("program", task.program);
    line("script", task.script);
    for (const auto& arg : task.arguments) {
        line("arg", arg);
    }

    std::vector<std::string> env;
    env.reserve(task.environment.size());
    for (const auto& [k, val] : task.environment) {
        env.push_back(k + "=" + val);
    }
    std::sort(env.begin(), env.end());
    for (const auto& e : env) {
        line("env", e);
    }

    line("cwd", task.working_directory);

    std::vector<std::string> input_ids;
    input_ids.reserve(inputs.size());
    for (const auto& art : inputs) {
        input_ids.push_back(art.identity());
    }
    std::sort(input_ids.begin(), input_ids.end());
    for (const auto& id : input_ids) {
        line("in", id);
    }

    return std::string(kCacheKeyPrefix) + ":" + util::sha256_hex(blob);
}

}  // namespace flowforge::cache
