#include "cli_app.hpp"

#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "args.hpp"
#include "console_reporter.hpp"
#include "flowforge/dag/dag.hpp"
#include "flowforge/engine/ascii_graph.hpp"
#include "flowforge/engine/config.hpp"
#include "flowforge/engine/engine.hpp"
#include "flowforge/serialization/pipeline_document.hpp"
#include "flowforge/util/string_utils.hpp"
#include "flowforge/util/time_utils.hpp"
#include "flowforge/validation/pipeline_validator.hpp"
#include "flowforge/version.hpp"

namespace fs = std::filesystem;

namespace flowforge::cli {

namespace {

std::atomic<bool>* g_cancel_flag = nullptr;

extern "C" void handle_signal(int) {
    if (g_cancel_flag != nullptr) {
        g_cancel_flag->store(true, std::memory_order_release);
    }
}

constexpr const char* kUsage = R"(flowforge - local-first DAG pipeline orchestration engine

usage: flowforge <command> [options]

commands:
  init                        scaffold .flowforge/ and a sample pipeline
  validate <pipeline.json>    check a pipeline without running it
  graph <pipeline.json>       print the DAG (levels + dependency tree)
  run <pipeline.json>         execute a pipeline
  runs                        list recent runs
  status <run-id>             show one run in detail
  logs <run-id>               show captured logs for a run
  cancel <run-id>             request cancellation of a running run
  clean-cache                 drop cached task results
  version                     print the version

global options:
  --state-dir <dir>           override the FlowForge state directory
  --db <file>                 override the SQLite database path
  --config <file>             load settings from a specific JSON file

run options:
  --max-concurrency <n>       cap simultaneously running tasks
  --no-cache                  ignore and do not write the result cache
  --checksums                 hash all artifacts (slower, stronger cache keys)
  --plain                     CI-friendly line output
  --quiet                     suppress per-task log lines

validate options:
  --strict                    also require root inputs to exist; warnings fail

logs options:
  --task <id>                 only this task
  --stream <stdout|stderr|engine>
  --limit <n>                 maximum lines (default 200)

runs / clean-cache options:
  --limit <n>                 runs to list (default 20)
  --older-than-days <n>       only prune cache entries older than n days
)";

engine::Config resolve_config(const Args& args) {
    engine::Config::LoadOptions opts;
    opts.working_dir = ".";
    if (const auto c = args.value("config")) {
        opts.explicit_config_file = *c;
    }
    engine::Config cfg = engine::Config::load(opts);
    if (const auto s = args.value("state-dir")) {
        cfg.state_dir = *s;
    }
    if (const auto d = args.value("db")) {
        cfg.database_path = *d;
    }
    return cfg;
}

std::string ts(std::int64_t unix_ms) {
    if (unix_ms <= 0) {
        return "-";
    }
    return util::to_iso8601(util::from_unix_millis(unix_ms));
}

std::string duration_str(std::optional<std::int64_t> start, std::optional<std::int64_t> end) {
    if (!start) {
        return "-";
    }
    const std::int64_t e = end.value_or(util::to_unix_millis(util::now()));
    const double secs = static_cast<double>(e - *start) / 1000.0;
    return util::format_duration(secs < 0 ? 0.0 : secs);
}

int fail_usage(const std::string& message) {
    std::fprintf(stderr, "flowforge: %s\n", message.c_str());
    std::fprintf(stderr, "try 'flowforge --help'\n");
    return 2;
}

std::optional<domain::PipelineDefinition> load_pipeline_or_report(const std::string& path) {
    auto result = serialization::load_pipeline_from_file(path);
    if (!result.ok()) {
        std::fprintf(stderr, "failed to load pipeline '%s':\n", path.c_str());
        for (const auto& e : result.errors) {
            std::fprintf(stderr, "  %s\n", e.to_string().c_str());
        }
        return std::nullopt;
    }
    return std::move(*result.pipeline);
}

// --- commands ----------------------------------------------------------

int cmd_version() {
    std::printf("%s %s\n", version::kName, version::kString);
    return 0;
}

int cmd_init(const Args& args) {
    const engine::Config cfg = resolve_config(args);
    std::error_code ec;
    fs::create_directories(cfg.state_dir, ec);
    fs::create_directories(cfg.cancel_dir(), ec);

    const fs::path config_file = fs::path(cfg.state_dir) / "config.json";
    if (!fs::exists(config_file)) {
        std::ofstream(config_file) << R"({
  "max_concurrency": 4,
  "python_interpreter": "python3",
  "cache_enabled": true,
  "compute_checksums": false
}
)";
        std::printf("created %s\n", config_file.string().c_str());
    }

    const fs::path sample = "pipeline.json";
    if (!fs::exists(sample)) {
        std::ofstream(sample) << R"({
  "schema_version": 1,
  "name": "sample",
  "tasks": [
    { "id": "hello", "type": "executable", "executable": "/bin/echo",
      "arguments": ["hello", "from", "flowforge"] }
  ],
  "dependencies": []
}
)";
        std::printf("created %s\n", sample.string().c_str());
    }
    std::printf("state directory: %s\n", cfg.state_dir.c_str());
    return 0;
}

std::optional<long> run_id_arg(const Args& args, const char* command) {
    const auto p = args.positional(0);
    if (!p) {
        return std::nullopt;
    }
    char* end = nullptr;
    const long v = std::strtol(p->c_str(), &end, 10);
    if (end == p->c_str() || *end != '\0' || v < 0) {
        std::fprintf(stderr, "flowforge: %s: '%s' is not a valid run id\n", command,
                     p->c_str());
        return std::nullopt;
    }
    return v;
}

int cmd_validate(const Args& args) {
    const auto path = args.positional(0);
    if (!path) {
        return fail_usage("validate: expected a pipeline file");
    }
    auto pipeline = load_pipeline_or_report(*path);
    if (!pipeline) {
        return 1;
    }

    const engine::Config cfg = resolve_config(args);
    resources::ResourcePool pool(cfg.resource_capacity);
    validation::ValidationOptions vopts;
    vopts.check_executables = true;
    vopts.resource_pool = &pool;
    vopts.check_root_inputs_exist = args.flag("strict");

    const auto report = validation::validate_pipeline(*pipeline, vopts);
    for (const auto& issue : report.issues) {
        std::fprintf(issue.severity == validation::Severity::kError ? stderr : stdout, "%s\n",
                     issue.to_string().c_str());
    }
    const bool strict_fail = args.flag("strict") && report.has_warnings();
    if (report.ok() && !strict_fail) {
        std::printf("ok: '%s' is valid (%zu tasks, %zu dependencies)\n", pipeline->name.c_str(),
                    pipeline->tasks.size(), pipeline->edges.size());
        return 0;
    }
    std::fprintf(stderr, "pipeline is invalid\n");
    return 1;
}

int cmd_graph(const Args& args) {
    const auto path = args.positional(0);
    if (!path) {
        return fail_usage("graph: expected a pipeline file");
    }
    auto pipeline = load_pipeline_or_report(*path);
    if (!pipeline) {
        return 1;
    }

    std::vector<std::string> nodes;
    for (const auto& t : pipeline->tasks) {
        nodes.push_back(t.id);
    }
    std::vector<std::pair<std::string, std::string>> edges;
    for (const auto& e : pipeline->edges) {
        edges.emplace_back(e.from, e.to);
    }
    auto built = dag::Dag::build(std::move(nodes), std::move(edges));
    if (!built.ok()) {
        std::fprintf(stderr, "cannot render graph:\n");
        for (const auto& e : built.errors) {
            std::fprintf(stderr, "  %s\n", e.message.c_str());
        }
        return 1;
    }
    std::printf("%s", engine::render_ascii_graph(*pipeline, *built.dag).c_str());
    return 0;
}

int cmd_run(const Args& args) {
    const auto path = args.positional(0);
    if (!path) {
        return fail_usage("run: expected a pipeline file");
    }
    auto pipeline = load_pipeline_or_report(*path);
    if (!pipeline) {
        return 1;
    }

    engine::Config cfg = resolve_config(args);
    engine::Engine eng(cfg);
    const int recovered = eng.recover_orphaned_runs();
    if (recovered > 0) {
        std::fprintf(stderr, "note: marked %d interrupted run(s) from a previous session\n",
                     recovered);
    }

    ConsoleReporter::Options ropts;
    ropts.plain = args.flag("plain") || (isatty(fileno(stdout)) == 0);
    ropts.quiet = args.flag("quiet");
    ConsoleReporter reporter(ropts);

    std::atomic<bool> cancel_flag{false};
    g_cancel_flag = &cancel_flag;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    engine::Engine::RunRequest req;
    req.observer = &reporter;
    req.external_cancel = &cancel_flag;
    if (const auto n = args.int_value("max-concurrency")) {
        req.max_concurrency = static_cast<int>(*n);
    }
    if (args.flag("no-cache")) {
        req.cache_enabled = false;
    }
    if (args.flag("checksums")) {
        req.compute_checksums = true;
    }

    const auto report = eng.run_pipeline(*pipeline, req);
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
    g_cancel_flag = nullptr;

    if (report.validation_failed) {
        std::fprintf(stderr, "pipeline failed validation:\n");
        for (const auto& issue : report.validation.errors()) {
            std::fprintf(stderr, "  %s\n", issue.to_string().c_str());
        }
        return 1;
    }

    std::printf("run id: %lld\n", static_cast<long long>(report.run_id));
    return report.state == domain::PipelineState::kSucceeded ? 0 : 1;
}

int cmd_runs(const Args& args) {
    const engine::Config cfg = resolve_config(args);
    engine::Engine eng(cfg);
    eng.recover_orphaned_runs();
    const int limit = static_cast<int>(args.int_value("limit").value_or(20));

    const auto runs = eng.list_runs(limit);
    if (runs.empty()) {
        std::printf("no runs yet\n");
        return 0;
    }
    std::printf("%-6s  %-20s  %-11s  %-24s  %s\n", "ID", "PIPELINE", "STATE", "STARTED",
               "DURATION");
    for (const auto& r : runs) {
        std::printf("%-6lld  %-20.20s  %-11s  %-24s  %s\n", static_cast<long long>(r.id),
                    r.pipeline_name.c_str(),
                    std::string(domain::to_string(r.state)).c_str(),
                    ts(r.started_at.value_or(r.created_at)).c_str(),
                    duration_str(r.started_at, r.finished_at).c_str());
    }
    return 0;
}

int cmd_status(const Args& args) {
    const auto id = run_id_arg(args, "status");
    if (!id) {
        return fail_usage("status: expected a run id");
    }
    const engine::Config cfg = resolve_config(args);
    engine::Engine eng(cfg);
    const auto view = eng.get_run(*id);
    if (!view) {
        std::fprintf(stderr, "no such run: %ld\n", *id);
        return 1;
    }

    std::printf("run %lld  pipeline '%s'\n", static_cast<long long>(view->id),
                view->pipeline_name.c_str());
    std::printf("state      : %s\n", std::string(domain::to_string(view->state)).c_str());
    std::printf("created    : %s\n", ts(view->created_at).c_str());
    std::printf("started    : %s\n", ts(view->started_at.value_or(0)).c_str());
    std::printf("finished   : %s\n", ts(view->finished_at.value_or(0)).c_str());
    std::printf("duration   : %s\n", duration_str(view->started_at, view->finished_at).c_str());
    std::printf("concurrency: %d\n", view->max_concurrency);
    const auto& c = view->counts;
    std::printf("tasks      : %d total | %d ok, %d cached, %d failed, %d skipped, %d cancelled\n",
                c.total, c.succeeded, c.cached, c.failed, c.skipped, c.cancelled);

    std::printf("\n%-24s  %-10s  %-8s  %-8s  %s\n", "TASK", "STATE", "ATTEMPTS", "EXIT",
                "DURATION");
    for (const auto& t : view->tasks) {
        std::printf("%-24.24s  %-10s  %-8d  %-8s  %s\n", t.task_id.c_str(),
                    std::string(domain::to_string(t.state)).c_str(), t.attempts,
                    t.last_exit_code ? std::to_string(*t.last_exit_code).c_str() : "-",
                    duration_str(t.started_at, t.finished_at).c_str());
    }
    return view->state == domain::PipelineState::kSucceeded ? 0 : 1;
}

int cmd_logs(const Args& args) {
    const auto id = run_id_arg(args, "logs");
    if (!id) {
        return fail_usage("logs: expected a run id");
    }
    const engine::Config cfg = resolve_config(args);
    engine::Engine eng(cfg);
    const int limit = static_cast<int>(args.int_value("limit").value_or(200));
    std::optional<std::string> task;
    if (const auto t = args.value("task")) {
        task = *t;
    }
    const auto stream_filter = args.value("stream");

    const auto logs = eng.get_logs(*id, task, limit);
    if (logs.empty()) {
        std::printf("no logs for run %ld\n", *id);
        return 0;
    }
    for (const auto& rec : logs) {
        if (stream_filter && rec.stream != *stream_filter) {
            continue;
        }
        std::printf("%s  [%-5s] %s%s: %s\n", ts(rec.ts).c_str(), rec.severity.c_str(),
                    rec.task_id ? rec.task_id->c_str() : "-",
                    rec.stream.empty() ? "" : ("/" + rec.stream).c_str(), rec.message.c_str());
    }
    return 0;
}

int cmd_cancel(const Args& args) {
    const auto id = run_id_arg(args, "cancel");
    if (!id) {
        return fail_usage("cancel: expected a run id");
    }
    const engine::Config cfg = resolve_config(args);
    engine::Engine eng(cfg);
    if (eng.request_cancel(*id)) {
        std::printf("cancellation requested for run %ld\n", *id);
        return 0;
    }
    std::fprintf(stderr, "run %ld is unknown or already finished\n", *id);
    return 1;
}

int cmd_clean_cache(const Args& args) {
    const engine::Config cfg = resolve_config(args);
    engine::Engine eng(cfg);
    int removed = 0;
    if (const auto days = args.int_value("older-than-days")) {
        removed = eng.prune_cache(static_cast<int>(*days));
        std::printf("pruned %d cache entr%s older than %ld day(s)\n", removed,
                    removed == 1 ? "y" : "ies", *days);
    } else {
        removed = eng.clear_cache();
        std::printf("removed %d cache entr%s\n", removed, removed == 1 ? "y" : "ies");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("%s", kUsage);
        return 0;
    }
    const std::string command = argv[1];
    if (command == "-h" || command == "--help" || command == "help") {
        std::printf("%s", kUsage);
        return 0;
    }
    if (command == "version" || command == "--version" || command == "-V") {
        return cmd_version();
    }

    const Args args(argc, argv, 2);

    if (command == "init") {
        return cmd_init(args);
    }
    if (command == "validate") {
        return cmd_validate(args);
    }
    if (command == "graph") {
        return cmd_graph(args);
    }
    if (command == "run") {
        return cmd_run(args);
    }
    if (command == "runs") {
        return cmd_runs(args);
    }
    if (command == "clean-cache") {
        return cmd_clean_cache(args);
    }
    if (command == "status") {
        return cmd_status(args);
    }
    if (command == "logs") {
        return cmd_logs(args);
    }
    if (command == "cancel") {
        return cmd_cancel(args);
    }

    return fail_usage("unknown command '" + command + "'");
}

}  // namespace flowforge::cli
