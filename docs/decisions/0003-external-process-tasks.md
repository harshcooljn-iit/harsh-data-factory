# 0003 — Tasks are external processes

## Context

A task could be an in-process plugin, a script interpreted by an embedded
runtime, or a separate OS process.

## Decision

Every task is an external process invoked with an explicit argument vector
(`program` + `argv`), environment, and working directory. No shell is involved:
arguments reach `execve` verbatim. Python is a *task runtime*, not part of the
orchestrator.

## Consequences

- Language independence: a task is any executable — Python, C++, Go, a shell
  script, `/bin/echo`.
- Hard isolation between orchestration and workload; a crashing task cannot
  take down the engine.
- Clean lifecycle signals: exit code, termination signal, timeout, spawn
  failure are all observable and distinct.
- No shell means no quoting bugs and no accidental glob/`$IFS` surprises; it
  also means "run this shell one-liner" is spelled `"/bin/sh", ["-c", "…"]`.
- Cost: process spawn overhead per task (irrelevant next to real task
  runtimes), and FlowForge must manage pids/pipes/reaping itself
  (see ADR 0004).
