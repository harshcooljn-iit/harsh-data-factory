# 0007 — Human-readable JSON pipeline format

## Context

Pipelines must be authored and reviewed by hand and diffed in version control.
Options: a bespoke DSL, YAML, TOML, or JSON.

## Decision

Pipelines are a single JSON object with a documented `schema_version`. A
conversion layer (`serialization/`) maps the document to/from the core
`domain::PipelineDefinition`; JSON never leaks into the scheduler or storage.
Parsing is **total** — it returns `{location, message}` errors and never throws.

## Consequences

- Ubiquitous tooling, obvious structure, `nlohmann-json` handles it.
- The core domain types stay free of serialization annotations; `dump_pipeline`
  produces a canonical form that round-trips.
- `schema_version` lets the loader reject formats it does not understand
  instead of guessing; additive fields stay within version 1.
- JSON has no comments — acceptable for v1; a future front end could accept
  YAML and emit this JSON.
- Validation of *meaning* (cycles, missing interpreter, resource fit) is a
  separate pass (`validation/`), so parse errors and semantic errors are
  reported distinctly.
