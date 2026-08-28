#include "flowforge/validation/pipeline_validator.hpp"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

#include "flowforge/artifacts/artifact_manager.hpp"
#include "flowforge/dag/dag.hpp"
#include "flowforge/domain/resource_requirements.hpp"
#include "flowforge/resources/resource_pool.hpp"

namespace fs = std::filesystem;

namespace flowforge::validation {

using namespace flowforge::domain;

namespace {

bool is_executable_file(const fs::path& p) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) {
        return false;
    }
    return ::access(p.c_str(), X_OK) == 0;
}

// Resolve a program name the way execvp would: absolute/relative paths are
// taken literally, a bare name is searched on PATH. Returns the resolved path
// if found and executable.
std::optional<fs::path> resolve_program(const std::string& program, const fs::path& work_dir) {
    if (program.empty()) {
        return std::nullopt;
    }
    if (program.find('/') != std::string::npos) {
        fs::path p(program);
        if (p.is_relative()) {
            p = (work_dir / p).lexically_normal();
        }
        return is_executable_file(p) ? std::optional<fs::path>(p) : std::nullopt;
    }
    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return std::nullopt;
    }
    const std::string paths(path_env);
    std::size_t start = 0;
    while (start <= paths.size()) {
        const std::size_t colon = paths.find(':', start);
        const std::string dir =
            paths.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
        if (!dir.empty()) {
            const fs::path candidate = fs::path(dir) / program;
            if (is_executable_file(candidate)) {
                return candidate;
            }
        }
        if (colon == std::string::npos) {
            break;
        }
        start = colon + 1;
    }
    return std::nullopt;
}

struct Sink {
    ValidationReport* report;
    void error(std::string task, std::string field, std::string msg) {
        report->issues.push_back(
            {Severity::kError, std::move(task), std::move(field), std::move(msg)});
    }
    void warn(std::string task, std::string field, std::string msg) {
        report->issues.push_back(
            {Severity::kWarning, std::move(task), std::move(field), std::move(msg)});
    }
};

}  // namespace

std::string ValidationIssue::to_string() const {
    std::string prefix = severity == Severity::kError ? "error" : "warning";
    std::string subject = task_id.empty() ? "pipeline" : ("task '" + task_id + "'");
    std::string loc = field.empty() ? "" : (" [" + field + "]");
    return prefix + ": " + subject + loc + ": " + message;
}

bool ValidationReport::ok() const noexcept {
    for (const auto& i : issues) {
        if (i.severity == Severity::kError) {
            return false;
        }
    }
    return true;
}

bool ValidationReport::has_warnings() const noexcept {
    for (const auto& i : issues) {
        if (i.severity == Severity::kWarning) {
            return true;
        }
    }
    return false;
}

std::vector<ValidationIssue> ValidationReport::errors() const {
    std::vector<ValidationIssue> out;
    for (const auto& i : issues) {
        if (i.severity == Severity::kError) {
            out.push_back(i);
        }
    }
    return out;
}

std::vector<ValidationIssue> ValidationReport::warnings() const {
    std::vector<ValidationIssue> out;
    for (const auto& i : issues) {
        if (i.severity == Severity::kWarning) {
            out.push_back(i);
        }
    }
    return out;
}

ValidationReport validate_pipeline(const PipelineDefinition& pipeline,
                                   const ValidationOptions& options) {
    ValidationReport report;
    Sink sink{&report};

    if (pipeline.name.empty()) {
        sink.error("", "name", "pipeline has no name");
    }

    // --- unique task ids ---
    std::unordered_map<std::string, int> id_counts;
    for (const auto& t : pipeline.tasks) {
        if (t.id.empty()) {
            sink.error("", "id", "a task has an empty id");
            continue;
        }
        ++id_counts[t.id];
    }
    for (const auto& [id, count] : id_counts) {
        if (count > 1) {
            sink.error(id, "id", "task id is used " + std::to_string(count) + " times");
        }
    }

    // --- graph structure via the DAG builder ---
    {
        std::vector<std::string> nodes;
        nodes.reserve(pipeline.tasks.size());
        for (const auto& t : pipeline.tasks) {
            if (!t.id.empty()) {
                nodes.push_back(t.id);
            }
        }
        std::vector<std::pair<std::string, std::string>> edges;
        edges.reserve(pipeline.edges.size());
        for (const auto& e : pipeline.edges) {
            edges.emplace_back(e.from, e.to);
        }
        auto built = dag::Dag::build(std::move(nodes), std::move(edges));
        if (!built.ok()) {
            for (const auto& de : built.errors) {
                sink.error("", "dependencies", de.message);
            }
        }
    }

    artifacts::ArtifactManager artifacts(pipeline.base_directory.empty()
                                             ? fs::current_path()
                                             : fs::path(pipeline.base_directory));

    std::unordered_set<std::string> non_root;
    for (const auto& e : pipeline.edges) {
        non_root.insert(e.to);
    }

    for (const auto& task : pipeline.tasks) {
        if (task.id.empty()) {
            continue;
        }
        const fs::path work_dir = artifacts.resolve_working_dir(task);

        // working directory
        if (!task.working_directory.empty()) {
            std::error_code ec;
            if (!fs::is_directory(work_dir, ec)) {
                sink.error(task.id, "working_directory",
                           "working directory does not exist: " + work_dir.string());
            }
        }

        // program / interpreter
        if (task.type == TaskType::kPython) {
            if (task.script.empty()) {
                sink.error(task.id, "script", "python task has no script");
            }
            if (task.program.empty()) {
                sink.error(task.id, "interpreter", "python task has no interpreter");
            } else if (options.check_executables &&
                       !resolve_program(task.program, work_dir)) {
                sink.error(task.id, "interpreter",
                           "Python interpreter not found: " + task.program);
            }
            if (!task.script.empty()) {
                const fs::path script_path =
                    fs::path(task.script).is_absolute()
                        ? fs::path(task.script)
                        : (work_dir / task.script).lexically_normal();
                std::error_code ec;
                if (options.check_executables && !fs::is_regular_file(script_path, ec)) {
                    sink.error(task.id, "script",
                               "script file not found: " + script_path.string());
                }
            }
        } else {
            if (task.program.empty()) {
                sink.error(task.id, "executable", "executable task has no executable");
            } else if (options.check_executables &&
                       !resolve_program(task.program, work_dir)) {
                sink.error(task.id, "executable",
                           "executable not found or not runnable: " + task.program);
            }
        }

        // retry / resources / timeout
        if (task.retry.max_retries < 0) {
            sink.error(task.id, "retry.max_retries", "max_retries must not be negative");
        }
        if (task.retry.backoff_multiplier < 0.0) {
            sink.error(task.id, "retry.backoff_multiplier",
                       "backoff_multiplier must not be negative");
        }
        if (task.resources.cpu_cores < 1) {
            sink.error(task.id, "resources.cpu_cores", "a task must request at least 1 CPU core");
        }
        if (task.resources.memory_mb < 0 || task.resources.gpu_count < 0) {
            sink.error(task.id, "resources", "resource requirements must not be negative");
        }
        if (task.timeout && task.timeout->count() <= 0) {
            sink.error(task.id, "timeout_ms", "timeout must be positive when set");
        }
        if (options.resource_pool != nullptr &&
            !options.resource_pool->can_ever_fit(task.resources)) {
            sink.error(task.id, "resources",
                       "task requires " + describe(task.resources) +
                           " which exceeds machine capacity (" +
                           options.resource_pool->describe() + ")");
        }

        // artifact declarations
        auto check_decls = [&](const std::vector<ArtifactDecl>& decls, const char* kind) {
            std::unordered_set<std::string> seen;
            for (const auto& d : decls) {
                if (d.path.empty()) {
                    sink.error(task.id, kind, "an artifact declaration has an empty path");
                }
                if (!d.name.empty() && !seen.insert(d.name).second) {
                    sink.warn(task.id, kind,
                              "duplicate artifact logical name '" + d.name + "'");
                }
            }
        };
        check_decls(task.inputs, "inputs");
        check_decls(task.outputs, "outputs");

        if (options.check_root_inputs_exist && non_root.find(task.id) == non_root.end()) {
            for (const auto& missing : artifacts.missing_inputs(task)) {
                sink.error(task.id, "inputs",
                           "declared input '" + missing + "' does not exist");
            }
        }
    }

    return report;
}

}  // namespace flowforge::validation
