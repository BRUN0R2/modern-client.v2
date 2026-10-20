# Documentation — Modern-Client.v2

> **Status:** draft v0.1 · **Language:** English (as are code identifiers and comments)
>
> **Modern-Client.v2** is the name of the project. It does not use Valve trademarks.
> "Counter-Strike" appears in this documentation only to describe the game being
> recreated (see [20-legal-and-licensing.md](20-legal-and-licensing.md)).

An open-source recreation of Counter-Strike 1.6, written from scratch in **C++26**, with
gameplay parity with the original and **without the historical limits** of GoldSrc:
a single thread, an entity cap, a precache cap, a 32-player cap and a mod ecosystem
tied to AMX Mod X/Pawn.

## Project rules

[RULES.md](RULES.md) is the constitution of the project: priorities, principles and rules that
apply to every contributor, human or AI. The documents below detail those rules and prevail
only within them.

## Recommended reading order

| # | Document | Contents |
|---|----------|----------|
| 00 | [Overview](00-overview.md) | Goals, non-goals, removed limits, principles |
| 01 | [Architecture](01-architecture.md) | Layers, modules, threads, frame cycle, dependency rules |
| 02 | [Coding standards](02-coding-standards.md) | C++26, naming, typed errors, contracts, ban on magic numbers |
| 03 | [ECS](03-ecs.md) | Entities, archetype-based components, queries, systems, scheduling |
| 04 | [Job System](04-job-system.md) | Multithreading, work-stealing, dependency graphs, `std::execution` |
| 05 | [Rendering](05-rendering.md) | RHI, Vulkan, DirectX 11 (future), render graph, GPU skinning |
| 06 | [Assets and resources](06-assets-and-resources.md) | VFS, GoldSrc formats, end of precache, resource download |
| 07 | [Simulation and physics](07-simulation-and-physics.md) | Tick, usercmd, movement (pmove), hull collision, map entities |
| 08 | [Networking](08-networking.md) | Protocol, delta snapshots, prediction, lag compensation, interest |
| 09 | [Dedicated server](09-dedicated-server.md) | `nextds`, cvars, commands, RCON, A2S, master server, identity |
| 10 | [Counter-Strike gameplay](10-counter-strike-gameplay.md) | Rounds, economy, weapons, objectives, game data |
| 11 | [Audio](11-audio.md) | Mixer, spatialization, room DSP, voice |
| 12 | [UI, HUD and console](12-ui-hud-and-console.md) | HUD, menus, console, cvars, binds, input |
| 13 | [Mod system](13-mod-system.md) | Runtime, isolation, packages, permissions, hot reload, client-side mods |
| 14 | [Rook language](14-rook-language.md) | Specification of the mod language |
| 15 | [Mod API](15-mod-api.md) | `std`, `nx` and `cs` modules, events, hooks, error catalog |
| 16 | [AMXX → Rook migration](16-amxx-migration.md) | Pawn/AMXX equivalences and converted examples |
| 17 | [Build and tools](17-build-and-tools.md) | Toolchain, CMake, dependencies, repository layout, CI |
| 18 | [Testing and quality](18-testing-and-quality.md) | Testing strategy, parity, determinism, performance |
| 19 | [Roadmap](19-roadmap.md) | Milestones M0–M8 and exit criteria |
| 20 | [Legal and licensing](20-legal-and-licensing.md) | Valve assets, clean room, trademarks, license |
| 21 | [Launcher and updates](21-launcher-and-updates.md) | Automatic, independent updates of the client and the server: signed, delta, atomic |
| — | [Glossary](glossary.md) | GoldSrc, engine and Rook terms |
| — | [ADRs](adr/README.md) | Architecture decision records |

The project's actionable backlog lives in [`../TODO.md`](../TODO.md).

## Conventions of this documentation

- **MUST / MUST NOT / MAY** indicate normative requirements (in the spirit of RFC 2119).
- C++ and Rook code examples are **illustrative**: they show the intent of the API, not a
  frozen contract. The official contract is the code and the accepted ADRs.
- Gameplay numeric values cited as a "reference" must be **validated against the
  original game** before they become official data (see
  [18-testing-and-quality.md](18-testing-and-quality.md#parity-tests)).
- Every relevant structural decision becomes an ADR in [`adr/`](adr/README.md). If a document
  contradicts an accepted ADR, the ADR prevails and the document must be fixed.

## Names used in the project

| Name | What it is |
|------|------------|
| **Modern-Client.v2** | The project as a whole (client, server, engine, language and tools) |
| **Next Engine** (`nx`) | The engine: core, ECS, jobs, RHI, render, audio, networking, assets, scripting host |
| **cstrike** (`cs`) | The Counter-Strike game implemented on top of the engine |
| **Rook** | Plugin language ("the piece that comes after the Pawn"). Extension `.rook` |
| **Plugin** | Smallest loadable unit, compiled by `rookc`. Equivalent to an `.amxx` |
| **Mod** | Distributable package with plugins, assets, translations and configs (e.g. ReZombie) |
| `next` / `nextds` | Client and dedicated server executables |
| `rookc` / `rook` | The Rook language's own compiler and CLI |
| `nxbake` | Asset import and baking tool |
