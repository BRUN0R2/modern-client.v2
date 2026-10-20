# Modern-Client.v2

[![CI](https://github.com/BRUN0R2/modern-client.v2/actions/workflows/ci.yml/badge.svg)](https://github.com/BRUN0R2/modern-client.v2/actions/workflows/ci.yml)
[![REUSE](https://img.shields.io/badge/REUSE-3.3-green)](https://reuse.software)

An open-source recreation of Counter-Strike 1.6, and only Counter-Strike 1.6 (no Half-Life,
[ADR-0017](docs/adr/0017-counter-strike-only.md)), written from scratch in **C++26**, with
gameplay and servers identical to the original and **without the historical limits** of GoldSrc.

What the project builds, milestone by milestone ([roadmap](docs/19-roadmap.md); what exists
today is under [Status](#status)):

- **Modern engine:** archetype ECS and multithreaded Job System (done); Vulkan renderer with
  GPU skinning (M2), DirectX 11 later.
- **No limits:** entities, precache, players (up to 1024), messages, attachments,
  body groups, sprites and HUD values.
- **Native plugins and mods:** the **Rook** language, strongly typed and familiar to anyone
  coming from AMX Mod X, with its own compiler, hot reload and LSP (M6).
- **Servers as always:** the same cvars, configs, commands, RCON and A2S queries.

> Will work without any Valve asset. Anyone who owns a legitimate copy of Counter-Strike 1.6
> (Steam) will be able to use the original assets: the game will detect the installation and
> configure the `classic` profile locally (M2,
> [ADR-0015](docs/adr/0015-free-content-and-optional-original-assets.md)). No Valve asset is
> distributed. This project is not affiliated with Valve Corporation.

## Status

Milestone **M2 (Assets and visible world)** in progress, on top of the M0 foundation (build
with C++ modules and `import std`, presets, vcpkg and CI) and the M1 runtime core: `nx.core`
(typed errors, contracts, strong types and units), `nx.meta` (reflection facade), `nx.data`
(TOML files with schemas), `nx.jobs` (*work-stealing* Job System), `nx.ecs` (archetype ECS),
`nx.math` (vectors, matrices, quaternions, GoldSrc angles), `nx.console` (typed cvars, commands
and `.cfg`), `nx.platform` (clocks, the native file system, the window and its input) and
`nx.assets` (the VFS, the Counter-Strike 1.6 installation and its detection in Steam, and
fuzzed readers of every binary GoldSrc format: BSP, WAD3, MDL, SPR, WAV), with tests, and
`nxcheck`, which reads every file of a Counter-Strike 1.6 installation with those readers. The
architecture documentation is in [`docs/`](docs/README.md), the project rules in
[`docs/RULES.md`](docs/RULES.md) and the backlog in [`TODO.md`](TODO.md).

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

On Linux, use the `dev-linux-gcc` preset; the other presets and their status are in
[docs/17 §3](docs/17-build-and-tools.md#3-presets).

With Counter-Strike 1.6 installed through Steam, `nxcheck` reads every file of the installation
with the engine's readers and reports any it cannot read
([docs/18](docs/18-testing-and-quality.md#real-content)):

```powershell
.\build\dev-windows-msvc\bin\nxcheck.exe
```

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
