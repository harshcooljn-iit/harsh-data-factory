# Caching

FlowForge caches **successful** task results so an unchanged task is not re-run.
A cache hit puts the task in state `CACHED`, which downstream tasks treat
exactly like `SUCCEEDED`.

## Cache key

`cache::compute_cache_key(task, probed_inputs)` hashes a canonical,
tab/newline-delimited blob of:

- a key-format version tag (`ffcache1`)
- `task.type`
- `task.program`, `task.script`
- the full argument vector
- environment overrides, **sorted by name**
- `task.working_directory` (as declared)
- the **identity** of every declared input, **sorted by logical name**:
  - `sha256:<hex>` when that input has `checksum: true`
  - otherwise `size:<bytes>|mtime:<unix-ms>`

Deliberately **excluded** (they do not change the produced files):
`task.name`, `priority`, `retry` policy, `resources`, `timeout`.

The key is `ffcache1:<sha256 of the blob>`.

## Store rules

- `CacheStore::store()` is only ever called for `exit_code == 0`. **Failed
  executions are never cached.**
- It records the exit code plus a snapshot of every declared output
  (`logical_name`, absolute `path`, `size`, `mtime`, and `sha256` when the
  output declared `checksum: true`).

## Hit rules

`CacheStore::lookup(key)` returns a hit **only if every recorded output still
validates**:

- the file still exists, **and**
- if a checksum was recorded, the current file's checksum matches it;
  otherwise the current byte size matches.

If any output is missing or changed, the entry is **deleted** and treated as a
miss. An invalid cache entry is never reused.

A hit bumps `last_used_at` and `hit_count`.

## Worked example (`examples/cache_pipeline`)

The `build` task hashes `data.in` into `artifact.out`, both declared with
`checksum: true`, `cache: true`.

| Run | Input | Outcome |
| --- | --- | --- |
| 1 | `data.in` = "…v1" | executes (~1 s sleep), stores entry keyed on the v1 checksum |
| 2 | `data.in` unchanged | `CACHED` — key matches, `artifact.out` still matches its stored checksum |
| 3 | `data.in` edited to "…v2" | key changes (input checksum differs) → executes again |
| — | delete `artifact.out`, rerun | previous entry fails validation → miss → executes |

This is asserted by `ExamplesTest.CachePipelineReusesResultThenRerunsOnInputChange`.

## Controlling the cache

| Where | Effect |
| --- | --- |
| task `"cache": false` | that task is never cached or served from cache |
| `defaults.cache` | default for tasks that don't say |
| `flowforge run --no-cache` | ignore and do not write the cache for the whole run |
| `flowforge run --checksums` | hash **all** artifacts (stronger keys, slower) |
| `flowforge clean-cache` | delete every cache entry |
| `flowforge clean-cache --older-than-days N` | delete entries not used in N days |

The cache lives in the `cache_entries` table (see [`persistence.md`](persistence.md)).
It stores metadata only — it never copies task output files.
