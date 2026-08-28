# Architecture Decision Records

Short records of *why* FlowForge is built the way it is. Each ADR: context,
decision, consequences. They are historical — later ADRs may supersede earlier
ones.

| # | Decision |
| --- | --- |
| [0001](0001-cli-first.md) | CLI-first, engine as a library |
| [0002](0002-dag-model.md) | Pipelines are an explicit DAG of tasks |
| [0003](0003-external-process-tasks.md) | Tasks are external processes |
| [0004](0004-no-thread-per-task.md) | One reactor thread, not a thread per task |
| [0005](0005-scheduler-strategy.md) | Dependency-counter, event-driven scheduler |
| [0006](0006-sqlite-persistence.md) | SQLite for persistence |
| [0007](0007-json-pipeline-format.md) | Human-readable JSON pipeline format |
| [0008](0008-file-artifacts.md) | Data flows through file artifacts |
| [0009](0009-optional-caching.md) | Result caching is optional and validated |
| [0010](0010-resource-model.md) | Coarse reservation-based resource model |
