# Project rules

The constitution of Modern-Client.v2. It binds every contributor, human or AI, in all code,
documents, data, scripts and decisions in the repository.

## 1. Supreme rule

Every contribution follows these rules **without exception**. No implementation,
optimization, abstraction, refactoring, dependency, shortcut or architectural decision may
break them.

When priorities conflict, the order is always:

1. Project rules (this document and the accepted [ADRs](adr/README.md))
2. Architectural integrity
3. Maintainability
4. Predictability
5. Security
6. Performance
7. Development speed

Speed never justifies degrading the architecture.

This document sets the principles. The normative details live in the documents it cites:
[01](01-architecture.md) (layers and modules), [02](02-coding-standards.md) (C++ code),
[18](18-testing-and-quality.md) (testing and the definition of done) and
[20](20-legal-and-licensing.md) (clean room and licenses). A contradiction between this
document and an ADR is a defect: a new ADR resolves it **before** any implementation, never
silently.

## 2. Philosophy

The project stays clean, predictable, minimal, modular, explicit, easy to maintain, scalable
and production-ready. The codebase evolves towards simplicity, never towards complexity: each
change solves a real problem with the least complexity it needs, and it fixes the **cause**,
never the symptom.

Forbidden: overengineering, premature abstraction, temporary solutions, hidden behavior,
implicit flows, chaotic defensiveness (checks without a clear contract) and architectural
inconsistency. The code stays understandable months later without historical context.

## 3. Repository

- Trunk-based development on `main`, which only receives commits with every required CI job
  green ([17 §7](17-build-and-tools.md#7-continuous-integration)).
- One commit per finished logical change, with its tests, documentation and
  [`TODO.md`](../TODO.md) updated **in the same commit**.
- Messages in English, as Conventional Commits, with a short imperative summary of the real
  change: `feat(console): add cvarlist, cmdlist and find`.
- Every commit carries a Signed-off-by line (DCO), because of the clean room
  ([§12](#12-legal-and-clean-room)).
- The root [`TODO.md`](../TODO.md) is the backlog: minimal, current and actionable. An item is
  checked `[x]` only by the [definition of done](18-testing-and-quality.md#definition-of-done).
- Every file has a license and a copyright notice in the REUSE format (SPDX header or
  `REUSE.toml`) and is stored with LF line endings.
- Never commit secrets, credentials, tokens, private data or Valve assets.
- No file, dependency, asset, log or tool without a real need. A clean, working codebase is
  worth more than obsolete compatibility, and technical debt is never taken on purpose.

## 4. Documentation

- **English is the default language** of every document: `docs/`, ADRs, `README.md`,
  `TODO.md`, `CONTRIBUTING.md`, `SECURITY.md`, code comments and commit messages
  ([ADR-0016](adr/0016-english-documentation.md)). Other languages appear only as
  content: translation files (`locale/pt-BR.toml`), localized diagnostics and player-facing
  text.
- **Documentation is part of the change.** A commit that changes behavior, a public API, the
  build, a workflow, a limit or a decision updates, in the same commit, every place that
  describes it: `docs/`, the ADRs, `///` comments, `README.md` and `TODO.md`.
- Documentation describes what exists, and marks what is planned as planned. A document that
  contradicts the code is a bug, fixed as soon as it is found.
- Status that drifts (the README status, the toolchain matrix, the CI table, test counts) is
  reviewed in every commit that affects it.
- Each fact lives in one place; other documents link to it instead of repeating it.

## 5. Architecture

### 5.1 Structure

- The layers of [01 §1](01-architecture.md#1-layered-view): a module depends only on layers
  below its own. Same-layer dependencies only when declared in the module catalog.
- Small, cohesive modules with one clear responsibility. No giant objects, no centralized
  accumulation of logic.
- Composition over inheritance; inheritance only for interfaces.
- No global state: dependencies arrive through constructors and composition happens in a
  single place (`app.*`). The two sanctioned exceptions are the contract handler and the
  default logger ([01 §2.1](01-architecture.md#21-no-global-state)).
- A lower layer never calls a higher one through callbacks: the higher layer queries.
- Deterministic flows instead of "dynamic magic". Business rules live in their owning module,
  never scattered.

### 5.2 Design

- Small, explicit APIs: inputs, outputs, side effects and failures are visible in the
  signature.
- An expected failure is an `nx::Result` with the module's own error code; a bug is a
  contract violation; an unrecoverable state is a controlled termination
  ([02 §6](02-coding-standards.md#6-typed-errors)).
- No silent fallback, no hidden recovery path at startup and no false resilience that hides
  a real failure. Errors surface clearly and predictably.
- State transitions are traceable: named states (`enum class`), typed events and a log entry
  where the transition matters to whoever operates the system.

### 5.3 Evolution

- Build what the next real step needs. Infrastructure arrives together with its **first
  consumer**, never speculatively.
- Systems are ready to grow without destructive rewrites: stable interfaces at the
  boundaries, private details behind them.
- Refactoring simplifies; it does not reshuffle complexity. Dead, obsolete or duplicated code
  is removed as soon as it appears.
- Every relevant structural decision becomes an [ADR](adr/README.md).

## 6. Code

[02-coding-standards.md](02-coding-standards.md) is the detailed standard. In short:

### 6.1 Style

- C++26 with named modules and `import std;`, without extensions, exceptions or RTTI.
  Features not yet universally supported sit behind facades in `nx.core`/`nx.meta`.
- Strong typing: domain values have their own types (`nx::Quantity`, units, handles), never
  raw `int`/`float`; narrowing conversions are explicit.
- Semantic, direct, readable code. Readability beats cleverness; templates and indirection
  need a reason.

### 6.2 Names

- camelCase across the project, including files and directories
  ([ADR-0013](adr/0013-camelcase-convention.md)); `PascalCase` for types and enumerators,
  `kPascalCase` for constants and `m_camelCase` for private members.
- Clear, descriptive names, without obscure abbreviations or artificial prefixes and
  suffixes. `snake_case` only where imposed from outside (cvars, commands, map classnames).

### 6.3 Logic

- Single responsibility, DRY, explicit ownership and lifetime (RAII; `new`/`delete` only
  inside allocators).
- **No magic numbers:** a value lives in game data, in a cvar or in a named constant, with a
  meaning, an explicit type and context ([02 §5](02-coding-standards.md#5-no-magic-numbers)).
- Forbidden: dead code, commented-out code, hidden state mutation, implicit ownership and
  unsafe resource handling. A `TODO` in code always references `TODO.md`.

## 7. Testing and reliability

- The [definition of done](18-testing-and-quality.md#definition-of-done) applies to every
  item: tests and a green CI are mandatory.
- Every test delivers real, measurable value: observable behavior, a bug's regression, a
  contract, determinism or parity. No noisy, redundant or expensive-to-maintain tests.
- Concurrent code runs under ThreadSanitizer. Every fixed bug gets a regression test.
- Contracts validate at runtime memory safety, resource lifetime, ownership, initialization
  order and failure visibility.
- Logs only where they help to operate or debug; on hot paths, through
  `nx::log::RateLimit`.

## 8. Performance

- Optimize responsibly: measure first (a benchmark for every hot path) and never trade
  maintainability for micro-optimizations.
- Avoid needless allocations and runtime overhead; short-lived memory uses the `nx.core`
  allocators.
- Stable, predictable tick and frame times. Deterministic simulation: the same result with
  any number of workers ([04 §8](04-job-system.md#8-determinism)).

Performance is intentional, never accidental.

## 9. Dependencies

- Every dependency justifies its existence and passes a review of license, maintenance and
  security surface ([17 §5](17-build-and-tools.md#5-dependencies)).
- In-house solutions when the complexity is low; no excess dependencies.
- External integrations stay isolated: headers only in the global module fragment, no
  third-party types in exported interfaces, built by the project triplets (no exceptions, no
  RTTI).
- Pinned versions (the vcpkg baseline), updated regularly.

## 10. Interface and experience

- Parity first: players and server operators recognize the original behavior (cvars,
  commands, configs, HUD).
- Clean, direct, functional interfaces, without visual complexity, states or options of no
  real use, and without truncated values
  ([12 §3.4](12-ui-hud-and-console.md#34-no-value-truncation)).
- Menus and flows minimize friction; information density is organized and intentional.

## 11. How the AI and every contributor operate

- Think before implementing: read the related code and documents before changing them.
- Preserve architectural consistency and point out maintenance risks.
- Report a rule violation **before** going on.
- Nothing speculative: never invent APIs, systems or behavior that do not exist.
- Nothing partial or unfinished: complete, working implementations.
- An item that depends on a human decision or an external resource (legal review, the
  original game, real players, reference hardware) is not simulated: it stays pending with
  the reason.
- Before finishing any change, review duplication, dead code, ambiguity, unsafe ownership,
  maintenance impact, consistency and the documentation it affects, then run the local
  checks (build, tests, `clang-format`, `reuse lint` and line endings).

## 12. Legal and clean room

[20-legal-and-licensing.md](20-legal-and-licensing.md) is the standard. In short:

- No Valve asset in the repository or in the binaries; tests use synthetic content
  (`devContent`).
- Forbidden: reading or copying leaked GoldSrc code, and copying code from the Half-Life SDK,
  ReHLDS, ReGameDLL_CS or any other incompatible source.
- Behavior is reimplemented from public documentation and specifications, from observing the
  game and from analyzing the data files themselves, and validated by parity tests.
  Disassembling or decompiling Valve binaries is not observation and is outside the clean
  room.

## 13. Forbidden patterns

- Singletons, service locators and hidden globals (beyond the two exceptions of
  [§5.1](#51-structure))
- Circular dependencies and undeclared edges between layers
- Deep inheritance trees and god classes
- State mutation without a clear owner and implicit resource ownership
- Runtime reflection where static reflection does the job
- Exceptions, RTTI, `bool` as an error code and ignored `Result`s

## 14. Engineering decisions

Among several solutions, the winner is the one that:

1. Reduces future maintenance
2. Improves predictability
3. Reduces hidden complexity
4. Minimizes coupling
5. Eases debugging
6. Has fewer moving parts
7. Preserves architectural consistency

## 15. Final directive

Every future instruction is read through this constitution. A request that conflicts with it
has the conflict stated explicitly before implementation goes on. The long-term integrity of
the project is mandatory and non-negotiable.
