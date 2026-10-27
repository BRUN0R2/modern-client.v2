# ADR-0013 — camelCase convention across the project (C++ and Rook)

- **Status:** Accepted, amended by [ADR-0021](0021-rook-amxx-names.md) and [ADR-0027](0027-private-member-camelcase.md)
- **Date:** 2026-10-20

## Context

The project's C++ already used camelCase for functions and variables, but the first version
of the Rook specification followed the `snake_case` of the AMXX natives (`get_weapon_var`,
`plugin_precache`). Two conventions in the same project make reading, tooling and
documentation harder. The maintainer decided to use camelCase across the whole project.

## Decision

- Functions, methods, variables, parameters and fields use **camelCase** in C++ and in Rook,
  including the plugin API and the keys of the `vars` catalogs: `getWeaponVar`,
  `setEntityVar`, `precacheModel`, `pluginPrecache`, `"maxClip"`.
- **Files and directories** too: `contractsTests.cpp`, `enumArray.cppm`, `rhiVulkan/`,
  `devShell.ps1`, `infectionTests.rook`. The same goes for the project's build targets,
  executables and CMake functions (`nxCoreTests`, `rookLsp`, `nxAddTest`).
- Types and enum values stay in PascalCase. Constants follow the idiom of each language
  (`kPascalCase` in C++, `SCREAMING_SNAKE_CASE` in Rook).
- Explicit exceptions: console cvars and commands (`mp_roundtime`), map classnames
  (`func_buyzone`), names required by tools (`CMakeLists.txt`, vcpkg triplets), conventional
  repository files, the documents in `docs/`, files of the original game, translation keys
  and names imposed by third parties
  ([02 §3.1](../02-coding-standards.md#31-project-rule-camelcase)); in Rook, the names of AMX Mod X's
  API in the `amxx` compatibility layer and the underscored accessors the compiler generates beside
  the canonical ones (`get_player_var`), and the plugins that use them ([ADR-0021](0021-rook-amxx-names.md)). The
  project's own code stays canonical.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| `snake_case` in Rook, camelCase in C++ | Two conventions in the same project |
| `snake_case` everywhere | Goes against the maintainer's choice and the C++ already written |
| Rename cvars to camelCase | Would break existing `server.cfg` files, binds and tools (server parity) |

## Consequences

- ✅ A single convention, checked by `clang-tidy` and by the `naming` lint of `rookc`.
- ✅ The migration stays mechanical: `set_props_var` → `setPropsVar`, done by `rook migrate`.
- ⚠️ Migrated plugins change spelling relative to AMXX. The equivalence table in
  [16](../16-amxx-migration.md) covers the names.
