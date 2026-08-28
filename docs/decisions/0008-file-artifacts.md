# 0008 — Data flows through file artifacts

## Context

Tasks produce and consume data. The orchestrator could pipe data between tasks
in memory, or tasks could exchange files and the orchestrator just track them.

## Decision

Data flows task → file → task. A task declares `inputs` and `outputs` as file
paths (with optional logical names and checksums). FlowForge stores **metadata
only**: resolved path, size, mtime, and an optional SHA-256. It never reads task
data into its own memory.

## Consequences

- Large datasets never pass through the orchestrator; memory use is bounded by
  bookkeeping, not payload size.
- Pre-run: declared inputs are checked to exist (missing input ⇒ non-retryable
  failure). Post-run: declared outputs are verified (missing output after
  exit 0 ⇒ task failure).
- Artifact identity (checksum, or size+mtime) feeds the cache key, so an
  unchanged input yields a cache hit.
- Tasks are responsible for their own file formats and locations; FlowForge
  only resolves paths (`artifact.path` → task working dir → pipeline base dir).
- Checksums are opt-in per artifact because hashing large files is expensive.
