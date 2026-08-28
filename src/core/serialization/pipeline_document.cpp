#include "flowforge/serialization/pipeline_document.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

#include "flowforge/domain/enums.hpp"

namespace flowforge::serialization {

using nlohmann::json;
using namespace flowforge::domain;

namespace {

constexpr int kSupportedSchemaVersion = 1;

struct Ctx {
    std::vector<LoadError>* errors;
    void add(std::string location, std::string message) {
        errors->push_back({std::move(location), std::move(message)});
    }
};

// --- typed field accessors -------------------------------------------------

std::optional<std::string> as_string(
    const json& parent, const char* key, const std::string& loc, Ctx& ctx, bool required) {
    if (!parent.contains(key) || parent.at(key).is_null()) {
        if (required) {
            ctx.add(loc + "." + key, "required string field is missing");
        }
        return std::nullopt;
    }
    const json& v = parent.at(key);
    if (!v.is_string()) {
        ctx.add(loc + "." + key, "expected a string");
        return std::nullopt;
    }
    return v.get<std::string>();
}

std::optional<std::int64_t> as_int(const json& parent,
                                   const char* key,
                                   const std::string& loc,
                                   Ctx& ctx) {
    if (!parent.contains(key) || parent.at(key).is_null()) {
        return std::nullopt;
    }
    const json& v = parent.at(key);
    if (!v.is_number_integer() && !v.is_number_unsigned()) {
        ctx.add(loc + "." + key, "expected an integer");
        return std::nullopt;
    }
    return v.get<std::int64_t>();
}

std::optional<double> as_double(const json& parent,
                                const char* key,
                                const std::string& loc,
                                Ctx& ctx) {
    if (!parent.contains(key) || parent.at(key).is_null()) {
        return std::nullopt;
    }
    const json& v = parent.at(key);
    if (!v.is_number()) {
        ctx.add(loc + "." + key, "expected a number");
        return std::nullopt;
    }
    return v.get<double>();
}

std::optional<bool> as_bool(const json& parent,
                            const char* key,
                            const std::string& loc,
                            Ctx& ctx) {
    if (!parent.contains(key) || parent.at(key).is_null()) {
        return std::nullopt;
    }
    const json& v = parent.at(key);
    if (!v.is_boolean()) {
        ctx.add(loc + "." + key, "expected a boolean");
        return std::nullopt;
    }
    return v.get<bool>();
}

std::vector<std::string> as_string_array(const json& parent,
                                         const char* key,
                                         const std::string& loc,
                                         Ctx& ctx) {
    std::vector<std::string> out;
    if (!parent.contains(key) || parent.at(key).is_null()) {
        return out;
    }
    const json& v = parent.at(key);
    if (!v.is_array()) {
        ctx.add(loc + "." + key, "expected an array of strings");
        return out;
    }
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (!v[i].is_string()) {
            ctx.add(loc + "." + key + "[" + std::to_string(i) + "]", "expected a string");
            continue;
        }
        out.push_back(v[i].get<std::string>());
    }
    return out;
}

RetryPolicy parse_retry(const json& parent,
                        const RetryPolicy& base,
                        const std::string& loc,
                        Ctx& ctx) {
    RetryPolicy p = base;
    if (!parent.contains("retry")) {
        return p;
    }
    const json& r = parent.at("retry");
    if (!r.is_object()) {
        ctx.add(loc + ".retry", "expected an object");
        return p;
    }
    const std::string rl = loc + ".retry";
    if (const auto v = as_int(r, "max_retries", rl, ctx)) {
        p.max_retries = static_cast<int>(*v);
    }
    if (const auto v = as_int(r, "base_delay_ms", rl, ctx)) {
        p.base_delay = std::chrono::milliseconds{*v};
    }
    if (const auto v = as_int(r, "max_delay_ms", rl, ctx)) {
        p.max_delay = std::chrono::milliseconds{*v};
    }
    if (const auto v = as_double(r, "backoff_multiplier", rl, ctx)) {
        p.backoff_multiplier = *v;
    }
    return p;
}

ResourceRequirements parse_resources(const json& parent,
                                     const ResourceRequirements& base,
                                     const std::string& loc,
                                     Ctx& ctx) {
    ResourceRequirements req = base;
    if (!parent.contains("resources")) {
        return req;
    }
    const json& r = parent.at("resources");
    if (!r.is_object()) {
        ctx.add(loc + ".resources", "expected an object");
        return req;
    }
    const std::string rl = loc + ".resources";
    if (const auto v = as_int(r, "cpu_cores", rl, ctx)) {
        req.cpu_cores = static_cast<int>(*v);
    }
    if (const auto v = as_int(r, "memory_mb", rl, ctx)) {
        req.memory_mb = *v;
    }
    if (const auto v = as_int(r, "gpu_count", rl, ctx)) {
        req.gpu_count = static_cast<int>(*v);
    }
    return req;
}

std::vector<ArtifactDecl> parse_artifacts(const json& parent,
                                          const char* key,
                                          const std::string& loc,
                                          Ctx& ctx) {
    std::vector<ArtifactDecl> out;
    if (!parent.contains(key) || parent.at(key).is_null()) {
        return out;
    }
    const json& arr = parent.at(key);
    if (!arr.is_array()) {
        ctx.add(loc + "." + key, "expected an array");
        return out;
    }
    for (std::size_t i = 0; i < arr.size(); ++i) {
        const std::string el = loc + "." + key + "[" + std::to_string(i) + "]";
        const json& item = arr[i];
        if (item.is_string()) {
            const std::string path = item.get<std::string>();
            const auto slash = path.find_last_of("/\\");
            out.push_back(
                {slash == std::string::npos ? path : path.substr(slash + 1), path, false});
            continue;
        }
        if (!item.is_object()) {
            ctx.add(el, "expected a string or an object with 'name' and 'path'");
            continue;
        }
        ArtifactDecl decl;
        if (const auto p = as_string(item, "path", el, ctx, true)) {
            decl.path = *p;
        }
        if (const auto n = as_string(item, "name", el, ctx, false)) {
            decl.name = *n;
        } else if (!decl.path.empty()) {
            const auto slash = decl.path.find_last_of("/\\");
            decl.name = slash == std::string::npos ? decl.path : decl.path.substr(slash + 1);
        }
        if (const auto c = as_bool(item, "checksum", el, ctx)) {
            decl.checksum = *c;
        }
        out.push_back(std::move(decl));
    }
    return out;
}

}  // namespace

std::string LoadError::to_string() const {
    return location.empty() ? message : (location + ": " + message);
}

LoadResult load_pipeline_from_json(std::string_view json_text, std::string base_directory) {
    LoadResult result;
    Ctx ctx{&result.errors};

    json doc = json::parse(json_text.begin(), json_text.end(), nullptr,
                           /*allow_exceptions=*/false);
    if (doc.is_discarded()) {
        ctx.add("", "document is not valid JSON");
        return result;
    }
    if (!doc.is_object()) {
        ctx.add("", "top-level value must be a JSON object");
        return result;
    }

    PipelineDefinition pipeline;
    pipeline.base_directory = std::move(base_directory);

    if (const auto v = as_int(doc, "schema_version", "", ctx)) {
        pipeline.schema_version = static_cast<int>(*v);
        if (*v != kSupportedSchemaVersion) {
            ctx.add("schema_version", "unsupported schema version " + std::to_string(*v) +
                                          " (this build understands version " +
                                          std::to_string(kSupportedSchemaVersion) + ")");
        }
    }

    if (const auto n = as_string(doc, "name", "", ctx, true)) {
        pipeline.name = *n;
        if (pipeline.name.empty()) {
            ctx.add("name", "pipeline name must not be empty");
        }
    }
    if (const auto d = as_string(doc, "description", "", ctx, false)) {
        pipeline.description = *d;
    }
    if (const auto c = as_int(doc, "max_concurrency", "", ctx)) {
        pipeline.max_concurrency = static_cast<int>(*c);
    }

    // ---- defaults ----
    std::string default_interpreter = "python3";
    RetryPolicy default_retry{};
    ResourceRequirements default_resources{};
    std::optional<std::chrono::milliseconds> default_timeout;
    bool default_cache = true;
    if (doc.contains("defaults")) {
        const json& def = doc.at("defaults");
        if (!def.is_object()) {
            ctx.add("defaults", "expected an object");
        } else {
            if (const auto s = as_string(def, "python_interpreter", "defaults", ctx, false)) {
                default_interpreter = *s;
            }
            default_retry = parse_retry(def, default_retry, "defaults", ctx);
            default_resources = parse_resources(def, default_resources, "defaults", ctx);
            if (const auto t = as_int(def, "timeout_ms", "defaults", ctx)) {
                default_timeout = std::chrono::milliseconds{*t};
            }
            if (const auto b = as_bool(def, "cache", "defaults", ctx)) {
                default_cache = *b;
            }
        }
    }

    // ---- tasks ----
    if (!doc.contains("tasks") || !doc.at("tasks").is_array()) {
        ctx.add("tasks", "required array field is missing or not an array");
    } else {
        const json& tasks = doc.at("tasks");
        if (tasks.empty()) {
            ctx.add("tasks", "a pipeline must define at least one task");
        }
        for (std::size_t i = 0; i < tasks.size(); ++i) {
            const std::string tl = "tasks[" + std::to_string(i) + "]";
            const json& tj = tasks[i];
            if (!tj.is_object()) {
                ctx.add(tl, "expected an object");
                continue;
            }
            TaskDefinition task;
            if (const auto id = as_string(tj, "id", tl, ctx, true)) {
                task.id = *id;
            }
            task.name = as_string(tj, "name", tl, ctx, false).value_or(task.id);

            const auto type_str = as_string(tj, "type", tl, ctx, true);
            if (type_str) {
                const auto parsed = parse_task_type(*type_str);
                if (!parsed) {
                    ctx.add(tl + ".type", "unknown task type '" + *type_str +
                                              "' (expected 'python' or 'executable')");
                } else {
                    task.type = *parsed;
                }
            }

            if (task.type == TaskType::kPython) {
                task.program =
                    as_string(tj, "interpreter", tl, ctx, false).value_or(default_interpreter);
                if (const auto sc = as_string(tj, "script", tl, ctx, true)) {
                    task.script = *sc;
                }
            } else {
                if (const auto ex = as_string(tj, "executable", tl, ctx, true)) {
                    task.program = *ex;
                }
            }

            task.arguments = as_string_array(tj, "arguments", tl, ctx);

            if (tj.contains("env")) {
                const json& env = tj.at("env");
                if (!env.is_object()) {
                    ctx.add(tl + ".env", "expected an object of string values");
                } else {
                    for (const auto& [k, val] : env.items()) {
                        if (!val.is_string()) {
                            ctx.add(tl + ".env." + k, "expected a string");
                            continue;
                        }
                        task.environment.emplace_back(k, val.get<std::string>());
                    }
                }
            }

            task.working_directory =
                as_string(tj, "working_directory", tl, ctx, false).value_or("");
            task.inputs = parse_artifacts(tj, "inputs", tl, ctx);
            task.outputs = parse_artifacts(tj, "outputs", tl, ctx);
            task.retry = parse_retry(tj, default_retry, tl, ctx);
            task.resources = parse_resources(tj, default_resources, tl, ctx);

            if (tj.contains("timeout_ms") && !tj.at("timeout_ms").is_null()) {
                if (const auto t = as_int(tj, "timeout_ms", tl, ctx)) {
                    task.timeout = std::chrono::milliseconds{*t};
                }
            } else {
                task.timeout = default_timeout;
            }

            task.priority = static_cast<int>(as_int(tj, "priority", tl, ctx).value_or(0));
            task.cache_enabled = as_bool(tj, "cache", tl, ctx).value_or(default_cache);

            for (const auto& dep : as_string_array(tj, "depends_on", tl, ctx)) {
                pipeline.edges.push_back({dep, task.id});
            }

            pipeline.tasks.push_back(std::move(task));
        }
    }

    // ---- explicit dependencies ----
    if (doc.contains("dependencies")) {
        const json& deps = doc.at("dependencies");
        if (!deps.is_array()) {
            ctx.add("dependencies", "expected an array");
        } else {
            for (std::size_t i = 0; i < deps.size(); ++i) {
                const std::string dl = "dependencies[" + std::to_string(i) + "]";
                const json& d = deps[i];
                if (!d.is_object()) {
                    ctx.add(dl, "expected an object with 'from' and 'to'");
                    continue;
                }
                const auto from = as_string(d, "from", dl, ctx, true);
                const auto to = as_string(d, "to", dl, ctx, true);
                if (from && to) {
                    pipeline.edges.push_back({*from, *to});
                }
            }
        }
    }

    if (!result.errors.empty()) {
        return result;  // leave pipeline unset: the document did not load cleanly
    }
    result.pipeline = std::move(pipeline);
    return result;
}

LoadResult load_pipeline_from_file(const std::filesystem::path& file) {
    LoadResult result;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        result.errors.push_back({"", "cannot open pipeline file: " + file.string()});
        return result;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::filesystem::path base = file.parent_path();
    if (base.empty()) {
        base = std::filesystem::current_path();
    }
    return load_pipeline_from_json(ss.str(), std::filesystem::absolute(base).string());
}

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------
namespace {

json retry_to_json(const RetryPolicy& p) {
    return json{{"max_retries", p.max_retries},
                {"base_delay_ms", p.base_delay.count()},
                {"backoff_multiplier", p.backoff_multiplier},
                {"max_delay_ms", p.max_delay.count()}};
}

json resources_to_json(const ResourceRequirements& r) {
    return json{
        {"cpu_cores", r.cpu_cores}, {"memory_mb", r.memory_mb}, {"gpu_count", r.gpu_count}};
}

json artifacts_to_json(const std::vector<ArtifactDecl>& decls) {
    json arr = json::array();
    for (const auto& d : decls) {
        arr.push_back(json{{"name", d.name}, {"path", d.path}, {"checksum", d.checksum}});
    }
    return arr;
}

}  // namespace

std::string dump_pipeline(const domain::PipelineDefinition& pipeline, int indent) {
    json doc;
    doc["schema_version"] = pipeline.schema_version;
    doc["name"] = pipeline.name;
    if (!pipeline.description.empty()) {
        doc["description"] = pipeline.description;
    }
    if (pipeline.max_concurrency > 0) {
        doc["max_concurrency"] = pipeline.max_concurrency;
    }

    json tasks = json::array();
    for (const auto& t : pipeline.tasks) {
        json tj;
        tj["id"] = t.id;
        tj["name"] = t.name;
        tj["type"] = std::string(to_string(t.type));
        if (t.type == TaskType::kPython) {
            tj["interpreter"] = t.program;
            tj["script"] = t.script;
        } else {
            tj["executable"] = t.program;
        }
        tj["arguments"] = t.arguments;
        if (!t.environment.empty()) {
            json env = json::object();
            for (const auto& [k, v] : t.environment) {
                env[k] = v;
            }
            tj["env"] = env;
        }
        if (!t.working_directory.empty()) {
            tj["working_directory"] = t.working_directory;
        }
        if (!t.inputs.empty()) {
            tj["inputs"] = artifacts_to_json(t.inputs);
        }
        if (!t.outputs.empty()) {
            tj["outputs"] = artifacts_to_json(t.outputs);
        }
        tj["retry"] = retry_to_json(t.retry);
        tj["resources"] = resources_to_json(t.resources);
        if (t.timeout) {
            tj["timeout_ms"] = t.timeout->count();
        }
        tj["priority"] = t.priority;
        tj["cache"] = t.cache_enabled;
        tasks.push_back(std::move(tj));
    }
    doc["tasks"] = std::move(tasks);

    json deps = json::array();
    for (const auto& e : pipeline.edges) {
        deps.push_back(json{{"from", e.from}, {"to", e.to}});
    }
    doc["dependencies"] = std::move(deps);

    return doc.dump(indent);
}

}  // namespace flowforge::serialization
