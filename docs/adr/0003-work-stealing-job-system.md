# ADR-0003 — Work-stealing Job System, no fibers in v1

- **Status:** Accepted
- **Date:** 2026-10-20

## Context

The original is practically single-threaded. The project wants to use every core for batch
simulation, animation, visibility, per-client replication, render command recording and
asset streaming, without creating ad-hoc threads and without blocking.

## Decision

- Fixed worker pool with **Chase-Lev deques** (work-stealing) and a global priority queue.
- Dependencies through **atomic counters**: continuations are enqueued by the last job to
  finish.
- `wait` with *help-while-waiting*, never blocking a worker while work is available.
- A **`std::execution`**-compatible scheduler for long asynchronous flows (assets, I/O).
- **No fibers** in v1 ([04 §10](../04-job-system.md#10-out-of-scope-for-v1)).

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| Fibers (Naughty Dog style) | Complicates debugging, tools and `thread_local`. The benefit only shows with many waits in the middle of jobs, which the design avoids |
| oneTBB / Taskflow | Good options, but with less control over per-frame priorities, arena allocation and integration with Tracy and determinism. Large dependency |
| `std::execution` only | Overhead and ergonomics still immature for the frame's hot path |

## Consequences

- ✅ Predictable, measurable and deterministic (outputs by index).
- ✅ Modern integration with senders/receivers where it makes sense.
- ⚠️ Jobs cannot wait midway. Code has to be structured in phases and continuations.
  Reevaluate fibers if the profiler shows a relevant loss.
