# ADR-0002 — Own archetype-based ECS

- **Status:** Accepted
- **Date:** 2026-10-20

## Context

GoldSrc uses *edicts* with a fixed cap and monolithic structures (`entvars_t`), and the entity
logic runs on a single thread. The project needs entities without a cap, cache-efficient
iteration, safe parallelism, determinism for replays and tests, integration with delta
replication and runtime extension by plugins.

## Decision

Implement an **own ECS** based on **archetypes and 16 KiB chunks (SoA)**, with 64-bit
handles (index + generation), typed queries, a scheduler that derives parallelism from the
declared access, command buffers with deterministic application and change versions per
chunk and column ([03](../03-ecs.md)).

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| EnTT (sparse sets) | Excellent, but multi-component iteration is less cache-friendly. Integration with the scheduler, replication and plugin components would require layers on top |
| flecs | Very complete, but a C API with its own concepts (relations, pipelines) and less control over determinism and layout for the network |
| Object-oriented entity model (like the original) | No safe parallelism, memory fragmentation and coupling |

## Consequences

- ✅ Full control over layout, determinism, change versions and integration with
  replication and Rook plugins.
- ✅ No entity cap.
- ⚠️ Implementation and maintenance cost. Mitigation: minimal scope in M1, benchmarks and
  extensive tests.
- ⚠️ Frequent structural changes cost moves between archetypes. Mitigation: tags used
  sparingly, and measurement.
