# Architecture decision records (ADR)

Every relevant structural decision is recorded here. An **accepted** ADR prevails over the
other documents. To change a decision, a new ADR is created that *supersedes* the previous
one, without erasing the history; one that only adds to it *amends* it, and the amended ADR
says where.

## Index

| ADR | Title | Status |
|-----|-------|--------|
| [0001](0001-cpp26-and-modules.md) | C++26 with named modules | Accepted |
| [0002](0002-archetype-ecs.md) | Own archetype-based ECS | Accepted |
| [0003](0003-work-stealing-job-system.md) | Work-stealing Job System, no fibers in v1 | Accepted |
| [0004](0004-vulkan-first-abstract-rhi.md) | Vulkan first, DirectX 11-compatible RHI, shaders in Slang | Accepted |
| [0005](0005-typed-errors-without-exceptions.md) | Typed errors with `std::expected` and contracts, without exceptions | Accepted |
| [0006](0006-own-plugin-language-rook.md) | Own plugin language (Rook) with its own compiler | Accepted, amended by 0021 |
| [0007](0007-own-vm-typed-bytecode.md) | Own VM with typed, verified bytecode | Accepted |
| [0008](0008-own-network-protocol.md) | Own network protocol, not compatible with protocol 48 | Accepted |
| [0009](0009-original-assets-from-the-user.md) | Original assets read from the user's installation | Superseded by 0015 |
| [0010](0010-license.md) | License: GPL-3.0-or-later, MIT (Rook and SDK) and CC BY 4.0 (docs) | Accepted |
| [0011](0011-player-limit-1024.md) | `maxplayers` from 1 to 1024 as a policy limit | Accepted |
| [0012](0012-typed-get-set-vars-pattern.md) | `get*Var`/`set*Var` pattern with compile-time checked keys | Accepted, amended by 0021 |
| [0013](0013-camelcase-convention.md) | camelCase convention across the project (C++ and Rook) | Accepted, amended by 0021 and 0027 |
| [0014](0014-std-module-built-by-the-project.md) | `std` module built by the project, without CMake's experimental `import std` | Accepted |
| [0015](0015-free-content-and-optional-original-assets.md) | Free content by default; original assets optional and configured locally | Accepted |
| [0016](0016-english-documentation.md) | English as the default language of the documentation | Accepted |
| [0017](0017-counter-strike-only.md) | Counter-Strike 1.6 only: no Half-Life and no other GoldSrc games | Accepted |
| [0018](0018-work-handed-down-is-not-a-callback.md) | Work handed down to a lower layer is not a forbidden callback | Accepted |
| [0019](0019-launcher-and-automatic-updates.md) | Automatic, independent updates for the client and the server | Accepted |
| [0020](0020-in-house-ui-toolkit.md) | In-house retained UI toolkit for the menus, the screens and the console | Accepted |
| [0021](0021-rook-amxx-names.md) | Rook accepts AMX Mod X's names and `get_*_var` through a compatibility layer | Accepted |
| [0022](0022-plugin-api-primitives.md) | The plugin API as a few generic primitives in place of the old modules | Proposed |
| [0023](0023-navigation.md) | Navigation meshes for every kind of agent, in `nx.navigation` | Accepted, amended by 0028 |
| [0024](0024-agents.md) | Agents: one system for bots, monsters, bosses and every character the game moves | Accepted, amended by 0028 |
| [0025](0025-enhanced-rendering.md) | The enhanced look: physically based materials, the map's reflections, HDR and bloom, the same on every card | Proposed |
| [0026](0026-clean-room-source-policy.md) | One clean room source policy, with consultation of public open-source references | Accepted |
| [0027](0027-private-member-camelcase.md) | Private members use ordinary camelCase, without a prefix | Accepted |
| [0028](0028-native-agent-test-mod.md) | Exercise generic entities and agents with an isolated native zombie test mod before Rook | Accepted |

## Template

```markdown
# ADR-NNNN — Title

- **Status:** Proposed | Accepted | Accepted, amended by ADR-XXXX | Superseded by ADR-XXXX | Rejected
- **Date:** YYYY-MM-DD

## Context
The problem, the forces and the constraints.

## Decision
What was decided, stated directly.

## Alternatives considered
Each alternative and why it was not chosen.

## Consequences
What becomes easier, what becomes harder, and the risks and how to mitigate them.
```
