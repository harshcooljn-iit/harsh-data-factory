# 0009 — Result caching is optional and validated

## Context

Re-running a pipeline should be able to skip work that would produce the same
result. But a cache that serves stale or wrong results is worse than no cache.

## Decision

- Caching is **opt-in per task** (`"cache": true`, default from
  `defaults.cache`) and can be disabled for a whole run (`--no-cache`).
- The cache key is a hash of only the things that change the output: task type,
  program, script, argv, sorted env, working dir, and input identities. Name,
  priority, retry policy, resources and timeout are excluded.
- Only successful executions (exit 0) are stored.
- A hit is served **only if every recorded output still validates** — file
  present, and checksum-match (if checksummed) or size-match otherwise. A
  stale entry is deleted, never reused.

## Consequences

- `first run → execute`, `second run → CACHED`, `input change → execute` — the
  documented, tested behaviour (`examples/cache_pipeline`).
- A cache hit sets the task to `CACHED`, which downstream treats as success.
- The cache stores metadata + a small outputs snapshot, never file contents.
- Excluding `resources`/`timeout` from the key is deliberate: they affect
  *whether/when* a task runs, not *what* it produces.
- Trade-off: validation on lookup does a `stat` (and possibly a re-hash) per
  output. That is the price of never serving a wrong result.
