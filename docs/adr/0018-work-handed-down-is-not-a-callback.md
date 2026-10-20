# ADR-0018 — Work handed down is not a callback

- **Status:** Accepted
- **Date:** 2026-10-20

## Context

[RULES §5.1](../RULES.md#51-structure) says that a lower layer never calls a higher one through
callbacks: the higher layer queries. [01 §2.3](../01-architecture.md#23-communication-between-subsystems)
forbids "callbacks registered by lower-layer subsystems to call higher layers". Read literally,
three modules that already exist would break it, and so would most of the planned engine:

- `nx.jobs` (layer 1) runs the functions that `nx.ecs`, the renderer and the game submit;
- `nx.ecs` (layer 2) runs the systems that the game registers in a `Schedule`;
- `nx.console` (layer 1) runs the handler of each command the server, the client and plugins
  register (`kick`, `jobs_stats`).

The audit of the code against the rules raised the console's command handlers as a possible
violation. What the rule protects is the direction of knowledge: a lower layer must not learn
about the layers above it, and its own events must not reach them by calling into them, so that
order, threads and failures stay where the higher layer can see them.

## Decision

- **Forbidden:** a lower layer *notifying* a higher one by calling a function the higher one left
  for that purpose: observers, listeners, "on change" hooks of a lower module (a cvar change
  callback in `nx.console`, an asset-loaded callback in `nx.assets`). The higher layer queries
  instead (`LogLevelCvars::apply`, the revision of a cvar), or reads typed ECS events.
- **Not a callback in that sense:** a lower layer *running work that a higher layer hands to it
  to run*: a job, an ECS system, the handler of a console command. The higher layer decides what
  runs and owns it; the lower layer only decides when, and knows nothing about it beyond a
  signature it declares (`JobFunction`, a system's parameters, `CommandHandler`).
- RULES §5.1 and 01 §2.3 state the distinction and link here.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| Keep the literal rule and redesign the three modules | A job system that cannot run jobs, a scheduler without systems and a console whose commands are polled by every module: each would need the higher layer to drive the lower one step by step |
| List each module as an exception | Exceptions that grow with every module; the principle, not the list, is what reviews need |

## Consequences

- ✅ The rule says what it protects, and reviews can apply it to new modules: does the lower
  layer *notify* through a function from above (forbidden), or *run work handed to it* (allowed)?
- ✅ The existing modules comply as written.
- ⚠️ Work handed down runs in the lower layer's context (a worker thread, a console run), so its
  contract is documented where it is handed down (`JobFunction`, docs/03 §7, `CommandHandler`).
