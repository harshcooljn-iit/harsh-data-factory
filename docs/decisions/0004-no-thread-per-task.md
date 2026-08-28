# 0004 — One reactor thread, not a thread per task

## Context

The naive way to run and wait on N subprocesses is N threads each blocked in
`waitpid` / `read`. That scales poorly and complicates synchronisation.

## Decision

`PosixProcessRunner` runs a single background **reactor thread** that services
every child: one `poll(2)` loop over all stdout/stderr pipes plus a self-pipe
for commands. Child exit is observed as pipe EOF and confirmed with `waitpid`.
Timeouts and cancellation escalate SIGTERM→SIGKILL from the same loop. The
scheduler adds no threads of its own.

## Consequences

- Running 100 ready tasks adds 100 file descriptors to one poll set, not 100
  threads.
- All process lifecycle is serialised on one thread → no races around
  `waitpid`, pipe buffers, or the child table.
- Completions/log lines reach the scheduler through a single mutex-guarded
  event queue + condition variable — the only cross-thread channel.
- Portable across macOS and Linux with plain POSIX (`fork`/`execvp`/`poll`).
- Cost: the reactor thread is a single point of scheduling for I/O; fine at the
  concurrency levels a local orchestrator sees. A Windows backend would
  implement the same interface with IOCP.
