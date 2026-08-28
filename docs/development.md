# Development

## Prerequisites

- C++20 compiler (developed on Apple Clang 17; GCC 12+ / Clang 15+ expected)
- CMake ≥ 3.24, Ninja
- vcpkg (any recent checkout). Point `VCPKG_ROOT` at it.
- Python 3 only to run the Python example / Python tasks.

Dependencies (`vcpkg.json`, manifest mode, pinned baseline): `nlohmann-json`,
`spdlog`, `sqlite3`, `gtest`.

## Build / test

```sh
export VCPKG_ROOT=/path/to/vcpkg

cmake --preset debug            # Ninja + vcpkg toolchain, warnings-as-errors
cmake --build --preset debug
ctest --preset debug

cmake --preset release          # -O3
cmake --preset asan             # Debug + ASan + UBSan
```

Presets write to `build/<preset>/`. `ctest` labels: `unit`, `integration`,
`performance`.

## Layout

```
include/flowforge/<area>/*.hpp     public headers of flowforge_core
src/core/<area>/*.cpp              implementation
src/cli/                           the flowforge binary (presentation only)
tests/{unit,integration,performance,helpers,support}/
examples/<name>/                   runnable example pipelines (+ helpers/)
docs/                              this documentation, docs/decisions/ = ADRs
scripts/                           run-examples.sh, format.sh, tidy.sh
```

## Configuration resolution (engine)

`engine::Config::load()` merges, lowest precedence first:

1. `Config::defaults()` — concurrency from `hardware_concurrency()` (capped at
   8), CPU capacity = core count, a nominal large memory ceiling.
2. a JSON file: `<state_dir>/config.json`, else `./flowforge.json`
   (or `--config <file>`).
3. environment: `FLOWFORGE_STATE_DIR`, `FLOWFORGE_MAX_CONCURRENCY`,
   `FLOWFORGE_PYTHON`, `FLOWFORGE_DB`, `FLOWFORGE_CACHE`, `FLOWFORGE_CHECKSUMS`,
   `FLOWFORGE_LOG_LEVEL`, `FLOWFORGE_CPU_CORES`.
4. CLI flags (`--state-dir`, `--db`, `--max-concurrency`, `--no-cache`,
   `--checksums`).

A malformed config file is ignored (defaults stand); it never aborts a command.

## Formatting & static analysis

`.clang-format` (Google base, 4-space, 95 col) and `.clang-tidy` (bugprone /
misc / performance / a light modernize+readability pass).

```sh
scripts/format.sh          # clang-format -i over src/ include/ tests/ examples/
scripts/format.sh --check  # non-zero if anything is unformatted (used in CI)
scripts/tidy.sh            # clang-tidy over the core library using compile_commands.json
```

`compile_commands.json` is emitted into each build dir; symlink it to the repo
root if your editor wants it there.

The Debug preset compiles the core library and CLI with
`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
-Wold-style-cast …` and **treats warnings as errors**
(`FLOWFORGE_WARNINGS_AS_ERRORS=ON`). Release does not, so a new compiler's
extra warnings don't block a release build.

## Development loop

Implement → `cmake --build --preset debug` → `ctest --preset debug` →
(fix / add a regression test) → format → commit. Commits are atomic and use
conventional-commit prefixes (`feat:`, `fix:`, `test:`, `docs:`, `build:`,
`chore:`, `perf:`). Inspect `git diff` / `git status` before each commit; a
commit should leave the tree building and green.

## Adding things

- **A new task type:** add the enum in `domain/enums`, parse it in
  `serialization/pipeline_document.cpp`, extend
  `TaskDefinition::resolve_command()`. The scheduler and process layer need no
  changes.
- **A new CLI command:** add a `cmd_*` function in `src/cli/cli_app.cpp` and a
  dispatch line; document it in `kUsage` and the README.
- **A schema change:** additive → keep `schema_version: 1`; breaking → bump it
  and update the loader + `docs/pipeline-format.md`.
- **A DB schema change:** append a migration in `storage/schema.cpp`, bump
  `kCurrentSchemaVersion`, document it in `docs/persistence.md`. Never edit a
  shipped migration.
