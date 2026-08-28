# FlowForge examples

Each example is a real, runnable pipeline. The `pipeline.json.in` templates are
expanded by CMake into `build/<preset>/examples/<name>/pipeline.json` with
absolute paths, and any scripts / data are staged next to them.

Build once, then run any example:

```sh
cmake --build --preset debug           # builds the CLI + example helpers
FF=./build/debug/bin/flowforge
$FF run build/debug/examples/hello/pipeline.json
```

Run them all (resets per-example scratch state first):

```sh
./scripts/run-examples.sh              # uses the debug preset by default
```

| Example | What it shows |
| --- | --- |
| `hello` | A single native executable task; writes `greeting.txt`. |
| `python_pipeline` | Two Python tasks, data passed via CSV files. Needs `python3` on `PATH`. |
| `cpp_pipeline` | Two native C++ tasks: generate a number series → histogram. |
| `parallel_pipeline` | One task fans out to three independent workers, then a join. With `--max-concurrency 3` the workers overlap. |
| `failure_pipeline` | `bad_branch` fails → `bad_child` is `SKIPPED`; the independent `good_branch` still `SUCCEEDED`; the run is `FAILED`. |
| `retry_pipeline` | `flaky` fails its first two attempts, succeeds on the third. Delete `attempts.count` between runs. |
| `cache_pipeline` | First run executes; second run is `CACHED`; edit `data.in` and it executes again. |

Every example is also exercised by the `examples_test` integration test, so they
stay working.

## Inspecting a run

```sh
$FF runs
$FF status <run-id>
$FF logs <run-id> --task <task-id>
```
