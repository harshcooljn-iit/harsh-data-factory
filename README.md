# FlowForge

**A small, local-first DAG pipeline orchestration engine — CLI first, no GUI, no
cloud, no distributed workers.**

FlowForge lets you describe a Directed Acyclic Graph of tasks in JSON, where each
task runs an external program (a Python script, a native executable, any binary).
The engine works out which tasks are ready, runs independent tasks concurrently
within configurable concurrency and resource limits, handles retries, timeouts,
cancellation, failure propagation and local result caching, persists every run to
SQLite, and streams structured logs.

> Status: **0.1.0**, in active development. This README grows as subsystems land.
> See [`docs/`](docs/) for design documents and decision records.

---

## Why it is built this way

| Decision | Rationale |
| --- | --- |
| CLI-first, engine as a library | The scheduler, DAG engine and executor live in `flowforge_core` and never depend on the CLI. The CLI, the unit tests and the integration tests all drive the *same* engine. A GUI or REST API could be added without touching the scheduler. |
| Tasks are external processes | Isolation, language independence, and a hard boundary between orchestration and workload. FlowForge manages metadata and lifecycle, not your data. |
| Data flows through files (artifacts) | Large datasets never pass through the orchestrator's memory. Tasks read and write files; FlowForge tracks path / size / mtime / optional checksum. |
| Dependency counters, event-driven scheduling | No busy-waiting, no repeated full-DAG scans, no thread-per-task. Dependency bookkeeping is O(V + E) per run. |
| SQLite for persistence | Zero-configuration, single-file, transactional. Enough for local run history, logs and the cache. |

Full rationale lives in [`docs/decisions/`](docs/decisions/) (architecture decision records).

---

## Requirements

- A C++20 compiler (tested with Apple Clang 17; GCC 12+/Clang 15+ expected to work)
- CMake ≥ 3.24 and Ninja
- [vcpkg](https://github.com/microsoft/vcpkg) (manifest mode; dependencies are
  declared in [`vcpkg.json`](vcpkg.json))
- Python 3 is only needed to *run* Python tasks/examples, not to build FlowForge

Dependencies pulled in via vcpkg: `nlohmann-json`, `spdlog`, `sqlite3`, `gtest`.

---

## Build, test, run

```sh
export VCPKG_ROOT=/path/to/vcpkg          # required by the presets

cmake --preset debug                       # configure (Ninja + vcpkg toolchain)
cmake --build --preset debug               # build core lib, CLI and tests
ctest --preset debug                       # run the test suite

./build/debug/bin/flowforge version
```

Release build: swap `debug` for `release`. An `asan` preset adds
Address/UB sanitizers.

---

## Platform support

Developed and tested on **macOS (arm64)**. The process backend is POSIX
(`posix_spawn` + a single-threaded `poll(2)` reactor) and is expected to work on
**Linux** unchanged; CI coverage is being added. **Windows is not supported** in
0.1.0 — the process layer is isolated behind an interface so a Win32 backend can
be added later without touching the scheduler.

---

## Repository layout

```
include/flowforge/   public headers for the engine library
src/core/            engine implementation (domain, dag, scheduler, process, ...)
src/cli/             the `flowforge` command line front end
tests/               unit / integration / performance tests + helper programs
examples/            runnable example pipelines
docs/                architecture docs and decision records
scripts/             developer helper scripts
```

---

## License

MIT — see [`LICENSE`](LICENSE).
