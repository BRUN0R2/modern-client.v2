# Modern-Client.v2

[![CI](https://github.com/BRUN0R2/modern-client.v2/actions/workflows/ci.yml/badge.svg)](https://github.com/BRUN0R2/modern-client.v2/actions/workflows/ci.yml)
[![REUSE](https://img.shields.io/badge/REUSE-3.3-green)](https://reuse.software)

An open-source recreation of Counter-Strike 1.6, written from scratch in **C++26**, with
gameplay and servers identical to the original and **without the historical limits** of GoldSrc.

- **Modern engine:** archetype ECS, multithreaded Job System, Vulkan (DirectX 11 in the
  future) and GPU skinning.
- **No limits:** entities, precache, players (up to 1024), messages, attachments,
  body groups, sprites and HUD values.
- **Native plugins and mods:** the **Rook** language, strongly typed and familiar to anyone
  coming from AMX Mod X, with its own compiler, hot reload and LSP.
- **Servers as always:** the same cvars, configs, commands, RCON and A2S queries.

> Works without any Valve asset. Anyone who owns a legitimate copy of Counter-Strike 1.6
> (Steam) can use the original assets: the game detects the installation and configures the
> `classic` profile locally ([ADR-0015](docs/adr/0015-free-content-and-optional-original-assets.md)).
> No Valve asset is distributed. This project is not affiliated with Valve Corporation.

## Status

Milestone **M1 (Runtime core)** in progress, on top of the M0 foundation (build with C++
modules and `import std`, presets, vcpkg and CI): `nx.core` (typed errors, contracts, strong
types and units), `nx.meta` (reflection facade), `nx.jobs` (*work-stealing* Job System), `nx.ecs` (archetype ECS),
`nx.math` (vectors, matrices, quaternions, GoldSrc angles), `nx.console` (typed cvars,
commands and `.cfg`) and `nx.platform` (clocks), with tests. The architecture documentation
is in [`docs/`](docs/README.md), the project rules in [`docs/RULES.md`](docs/RULES.md) and
the backlog in [`TODO.md`](TODO.md).

## Building

Requirements on Windows: Visual Studio 2026 with the "Desktop development with C++" workload
(it brings MSVC, Ninja, CMake and vcpkg). Details and the toolchain matrix are in
[docs/17-build-and-tools.md](docs/17-build-and-tools.md).

```powershell
. .\scripts\devShell.ps1
cmake --preset dev-windows-msvc
cmake --build --preset dev-windows-msvc
ctest --preset dev-windows-msvc
```

On Linux, use the `dev-linux-gcc` or `dev-linux-clang` presets.

## License

| Part | License |
|------|---------|
| Engine, game, server, tools and data | [GPL-3.0-or-later](LICENSES/GPL-3.0-or-later.txt) with the [Rook Plugin Exception](LICENSES/LicenseRef-Rook-Plugin-Exception.txt) |
| Rook toolchain and plugin SDK (`rook/`) | [MIT](LICENSES/MIT.txt) |
| Documentation (`docs/`, `README.md`, `TODO.md`, `CONTRIBUTING.md`, `SECURITY.md`) | [CC BY 4.0](LICENSES/CC-BY-4.0.txt) |

**Plugins and mods** that use only the plugin interface may adopt **any license**,
including proprietary ones. This is guaranteed by the exception, not just by intent.

Each file's license is in its SPDX header and in [`REUSE.toml`](REUSE.toml)
([REUSE](https://reuse.software)). Decision and rationale: [ADR-0010](docs/adr/0010-license.md).
Contributions use *Signed-off-by* (DCO); see [CONTRIBUTING.md](CONTRIBUTING.md) and, for
vulnerabilities, [SECURITY.md](SECURITY.md).
