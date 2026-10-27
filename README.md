# Modern-Client.v2

[![CI](https://github.com/BRUN0R2/modern-client.v2/actions/workflows/ci.yml/badge.svg)](https://github.com/BRUN0R2/modern-client.v2/actions/workflows/ci.yml)
[![REUSE](https://img.shields.io/badge/REUSE-3.3-green)](https://reuse.software)

An open-source recreation of Counter-Strike 1.6, and only Counter-Strike 1.6 (no Half-Life,
[ADR-0017](docs/adr/0017-counter-strike-only.md)), written from scratch in **C++26**, with
gameplay and servers identical to the original and **without the historical limits** of GoldSrc.

What the project builds, milestone by milestone ([roadmap](docs/19-roadmap.md); what exists
today is under [Status](#status)):

- **Modern engine:** archetype ECS and multithreaded Job System, a Vulkan renderer with GPU
  skinning (done); DirectX 11 later.
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

Milestones **M2 (Assets and visible world)** and **M3 (Local game)** in progress, on top of the
M0 foundation (build with C++ modules and `import std`, presets, vcpkg and CI) and the M1 runtime
core: `nx.core` (typed errors, contracts, strong types and units), `nx.meta` (reflection facade),
`nx.data` (TOML files with schemas), `nx.jobs` (*work-stealing* Job System), `nx.ecs` (archetype
ECS), `nx.math` (vectors, matrices, quaternions, GoldSrc angles) and `nx.console` (typed cvars,
commands and `.cfg`). Above them: `nx.platform` (clocks, the native file system, the window, its
input by position and layout, and the sound output), `nx.rhi` (the GPU interface, with its Vulkan backend), `nx.assets`
(the VFS, the Counter-Strike 1.6 installation and its detection in Steam, and fuzzed readers of
every binary GoldSrc format: BSP, WAD3, MDL, SPR, WAV, TGA, and of VGUI2's schemes and fonts),
`nx.render` (the BSP world with its lightmaps, sky, water and decals, studio models with GPU
skinning, sprites, the HUD's screen, and the enhanced look), `nx.ui` (fonts as Windows sizes
them, panels and the console's window), `nx.audio` (the mixer), `nx.settings` (the player's
`.cfg` files, cvars and binds), `nx.collision` (traces through the hulls of a map), `nx.game`
(the map's brush entities, doors, buttons and triggers) and `cs.shared` (player movement, the
knife, Glock 18, USP, AK-47 and M4A1 as the original fires them, the rounds of a map: their
freeze, their time, its end and the money they pay, and a player hurt by its falls, drowning, slime
and lava, and killed), with which the client, `next`, plays the maps alone, round after round.
`nxcheck` reads every file of a Counter-Strike 1.6 installation with those readers.
The architecture documentation is in [`docs/`](docs/README.md), the project rules in
[`docs/RULES.md`](docs/RULES.md) and the backlog in [`docs/TODO.md`](docs/TODO.md).

## Building

Requirements on Windows: Visual Studio 2026 with the "Desktop development with C++" workload
(it brings MSVC, Ninja, CMake and vcpkg), and the Vulkan SDK for the GPU tests' validation layers.
Details and the toolchain matrix are in
[docs/17-build-and-tools.md](docs/17-build-and-tools.md).

```powershell
. .\scripts\devShell.ps1
cmake --preset dev-windows-msvc
cmake --build --preset dev-windows-msvc
ctest --preset dev-windows-msvc
```

On Linux, use the `dev-linux-gcc` preset; the other presets and their status are in
[docs/17 §3](docs/17-build-and-tools.md#3-presets).

Release builds include the component benchmarks, including `nxBenchClient` for the client's hot
paths. Their synthetic workloads and comparison procedure are in
[docs/18](docs/18-testing-and-quality.md#performance).

With Counter-Strike 1.6 installed through Steam, `nxcheck` reads every file of the installation
with the engine's readers and reports any it cannot read
([docs/18](docs/18-testing-and-quality.md#real-content)), and `next` plays one of its maps as a
player with what its team spawns with (a knife and a pistol; `give ak47` gives a rifle): the mouse
looks, and the keys do what Counter-Strike binds them to (W A S D move, Space jumps, Ctrl ducks,
Shift walks, E uses, the arrows turn, the left button fires, the right one switches a silencer or
a burst, R reloads, Q draws the last weapon, the tilde opens the console); V flies through
everything (`noclip`), and Escape ends it. Its settings and binds are kept in `config.cfg`, in
`%APPDATA%\Modern-Client.v2`. Keyboard binds follow the layout by default (`in_keymap keycode`);
`in_keymap scancode` selects physical US positions instead. The mode can also be set on the command
line with `+in_keymap scancode`. `nxview` renders one frame of a map into a BMP file:

```powershell
.\build\dev-windows-msvc\bin\nxcheck.exe
.\build\dev-windows-msvc\bin\next.exe +map de_dust2
.\build\dev-windows-msvc\bin\nxview.exe --map cs_italy --screenshot cs_italy.bmp
```

For local material comparisons, `nxview --look enhanced --user-folder <folder>` enables
the enhanced renderer and that folder's `addons/cstrike/`; `--weapon <id>` selects a
weapon from the data catalog. Reflection probes finish baking before the capture.
A model's materials are in the file beside it, `<model>.material.toml`, texture by texture: its
surface, metallic-roughness or painted with the light painted for it, what it emits and how it is
drawn ([material file](docs/05-rendering.md#92-mdl-format-features-to-support)).

## License

| Part | License |
|------|---------|
| Engine, game, server, tools and data | [GPL-3.0-or-later](LICENSES/GPL-3.0-or-later.txt) with the [Rook Plugin Exception](LICENSES/LicenseRef-Rook-Plugin-Exception.txt) |
| Rook toolchain and plugin SDK (`rook/`) | [MIT](LICENSES/MIT.txt) |
| Documentation (`docs/`, `README.md`, `docs/TODO.md`, `CONTRIBUTING.md`, `SECURITY.md`) | [CC BY 4.0](LICENSES/CC-BY-4.0.txt) |

**Plugins and mods** that use only the plugin interface may adopt **any license**,
including proprietary ones. This is guaranteed by the exception, not just by intent.

Each file's license is in its SPDX header and in [`REUSE.toml`](REUSE.toml)
([REUSE](https://reuse.software)). Decision and rationale: [ADR-0010](docs/adr/0010-license.md).
Contributions use *Signed-off-by* (DCO); see [CONTRIBUTING.md](CONTRIBUTING.md) and, for
vulnerabilities, [SECURITY.md](SECURITY.md).
