# Pipeline JSON format (schema version 1)

A pipeline is a single JSON object. Parsing is **total**: it never throws, and
every problem is reported as `location: message` (e.g. `tasks[2].script: ...`).

## Top level

| Field | Type | Required | Default | Notes |
| --- | --- | --- | --- | --- |
| `schema_version` | int | no | `1` | Only `1` is accepted by this build. |
| `name` | string | **yes** | — | Non-empty. |
| `description` | string | no | `""` | |
| `max_concurrency` | int | no | `0` | `0` = use the engine/CLI value. |
| `defaults` | object | no | see below | Per-task defaults. |
| `tasks` | array | **yes** | — | At least one task. |
| `dependencies` | array | no | `[]` | `{ "from": id, "to": id }` edges. |

### `defaults`

| Field | Type | Default | Applied to |
| --- | --- | --- | --- |
| `python_interpreter` | string | `"python3"` | python tasks with no `interpreter` |
| `retry` | object | no retries | tasks with no `retry` |
| `resources` | object | `cpu_cores=1` | tasks with no `resources` |
| `timeout_ms` | int | none | tasks with no `timeout_ms` |
| `cache` | bool | `true` | tasks with no `cache` |

## Task object

| Field | Type | Required | Notes |
| --- | --- | --- | --- |
| `id` | string | **yes** | Unique within the pipeline. |
| `name` | string | no | Defaults to `id`. |
| `type` | `"python"` \| `"executable"` | **yes** | |
| `interpreter` | string | python only | Defaults to `defaults.python_interpreter`. |
| `script` | string | python only | The `.py` file. Resolved against the working dir. |
| `executable` | string | executable only | Program to run. `PATH` searched if it has no `/`. |
| `arguments` | string[] | no | Passed verbatim to `execve` — no shell, no quoting. |
| `env` | object<string,string> | no | Overrides applied on top of the inherited environment. |
| `working_directory` | string | no | Resolved against the pipeline's base directory (the folder the JSON lives in). |
| `inputs` | (string \| object)[] | no | See *Artifacts*. Checked to exist before the task starts. |
| `outputs` | (string \| object)[] | no | Verified to exist after a successful exit (when `verify_outputs`). |
| `retry` | object | no | Merged over `defaults.retry`. |
| `resources` | object | no | Merged over `defaults.resources`. |
| `timeout_ms` | int \| null | no | Wall-clock limit for one attempt. |
| `priority` | int | no | Higher runs first among ready tasks. Default `0`. |
| `cache` | bool | no | Default from `defaults.cache`. |
| `depends_on` | string[] | no | Sugar: adds `{from: dep, to: this}` edges. |

### `retry` object

```json
{ "max_retries": 2, "base_delay_ms": 500, "backoff_multiplier": 2.0, "max_delay_ms": 30000 }
```

### `resources` object

```json
{ "cpu_cores": 2, "memory_mb": 512, "gpu_count": 0 }
```

GPUs are an integer count only — no device management in 0.1.0.

### Artifacts

Short form (name defaults to the file's basename):

```json
"inputs": ["data/input.csv"]
```

Long form:

```json
"inputs": [{ "name": "raw", "path": "data/input.csv", "checksum": true }]
```

`checksum: true` makes FlowForge hash the file. Input checksums feed the cache
key; output checksums are recorded for cache validation. Hashing is opt-in
because it is expensive on large files.

**Path resolution:** `artifact.path` → resolved against the task's
`working_directory` → resolved against the pipeline's base directory. Absolute
paths are used as-is.

## Example

```json
{
  "schema_version": 1,
  "name": "ml_pipeline",
  "defaults": { "python_interpreter": "python3" },
  "tasks": [
    {
      "id": "prepare", "name": "Prepare data", "type": "python",
      "script": "prepare.py", "arguments": ["input.csv", "clean.csv"],
      "inputs": ["input.csv"], "outputs": ["clean.csv"]
    },
    {
      "id": "train", "type": "python", "script": "train.py",
      "arguments": ["clean.csv", "model.bin"],
      "inputs": ["clean.csv"], "outputs": ["model.bin"],
      "retry": { "max_retries": 1 },
      "resources": { "cpu_cores": 4, "memory_mb": 2048 },
      "timeout_ms": 600000, "priority": 10
    }
  ],
  "dependencies": [{ "from": "prepare", "to": "train" }]
}
```

## Round-tripping

`flowforge` reads this format and stores the canonical serialisation
(`serialization::dump_pipeline`) in the `pipelines` table. Load → dump → load is
covered by tests.

## Compatibility

- New optional fields may be added within `schema_version: 1`.
- A breaking change bumps `schema_version`; the loader rejects versions it does
  not recognise instead of guessing.
