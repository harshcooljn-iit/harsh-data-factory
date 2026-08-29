# FlowForge user guide

A task-oriented walkthrough: install FlowForge on macOS, write a pipeline in
JSON, run it, and inspect the results. For the exhaustive field reference see
[`pipeline-format.md`](pipeline-format.md); for the exact run semantics see
[`execution-model.md`](execution-model.md).

- [1. Install on macOS](#1-install-on-macos)
- [2. First run in 60 seconds](#2-first-run-in-60-seconds)
- [3. The pipeline JSON file](#3-the-pipeline-json-file)
- [4. Worked example: a data project](#4-worked-example-a-data-project)
- [5. Running a pipeline](#5-running-a-pipeline)
- [6. Inspecting runs](#6-inspecting-runs)
- [7. Recipes](#7-recipes)
- [8. Configuration](#8-configuration)
- [9. Command reference](#9-command-reference)
- [10. Troubleshooting](#10-troubleshooting)

---

## 1. Install on macOS

Works on Apple Silicon and Intel Macs.

### 1.1 Prerequisites

```sh
# Apple Clang + the macOS SDK (skip if you already have Xcode)
xcode-select --install

# Build tools via Homebrew (https://brew.sh)
brew install cmake ninja git
# python3 ships with the Command Line Tools; only needed to RUN Python tasks.
```

Check versions (CMake ≥ 3.24 required):

```sh
clang++ --version      # Apple clang 15+ is fine
cmake --version
ninja --version
```

### 1.2 Get vcpkg (dependency manager)

FlowForge builds its dependencies (`nlohmann-json`, `spdlog`, `sqlite3`,
`gtest`) reproducibly through vcpkg.

```sh
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

Tell FlowForge where it is (add this line to `~/.zshrc` so it persists):

```sh
export VCPKG_ROOT="$HOME/vcpkg"
```

### 1.3 Build FlowForge

```sh
git clone <this-repo> flowforge && cd flowforge

cmake --preset release          # first run also builds the vcpkg deps (~1 min)
cmake --build --preset release
```

The binary is now at `build/release/bin/flowforge`. Try it:

```sh
./build/release/bin/flowforge version
# FlowForge 0.1.0
```

### 1.4 Install it on your PATH (optional)

```sh
sudo cmake --install build/release --prefix /usr/local
flowforge version
```

Or install into your home directory without `sudo`:

```sh
cmake --install build/release --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"     # add to ~/.zshrc
```

The rest of this guide assumes `flowforge` is on your `PATH`. If it isn't, use
`./build/release/bin/flowforge` instead.

### 1.5 Run the test suite (optional, to confirm the build)

```sh
ctest --preset release            # ~20 s, 140+ tests
```

### 1.6 Where FlowForge is installed, and how to remove it

**FlowForge is not a system service and it does not touch anything outside the
places you point it at.** There are three kinds of files:

| What | Where | Global? |
| --- | --- | --- |
| The `flowforge` **binary** | wherever you built or installed it. If you ran `cmake --install ... --prefix /usr/local` it's `/usr/local/bin/flowforge` (global). If you used `--prefix "$HOME/.local"` it's `~/.local/bin/flowforge` (just your user). If you didn't install, it's only `build/<preset>/bin/flowforge` inside the repo. | Only if you chose a global prefix. |
| The engine **library + headers** | alongside the binary under the same prefix: `<prefix>/lib/libflowforge_core.a`, `<prefix>/include/flowforge/…`. | Same as above. |
| Per-project **state** (run history, logs, cache) | a `.flowforge/` directory in whatever folder you run commands from (or the `--state-dir` you pass). Nothing is written to your home directory or `/etc` by default. | No — one per project. |

FlowForge writes **no** dotfiles in `$HOME`, no `launchd` agent, no
`/Library` entries, and no global config. `~/vcpkg` is the dependency manager
*you* cloned, not part of FlowForge.

**To uninstall completely:**

```sh
# 1. Remove the installed binary + library + headers (match the prefix you used)
sudo rm -f  /usr/local/bin/flowforge
sudo rm -rf /usr/local/include/flowforge
sudo rm -f  /usr/local/lib/libflowforge_core.a
#   ...or, for a --prefix "$HOME/.local" install:
rm -f  "$HOME/.local/bin/flowforge"
rm -rf "$HOME/.local/include/flowforge"
rm -f  "$HOME/.local/lib/libflowforge_core.a"

# 2. Delete the source tree and its build output
rm -rf /path/to/flowforge

# 3. Delete per-project state wherever you created it (loses run history + cache)
rm -rf /path/to/your-project/.flowforge

# 4. (optional) the line you added to ~/.zshrc
#    export VCPKG_ROOT="$HOME/vcpkg"
#    ...and, if you only cloned vcpkg for this:  rm -rf ~/vcpkg
```

If you installed with CMake you can also list exactly what it placed:
`cat build/release/install_manifest.txt`, then delete those paths.

---

## 2. First run in 60 seconds

```sh
mkdir ~/ff-demo && cd ~/ff-demo

flowforge init
```

`init` creates:

- `.flowforge/` — the state directory (SQLite database, cancellation
  sentinels). Everything FlowForge remembers lives here.
- `.flowforge/config.json` — optional settings.
- `pipeline.json` — a one-task sample.

Now run it:

```sh
flowforge run pipeline.json
```

```
Pipeline: sample  (1 tasks)

[RUNNING ] hello
    hello | stdout: hello from flowforge
[SUCCESS ] hello  0.01s

Pipeline SUCCEEDED in 0.01s
  1 succeeded, 0 cached, 0 failed, 0 skipped, 0 cancelled  (of 1)
run id: 1
```

Inspect it:

```sh
flowforge runs
flowforge status 1
flowforge logs 1
```

That's the whole loop: **write JSON → `flowforge run` → inspect with
`runs` / `status` / `logs`.**

---

## 3. The pipeline JSON file

A pipeline is a single JSON object. Minimum viable file:

```json
{
  "schema_version": 1,
  "name": "my_pipeline",
  "tasks": [
    {
      "id": "step1",
      "type": "executable",
      "executable": "/bin/echo",
      "arguments": ["hello"]
    }
  ],
  "dependencies": []
}
```

### 3.1 Top-level fields

| Field | Required | Meaning |
| --- | --- | --- |
| `schema_version` | no (default `1`) | Must be `1`. |
| `name` | **yes** | Any non-empty string. |
| `description` | no | Free text. |
| `max_concurrency` | no | Max tasks running at once. `0` = use the CLI/engine default (your core count, capped at 8). |
| `defaults` | no | Per-task defaults (see 3.4). |
| `tasks` | **yes** | The list of tasks. At least one. |
| `dependencies` | no | Edges: `[{ "from": "a", "to": "b" }]` — `b` runs only after `a` succeeds. |

### 3.2 A task

```json
{
  "id": "train",
  "name": "Train the model",
  "type": "python",
  "interpreter": "python3",
  "script": "train.py",
  "arguments": ["clean.csv", "model.bin"],
  "env": { "OMP_NUM_THREADS": "4" },
  "working_directory": "src",
  "inputs": [{ "name": "data", "path": "clean.csv" }],
  "outputs": [{ "name": "model", "path": "model.bin" }],
  "retry": { "max_retries": 2, "base_delay_ms": 1000, "backoff_multiplier": 2.0 },
  "resources": { "cpu_cores": 4, "memory_mb": 2048 },
  "timeout_ms": 600000,
  "priority": 10,
  "cache": true,
  "depends_on": ["prepare"]
}
```

| Field | Applies to | Notes |
| --- | --- | --- |
| `id` | all | Unique within the pipeline. Used everywhere (`--task`, logs, graph). |
| `name` | all | Human label. Defaults to `id`. |
| `type` | all | `"python"` or `"executable"`. |
| `interpreter` | python | The Python binary. Defaults to `defaults.python_interpreter` (`python3`). Searched on `PATH` if it has no `/`. |
| `script` | python | The `.py` file. Resolved against the working directory. |
| `executable` | executable | The program. Searched on `PATH` if it has no `/`; a relative path like `./tool` resolves against the working directory. |
| `arguments` | all | Passed **verbatim** to the program — no shell, no quoting, no globbing. |
| `env` | all | Extra environment variables, layered on top of the inherited environment. |
| `working_directory` | all | The directory the task runs in. Relative paths resolve against the pipeline's **base directory** (the folder the JSON file is in). Default: the base directory itself. |
| `inputs` | all | Files the task needs. Checked to exist **before** the task starts; a missing input fails the task (and is not retried). |
| `outputs` | all | Files the task should produce. Verified to exist **after** a successful exit; a missing output fails the task. |
| `retry` | all | `max_retries` extra attempts, with exponential backoff. See 7.1. |
| `resources` | all | `cpu_cores` / `memory_mb` / `gpu_count` reserved while the task runs. See 7.3. |
| `timeout_ms` | all | Kill the task (SIGTERM then SIGKILL) if one attempt runs longer than this. |
| `priority` | all | Higher numbers run first when several tasks are ready and slots are scarce. Default `0`. |
| `cache` | all | If `true`, an unchanged task is served from cache. See 7.2. |
| `depends_on` | all | Shorthand for dependency edges — `["prepare"]` means "run after `prepare`". |

### 3.3 How data flows

FlowForge does **not** pipe data between tasks. Tasks read and write **files**;
FlowForge just tracks them (path, size, mtime, optional checksum). A typical
chain:

```
prepare.py  ─writes→  clean.csv  ─read by→  features.py  ─writes→  features.bin  ─read by→  train.py
```

You wire the ordering with `dependencies` / `depends_on`, and you name the
files each task reads/writes in `inputs` / `outputs` so FlowForge can validate
them and key the cache on them.

### 3.4 `defaults`

Anything you'd repeat on every task can go here instead:

```json
"defaults": {
  "python_interpreter": "python3.12",
  "retry": { "max_retries": 1 },
  "resources": { "cpu_cores": 1 },
  "timeout_ms": 300000,
  "cache": true
}
```

Per-task values override the matching default.

### 3.5 Artifact paths, precisely

`artifact.path` is resolved against the task's `working_directory`, which is
resolved against the **pipeline base directory** (the folder containing the
JSON file). Absolute paths are used unchanged. So if
`~/proj/pipeline.json` has a task with `"working_directory": "src"` and an
output `"path": "out/model.bin"`, the file is
`~/proj/src/out/model.bin`.

### 3.6 Running a shell one-liner

There is deliberately no shell. To run a shell snippet, invoke the shell
explicitly:

```json
{ "id": "combine", "type": "executable", "executable": "/bin/sh",
  "arguments": ["-c", "cat a.txt b.txt > combined.txt"],
  "working_directory": "." }
```

---

## 4. Worked example: a data project

A realistic layout: monthly CSVs in `data/`, a mix of Python and C++ code in
`src/`. The pipeline merges the CSVs, cleans them, computes summary statistics
with a **compiled C++ binary** (the pipeline builds it as its first step), and
writes a report.

### 4.1 The project

```
sales-pipeline/
├── data/
│   ├── january.csv
│   └── february.csv
├── src/
│   ├── merge.py       # combine data/*.csv           -> work/combined.csv
│   ├── clean.py       # drop rows with a bad amount  -> work/clean.csv
│   ├── stats.cpp      # count / sum / min / max / mean -> work/stats.txt
│   └── report.py      # clean.csv + stats.txt        -> out/report.txt
└── pipeline.json
```

`data/january.csv` (note the blank row — `clean` will drop it):

```
region,amount
north,1200
south,900
east,1500
west,
north,1100
```

`data/february.csv` (`south` has a non-numeric amount):

```
region,amount
north,1300
south,invalid
east,1400
west,1000
```

`src/merge.py`:

```python
#!/usr/bin/env python3
"""Combine every CSV in data/ into one file, keeping a single header."""
import csv, glob, sys
from pathlib import Path

def main() -> int:
    out_path = sys.argv[1]
    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    files = sorted(glob.glob("data/*.csv"))
    if not files:
        print("merge: no CSV files in data/", file=sys.stderr)
        return 1
    rows = 0
    with open(out_path, "w", newline="", encoding="utf-8") as out:
        writer = None
        for path in files:
            with open(path, newline="", encoding="utf-8") as f:
                reader = csv.reader(f)
                header = next(reader)
                if writer is None:
                    writer = csv.writer(out)
                    writer.writerow(header)
                for row in reader:
                    writer.writerow(row)
                    rows += 1
    print(f"merge: combined {len(files)} files, {rows} rows -> {out_path}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

`src/clean.py`:

```python
#!/usr/bin/env python3
"""Drop rows whose amount is missing or non-numeric."""
import csv, sys

def main() -> int:
    src, dst = sys.argv[1], sys.argv[2]
    kept = dropped = 0
    with open(src, newline="", encoding="utf-8") as fin, \
         open(dst, "w", newline="", encoding="utf-8") as fout:
        reader = csv.reader(fin)
        writer = csv.writer(fout)
        writer.writerow(next(reader))                       # header
        for row in reader:
            if len(row) != 2 or not row[1].strip().isdigit():
                dropped += 1
                continue
            writer.writerow([row[0].strip(), row[1].strip()])
            kept += 1
    print(f"clean: kept {kept}, dropped {dropped}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

`src/stats.cpp`:

```cpp
// Reads a two-column CSV (region,amount); writes count/sum/min/max/mean.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: stats <input.csv> <output.txt>\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) { std::cerr << "stats: cannot open " << argv[1] << "\n"; return 1; }

    std::string line;
    std::getline(in, line);                                 // header
    long count = 0, sum = 0;
    long lo = std::numeric_limits<long>::max();
    long hi = std::numeric_limits<long>::min();
    while (std::getline(in, line)) {
        const auto comma = line.find(',');
        if (comma == std::string::npos) continue;
        const long v = std::stol(line.substr(comma + 1));
        sum += v; ++count;
        lo = std::min(lo, v); hi = std::max(hi, v);
    }
    std::ofstream out(argv[2]);
    out << "count=" << count << "\nsum=" << sum << "\n";
    if (count > 0)
        out << "min=" << lo << "\nmax=" << hi << "\nmean="
            << (static_cast<double>(sum) / static_cast<double>(count)) << "\n";
    std::cout << "stats: " << count << " rows summarised\n";
    return 0;
}
```

`src/report.py`:

```python
#!/usr/bin/env python3
"""Combine the cleaned data and the stats into a readable report."""
import csv, sys
from pathlib import Path

def main() -> int:
    clean_csv, stats_txt, out_txt = sys.argv[1], sys.argv[2], sys.argv[3]
    Path(out_txt).parent.mkdir(parents=True, exist_ok=True)
    by_region = {}
    with open(clean_csv, newline="", encoding="utf-8") as f:
        reader = csv.reader(f)
        next(reader)
        for region, amount in reader:
            by_region[region] = by_region.get(region, 0) + int(amount)
    with open(out_txt, "w", encoding="utf-8") as out:
        out.write("SALES REPORT\n============\n\nBy region:\n")
        for region in sorted(by_region):
            out.write(f"  {region:6s} {by_region[region]}\n")
        out.write("\nOverall:\n")
        out.write(Path(stats_txt).read_text())
    print(f"report: wrote {out_txt}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

### 4.2 The pipeline

`pipeline.json` lives in the **project root**, so every relative path below is
relative to `sales-pipeline/`. The `setup` task creates the output
directories (FlowForge does not create them for you); `build_stats` compiles
`src/stats.cpp` — and `stats` may reference `bin/stats` even though it does not
exist yet, because an upstream task it depends on produces it.

```json
{
  "schema_version": 1,
  "name": "sales_report",
  "description": "Merge monthly CSVs, clean them, compute stats with a native binary, build a report.",
  "defaults": { "python_interpreter": "python3", "cache": true },
  "tasks": [
    {
      "id": "setup", "name": "Create output directories",
      "type": "executable", "executable": "/bin/mkdir",
      "arguments": ["-p", "bin", "work", "out"],
      "cache": false
    },
    {
      "id": "build_stats", "name": "Compile src/stats.cpp",
      "type": "executable", "executable": "/usr/bin/c++",
      "arguments": ["-O2", "-std=c++17", "src/stats.cpp", "-o", "bin/stats"],
      "inputs":  [{ "name": "source", "path": "src/stats.cpp" }],
      "outputs": [{ "name": "binary", "path": "bin/stats" }],
      "depends_on": ["setup"]
    },
    {
      "id": "merge", "name": "Merge monthly CSVs",
      "type": "python", "script": "src/merge.py",
      "arguments": ["work/combined.csv"],
      "inputs": [
        { "name": "january",  "path": "data/january.csv" },
        { "name": "february", "path": "data/february.csv" }
      ],
      "outputs": [{ "name": "combined", "path": "work/combined.csv" }],
      "depends_on": ["setup"]
    },
    {
      "id": "clean", "name": "Drop invalid rows",
      "type": "python", "script": "src/clean.py",
      "arguments": ["work/combined.csv", "work/clean.csv"],
      "inputs":  [{ "name": "combined", "path": "work/combined.csv" }],
      "outputs": [{ "name": "clean",    "path": "work/clean.csv" }],
      "depends_on": ["merge"]
    },
    {
      "id": "stats", "name": "Summary statistics (native)",
      "type": "executable", "executable": "bin/stats",
      "arguments": ["work/clean.csv", "work/stats.txt"],
      "inputs":  [{ "name": "clean", "path": "work/clean.csv" }],
      "outputs": [{ "name": "stats", "path": "work/stats.txt" }],
      "depends_on": ["clean", "build_stats"]
    },
    {
      "id": "report", "name": "Build the report",
      "type": "python", "script": "src/report.py",
      "arguments": ["work/clean.csv", "work/stats.txt", "out/report.txt"],
      "inputs": [
        { "name": "clean", "path": "work/clean.csv" },
        { "name": "stats", "path": "work/stats.txt" }
      ],
      "outputs": [{ "name": "report", "path": "out/report.txt" }],
      "depends_on": ["clean", "stats"]
    }
  ],
  "dependencies": []
}
```

### 4.3 Run it

From the project root:

```sh
cd sales-pipeline

flowforge validate pipeline.json
#  ok: 'sales_report' is valid (6 tasks, 7 dependencies)

flowforge graph pipeline.json
```

```
Levels (tasks on the same level have no ordering constraint):
  0  setup
  1  build_stats, merge
  2  clean
  3  stats
  4  report
```

```sh
flowforge run pipeline.json --max-concurrency 3
```

```
[SUCCESS ] setup  0.00s
[SUCCESS ] merge  0.10s          build_stats + merge ran together
[SUCCESS ] clean  0.02s
[SUCCESS ] build_stats  0.79s
[SUCCESS ] stats  0.87s
[SUCCESS ] report  0.08s

Pipeline SUCCEEDED in 1.75s
  6 succeeded, 0 cached, 0 failed, 0 skipped, 0 cancelled  (of 6)
run id: 1

$ cat out/report.txt
SALES REPORT
============

By region:
  east   2900
  north  3600
  south  900
  west   1000

Overall:
count=7
sum=8400
min=900
max=1500
mean=1200
```

(The two dropped rows — `west` with a blank amount and `south,invalid` — are
why `count` is 7, not 9.)

### 4.4 Re-run and change an input

Run it again — nothing changed, so everything is served from cache
(`setup` has `"cache": false`, so it always runs):

```sh
flowforge run pipeline.json
#  [SUCCESS ] setup   0.00s
#  [CACHED  ] build_stats
#  [CACHED  ] merge
#  [CACHED  ] clean
#  [CACHED  ] stats
#  [CACHED  ] report
#  Pipeline SUCCEEDED in 0.01s
#    1 succeeded, 5 cached, 0 failed, ...
```

Now edit a CSV and re-run:

```sh
echo "central,2000" >> data/january.csv
flowforge run pipeline.json
```

`build_stats` stays `CACHED` (its input `src/stats.cpp` is untouched), but
`merge → clean → stats → report` all re-execute because the changed
`data/january.csv` flows down the chain and changes each cache key.

```sh
flowforge runs
flowforge status 3
flowforge logs 3 --task clean
```

---

## 5. Running a pipeline

### 5.1 Validate first (no execution)

```sh
flowforge validate pipeline.json
```

It checks: unique task ids, that every dependency points at a real task, no
cycles (reported as `a -> b -> c -> a`), that interpreters/executables exist
and are runnable, that scripts exist, and that each task's resources fit your
machine. Errors name the task and field:

```
error: task 'train' [interpreter]: Python interpreter not found: /bad/python3
```

Add `--strict` to also require that root-task input files already exist and to
treat warnings as failures.

### 5.2 See the shape of the graph

```sh
flowforge graph pipeline.json
```

```
Pipeline: my_pipeline  (4 tasks, 4 dependencies)

Levels (tasks on the same level have no ordering constraint):
  0  prepare
  1  features, statistics
  2  train

Dependency tree (roots -> leaves, '*' = subtree shown above):
prepare
|-- features
|   `-- train
`-- statistics
    `-- train *
```

### 5.3 Run it

```sh
flowforge run pipeline.json
```

Useful flags:

| Flag | Effect |
| --- | --- |
| `--max-concurrency N` | Cap simultaneously running tasks for this run. |
| `--no-cache` | Ignore the cache and don't write to it. |
| `--checksums` | Hash every artifact (stronger cache keys, slower). |
| `--plain` | One timestamped line per event — good for CI / logs / piping to a file. Automatic when stdout isn't a terminal. |
| `--quiet` | Keep the status lines, drop the per-line task output. |

The command exits **0** only if the pipeline `SUCCEEDED`. It exits **1** if the
pipeline `FAILED` / `CANCELLED` or validation failed, and **2** for a usage
error. So you can do:

```sh
flowforge run pipeline.json && echo "ok" || echo "pipeline failed"
```

### 5.4 Stopping a run

- Press **Ctrl-C** in the terminal running `flowforge run`. Running tasks get
  SIGTERM, then SIGKILL after a short grace period; pending tasks are
  cancelled; the run ends `CANCELLED`.
- From **another terminal**, while a run is in progress:

  ```sh
  flowforge cancel <run-id>       # run-id from `flowforge runs`
  ```

  This writes a sentinel the running engine notices within ~150 ms.

---

## 6. Inspecting runs

Everything is stored in `.flowforge/flowforge.sqlite` and survives across
invocations.

```sh
flowforge runs                       # recent runs: ID, pipeline, state, started, duration
flowforge runs --limit 50

flowforge status 7                   # one run in detail + per-task table
```

```
run 7  pipeline 'my_pipeline'
state      : FAILED
started    : 2026-08-29T10:15:02.101Z
duration   : 12.44s
concurrency: 4
tasks      : 4 total | 2 ok, 0 cached, 1 failed, 1 skipped, 0 cancelled

TASK                      STATE       ATTEMPTS  EXIT      DURATION
prepare                   SUCCEEDED   1         0         1.02s
features                  FAILED      3         1         8.30s
statistics                SUCCEEDED   1         0         0.91s
train                     SKIPPED     0         -         -
```

Logs (captured stdout/stderr of every attempt, plus engine notes):

```sh
flowforge logs 7                         # everything, oldest first
flowforge logs 7 --task features         # just one task
flowforge logs 7 --stream stderr         # just stderr lines
flowforge logs 7 --limit 500
```

You can also query the database directly:

```sh
sqlite3 .flowforge/flowforge.sqlite \
  "SELECT task_id, state, attempts FROM task_runs WHERE run_id = 7;"
```

---

## 7. Recipes

### 7.1 Retry a flaky task

```json
{
  "id": "download",
  "type": "executable", "executable": "./fetch.sh",
  "arguments": ["https://example.com/data"],
  "retry": { "max_retries": 3, "base_delay_ms": 2000, "backoff_multiplier": 2.0, "max_delay_ms": 30000 }
}
```

Up to 4 attempts total. Backoff before attempt 2 is 2 s, before 3 is 4 s,
before 4 is 8 s (capped at `max_delay_ms`). Every attempt is recorded — see
`flowforge status`. Deterministic errors (interpreter missing, declared input
missing) are **never** retried regardless of this policy.

### 7.2 Skip work that hasn't changed (caching)

Give the task `"cache": true` and declare its `inputs` and `outputs`:

```json
{
  "id": "build_index",
  "type": "python", "script": "index.py",
  "arguments": ["corpus/", "index.bin"],
  "inputs": [{ "name": "corpus", "path": "corpus.tar", "checksum": true }],
  "outputs": [{ "name": "index", "path": "index.bin", "checksum": true }],
  "cache": true
}
```

- **First run:** executes, stores the result.
- **Second run, nothing changed:** the task shows `CACHED` and downstream tasks
  proceed as if it succeeded.
- **Input changes:** the cache key changes → it executes again.
- Only successful (exit 0) runs are cached. A cached entry whose output file
  was deleted or modified is discarded, never reused.

Manage the cache:

```sh
flowforge clean-cache                       # drop everything
flowforge clean-cache --older-than-days 30  # drop entries unused for 30 days
flowforge run pipeline.json --no-cache      # ignore it for one run
```

The cache key covers task type, program, script, arguments, environment,
working directory and input identity. It deliberately ignores `name`,
`priority`, `retry` and `resources`.

### 7.3 Limit resource usage

```json
"defaults": { "resources": { "cpu_cores": 1 } },
"tasks": [
  { "id": "a", "type": "executable", "executable": "./work", "resources": { "cpu_cores": 4 } },
  { "id": "b", "type": "executable", "executable": "./work", "resources": { "cpu_cores": 4 } },
  { "id": "c", "type": "executable", "executable": "./work", "resources": { "cpu_cores": 4 } }
]
```

On an 8-core machine, `a` and `b` run together; `c` waits until one frees a
reservation. This is separate from `--max-concurrency` (both gates apply).
`cpu_cores` is a declared weight for scheduling, not a hard CPU limit; GPUs are
counted but not device-managed.

### 7.4 Fan out and join

```json
"tasks": [
  { "id": "split",   "type": "executable", "executable": "./split" },
  { "id": "shard_1", "type": "executable", "executable": "./process", "depends_on": ["split"] },
  { "id": "shard_2", "type": "executable", "executable": "./process", "depends_on": ["split"] },
  { "id": "shard_3", "type": "executable", "executable": "./process", "depends_on": ["split"] },
  { "id": "merge",   "type": "executable", "executable": "./merge",
    "depends_on": ["shard_1", "shard_2", "shard_3"] }
],
"dependencies": []
```

Run with `--max-concurrency 3` to overlap the shards.

### 7.5 What happens when a task fails

Failure is **branch-local**. If `shard_2` fails and retries are exhausted:
`shard_2 → FAILED`, `merge → SKIPPED` (a dependency didn't succeed), but
`shard_1` and `shard_3` still run to completion. The run ends `FAILED`.
Unrelated branches are never cancelled just because one branch failed.

### 7.6 Python tasks

```json
{
  "id": "clean",
  "type": "python",
  "interpreter": "python3",
  "script": "clean.py",
  "arguments": ["raw.csv", "clean.csv"],
  "working_directory": ".",
  "inputs": [{ "name": "raw", "path": "raw.csv" }],
  "outputs": [{ "name": "clean", "path": "clean.csv" }]
}
```

`clean.py` just reads `sys.argv[1]` and writes `sys.argv[2]`. FlowForge runs
`python3 clean.py raw.csv clean.csv` in the working directory. Don't assume a
bare `python` exists — name `python3` (or an absolute path, or a virtualenv's
`bin/python`).

### 7.7 CI usage

```sh
flowforge run pipeline.json --plain --no-cache
rc=$?
flowforge logs "$(flowforge runs --limit 1 | awk 'NR==2 {print $1}')" > run.log
exit $rc
```

`--plain` gives greppable timestamped lines; the exit code is the pass/fail
signal.

---

## 8. Configuration

Settings are resolved lowest-precedence first:

1. **Built-in defaults** — `max_concurrency` = CPU cores (capped at 8),
   `python_interpreter` = `python3`, cache on, database at
   `.flowforge/flowforge.sqlite`.
2. **A JSON file** — `.flowforge/config.json`, else `./flowforge.json`, else
   whatever you pass to `--config <file>`.
3. **Environment variables** — `FLOWFORGE_STATE_DIR`,
   `FLOWFORGE_MAX_CONCURRENCY`, `FLOWFORGE_PYTHON`, `FLOWFORGE_DB`,
   `FLOWFORGE_CACHE` (`0`/`1`), `FLOWFORGE_CHECKSUMS`, `FLOWFORGE_LOG_LEVEL`,
   `FLOWFORGE_CPU_CORES`.
4. **CLI flags** — `--state-dir`, `--db`, `--config`, and the `run` flags.

Example `.flowforge/config.json`:

```json
{
  "max_concurrency": 4,
  "python_interpreter": "python3",
  "cache_enabled": true,
  "compute_checksums": false,
  "resources": { "cpu_cores": 8, "memory_mb": 16384 }
}
```

A malformed config file is ignored (defaults stand) — it never aborts a
command.

Keep separate histories by pointing at different state directories:

```sh
flowforge run pipeline.json --state-dir .flowforge-experiments
flowforge runs               --state-dir .flowforge-experiments
```

---

## 9. Command reference

```
flowforge init                         scaffold .flowforge/ and a sample pipeline.json
flowforge validate <file> [--strict]   check a pipeline without running it
flowforge graph <file>                 print the DAG (levels + dependency tree)
flowforge run <file> [options]         execute a pipeline
    --max-concurrency <n>              cap running tasks
    --no-cache                         ignore + don't write the result cache
    --checksums                        hash all artifacts
    --plain                            one line per event (CI)
    --quiet                            hide per-task log lines
flowforge runs [--limit <n>]           list recent runs (default 20)
flowforge status <run-id>              one run in detail
flowforge logs <run-id> [options]      captured logs
    --task <id>                        only this task
    --stream <stdout|stderr|engine>    only this stream
    --limit <n>                        max lines (default 200)
flowforge cancel <run-id>              request cancellation of a running run
flowforge clean-cache [--older-than-days <n>]   drop cached results
flowforge version
flowforge --help

global options (any command): --state-dir <dir>   --db <file>   --config <file>

exit codes:  0 success   1 reported failure   2 usage error
```

---

## 10. Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| `CMake ... requires ... VCPKG_ROOT` or deps not found | `export VCPKG_ROOT=$HOME/vcpkg` (and put it in `~/.zshrc`). Re-run `cmake --preset release`. |
| Configure is slow the first time | vcpkg is building `sqlite3` / `spdlog` / `gtest` from source. It's cached afterwards. |
| `error: task 'x' [interpreter]: Python interpreter not found` | The `interpreter` isn't on `PATH`. Use `python3`, an absolute path, or a venv's `bin/python`. |
| `error: task 'x' [executable]: executable not found or not runnable` | Wrong path, or the file isn't `chmod +x`. Relative executables resolve against the task's `working_directory`. |
| `declared input '...' does not exist` | The `inputs` path (resolved against `working_directory` → pipeline base dir) is wrong, or an upstream task didn't actually produce it. |
| `task exited 0 but declared output '...' was not produced` | The program returned success but didn't write the file you listed in `outputs`. Fix the program or the declared path. |
| `pipeline contains a cycle: a -> b -> c -> a` | Remove one of those dependency edges. |
| Task hangs forever | Add `"timeout_ms"`; FlowForge will SIGTERM then SIGKILL it. |
| Second run didn't use the cache | The task needs `"cache": true`; an input or argument changed; or `--no-cache` was passed; or an output file was modified/removed since. |
| A previous run shows `INTERRUPTED` | The engine process was killed mid-run. FlowForge marks such runs on the next command. Just run the pipeline again — cached tasks are skipped. |
| Colours/box characters look odd when piping to a file | Use `--plain`. |
| Want to start clean | Delete the `.flowforge/` directory (loses all run history and the cache) — or use a fresh `--state-dir`. |

For how the scheduler makes its decisions, see
[`scheduler.md`](scheduler.md) and [`execution-model.md`](execution-model.md).
