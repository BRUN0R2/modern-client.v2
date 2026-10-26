# TODO — Modern-Client.v2

Project backlog, by milestone ([docs/19-roadmap.md](docs/19-roadmap.md)).
`[x]` = done according to the [definition of done](docs/18-testing-and-quality.md#definition-of-done). `[ ]` = pending.

---

## Pending decisions

- [x] Project name: **Modern-Client.v2**
- [x] Player limit: 1 to 1024, default 32 ([ADR-0011](docs/adr/0011-player-limit-1024.md))
- [x] Typed `get*Var`/`set*Var` pattern for plugins ([ADR-0012](docs/adr/0012-typed-get-set-vars-pattern.md))
- [x] camelCase convention across the whole project, C++ and Rook ([ADR-0013](docs/adr/0013-camelcase-convention.md))
- [x] Files, directories, build targets and scripts also in camelCase (`contractsTests.cpp`)
- [x] C++ test framework: GoogleTest (official support for builds without exceptions)
- [x] Project license: option A, GPL-3.0-or-later + MIT (Rook and SDK) + CC BY 4.0 (docs) ([ADR-0010](docs/adr/0010-license.md))
- [x] Public repository: https://github.com/BRUN0R2/modern-client.v2
- [ ] Final name of the plugin language (provisional: **Rook**)
- [ ] The plugin API as eight generic primitives (handles and `vars`, standalone types with no parent, methods and hooks, actions, queries, messages and effects, commands/cvars/tasks, services) in place of the old modules ([ADR-0022](docs/adr/0022-plugin-api-primitives.md), proposed)
- [x] Navigation meshes for every kind of agent, `nx.navigation` over Recast/Detour ([ADR-0023](docs/adr/0023-navigation.md))
- [x] Agents for bots, monsters and bosses: one brain, a player's or an entity's body, perception, memory and behaviors, `nx.agents`, each kind its own module over it: `cs.bots`, a mod's zombies and bosses in Rook ([ADR-0024](docs/adr/0024-agents.md))
- [x] Rook accepting AMX Mod X's literal names (`#include`, `new`, `register_clcmd`, `client_print`; a property's native such as `get_user_health` being its accessor, `get_player_var(player, "health")`) and the accessors with underscores (`get_player_var`, `set_weapon_var`, `get_entity_var`) through an `amxx` compatibility layer, an exception to ADR-0013 ([ADR-0021](docs/adr/0021-rook-amxx-names.md))
- [ ] The enhanced look: physically based materials, the map's reflection probes, HDR and bloom, the same on every card ([ADR-0025](docs/adr/0025-enhanced-rendering.md), proposed)
- [x] Menu toolkit: in-house, in VGUI2's model ([ADR-0020](docs/adr/0020-in-house-ui-toolkit.md))
- [x] `nx.ui` panels, placed by VGUI2's layouts and following their parent as it changes size
- [x] `nx.ui` fonts: TrueType faces sized for a scheme's `tall` as Windows' GDI sizes them and drawn by FreeType, with and without antialiasing, fuzzed (`fuzzFont`; a device height whose line is less than half its em left out, so that a broken VDMX table cannot size a face beyond its cell; [12 §2.1](docs/12-ui-hud-and-console.md#21-fonts))
- [x] `nx.ui` text: glyph atlases drawn by `nx.render:screen`, lines laid out as GDI's `TextOut` lays them out ([12 §2.1](docs/12-ui-hud-and-console.md#21-fonts)); UTF-8 in `nx.core`
- [x] `nx.ui` font catalog: the system's fonts and a scheme's own, the face of a family by weight and italics as GDI chooses it, bold simulated as GDI simulates it ([12 §2.1](docs/12-ui-hud-and-console.md#21-fonts))
- [x] `nx.ui` painting: panels' backgrounds and their children within them; labels placed by their alignment
- [x] `nx.ui` text entries: typed text at the cursor, the keys that edit it, scrolled so that the cursor shows
- [x] `nx.ui` rich text: coloured lines wrapped to the width, the oldest dropped past a capacity, scrolled from the newest
- [x] `nx.ui` console window over `nx.console`: lines run and shown, names offered and completed, history, output coloured by kind and log level
- [x] The console window in `next` (`toggleconsole`, `~`), its font the installation's scheme's, what is logged shown in it; `nxview --console`
- [ ] The console window in the original's look and place, from its scheme and reference shots; search in it
- [x] `nx.ui` scheme fonts: a scheme's font made for a screen's height, its candidates in its order, each for the characters left by those before it
- [ ] Fonts: Windows' bitmap fonts (`.fon`); simulated italics; atlases that grow as new characters come
- [ ] Define the reference hardware for the performance targets
- [x] Free content by default; original assets optional and configured locally ([ADR-0015](docs/adr/0015-free-content-and-optional-original-assets.md))
- [x] Rook Plugin Exception: additional permission under section 7 of the GPL for plugins and mods ([ADR-0010](docs/adr/0010-license.md))
- [x] Counter-Strike 1.6 only: no Half-Life, no other GoldSrc games, no game-module abstraction ([ADR-0017](docs/adr/0017-counter-strike-only.md))
- [x] SDL3 (window, events, input, raw mouse) arrives in M2, together with the Vulkan swapchain, its first consumer. In M1 nothing uses a window or input, and the vcpkg `sdl3` port pulls X11, Wayland and D-Bus into the CI Linux with nothing to test (2026-10-20)
- [ ] Legal review before the first public release, including the text of the Rook Plugin Exception ([20](docs/20-legal-and-licensing.md))

---

## Documentation

- [x] Project rules (`docs/RULES.md`): priorities, principles and forbidden patterns
- [x] Documentation in English ([ADR-0016](docs/adr/0016-english-documentation.md))
- [x] Documentation index (`docs/README.md`)
- [x] 00 — Overview and removed limits
- [x] 01 — Layered architecture, threads and frame cycle
- [x] 02 — C++26 coding standards (typed errors, no magic numbers)
- [x] 03 — Archetype ECS
- [x] 04 — Job System
- [x] 05 — Rendering (RHI, Vulkan, future DirectX 11, GPU skinning)
- [x] 06 — Assets, the end of precache and removed content limits
- [x] 07 — Simulation, movement and physics
- [x] 08 — Networking
- [x] 09 — Dedicated server
- [x] 10 — Counter-Strike gameplay
- [x] 11 — Audio
- [x] 12 — UI, HUD (no value truncation) and console
- [x] 13 — Plugin and mod system
- [x] 14 — Rook language
- [x] 15 — Plugin API
- [x] 16 — AMXX → Rook migration (ReZombie examples)
- [x] 17 — Build, dependencies and tools
- [x] 18 — Testing and quality
- [x] 19 — Roadmap
- [x] 20 — Legal and licensing
- [x] 21 — Launcher and automatic updates (planned for M4)
- [x] Glossary
- [x] ADRs 0001–0019
- [x] camelCase rule documented ([02 §3.1](docs/02-coding-standards.md#31-project-rule-camelcase))
- [x] Licenses in the REUSE format: `LICENSES/`, `LICENSE`, `REUSE.toml` and SPDX headers
- [x] `CONTRIBUTING.md` (PR flow, DCO, standards, license of contributions, how to run the tests)
- [x] `SECURITY.md` (how to report vulnerabilities)
- [ ] Complete specification of the Rook grammar (`rook/spec/grammar.ebnf`)
- [ ] Complete cvar catalog in `data/cstrike/cvars.toml`
- [ ] Survey of the original's economy and weapon values for the official data

---

## M0 — Foundation

### Repository and build
- [x] Initialize the git repository (`main`), `.gitignore`, `.gitattributes`, `.editorconfig`
- [x] Folder structure ([17 §1](docs/17-build-and-tools.md#1-repository-layout))
- [x] Root `CMakeLists.txt` with C++ modules and `import std`
- [x] `std` module built by the project (`nx::std`) from `modules.json`, without experimental CMake features ([ADR-0014](docs/adr/0014-std-module-built-by-the-project.md))
- [x] CMake helpers: `nxAddModuleLibrary`, `nxAddTest`, per-target warning policy
- [x] `CMakePresets.json`: dev/release MSVC, dev/release GCC, dev Clang, ASan + UBSan, TSan
- [x] `tools` presets (`tools-windows-msvc`, `tools-linux-gcc`): `nxcheck` and `nxview` alone, in Release
- [ ] `server-headless` preset (once `nextds` exists)
- [ ] Windows presets with GCC (MinGW-w64) and Clang
- [x] `vcpkg.json` with a pinned baseline and a `tests` feature
- [x] `x64-windows-nx` and `x64-linux-nx` triplets (dependencies without exceptions and without RTTI)
- [x] `.clang-format` and `.clang-tidy` with `readability-magic-numbers` and naming as errors
- [x] Warnings as errors, deterministic floating point, no exceptions and no RTTI
- [x] Functions defined in module interfaces are `inline`, so importers can inline them ([02 §2](docs/02-coding-standards.md#2-file-and-module-organization))
- [x] `scripts/devShell.ps1` for command-line builds on Windows
- [ ] Run `clang-tidy` with modules (requires a Clang build and `compile_commands.json`)
- [x] Recommended editor settings in `.vscode/`
- [x] Remove the dependency on the experimental `import std` and the `@cmake_cxx_std.lib` workaround
- [x] Normalize the tree's line endings according to `.gitattributes`

### Toolchain
- [x] Validate MSVC 14.51 (VS 2026) with CMake 4.3.1 and 4.4.4, Debug and Release
- [x] Validate GCC 16 on Linux (GCC 16.2.0 in CI)
- [ ] Validate Clang on Linux
- [ ] Validate GCC (MinGW-w64) and Clang on Windows
- [x] Document MSVC's native and fallback features ([17 §2.1](docs/17-build-and-tools.md#21-compiler-matrix))

### CI
- [x] `.github/workflows/ci.yml` workflow (push to `main`, pull requests, on demand)
- [x] `windows-msvc` job: Debug and Release with build and tests
- [x] Required `linux-gcc` job (GCC 16)
- [ ] Linux job with Clang
- [x] Formatting (`clang-format --dry-run --Werror`) in the `checks` job
- [ ] `clang-tidy` in CI (requires a Clang build and modules)
- [x] Dependency graph check: every module declares its layer and CMake refuses a dependency on the same or a higher layer once every module exists, in every build ([01 §1.1](docs/01-architecture.md#11-module-catalog)), with `LayerCheck.*` regression tests
- [x] ThreadSanitizer on all tests (`tsan-linux-gcc` preset in the `linux-gcc` job)
- [ ] ASan + UBSan jobs (with Clang)
- [x] Line endings (`git ls-files --eol`) in the `checks` job
- [x] `reuse lint` in the `checks` job

### `nx.core`
- [x] `nx::Failure<E>`, `nx::Result<T, E>`, `nx::fail`, chained cause, `nx::describe` and `std::formatter`
- [x] Contracts `nx::expects`, `nx::ensures`, `nx::invariant` with an *enforce*/*observe* handler
- [x] Contract handler: stack trace and `breakpoint_if_debugging` (`nx.core:debugging`)
- [ ] Crash report (minidump and the last log lines) when the contract handler or a crash aborts, in `nx.platform`, with the first application
- [x] Introspection: `typeName`, `shortTypeName`, `enumName`, `enumCount`, `enumerators` (signature-based fallback)
- [x] Text of diagnostics shared by the modules that read values: `toLowerAscii`, `boundsText`
- [ ] `typeName` and `enumName` through the native path (P2996) once every supported compiler offers it (GCC 16 does and `nx.meta` already uses it; MSVC does not)
- [x] `nx::Quantity` and units (`HammerUnits`, `UnitsPerSecond`, `HitPoints`, `ArmorPoints`, `Damage`, `Money`, `Scale`, angles, time) with literals
- [x] `nx::narrow`, `nx::tryNarrow`, `nx::truncate`, `nx::tryRound`, checked and saturating arithmetic
- [x] `EnumArray` and `Flags<E>`
- [ ] `inplace_vector` (facade), when the first consumer exists (ECS or jobs)
- [ ] `function_ref` (own implementation of `std::function_ref`), when the first consumer exists
- [ ] `OptionalRef<T>` (facade for `std::optional<T&>`), when the first consumer exists
- [x] `FrameArena` with a typed failure (`Exhausted`); a bad alignment is a contract violation
- [ ] Generic `PoolAllocator<T>` (the ECS chunks use the `World`'s own block pool) and `TrackedHeapAllocator`
- [x] Lock-free SPSC queue with a typed queue-full error (`nx.jobs:spscQueue`), first used by the mixer's commands
- [ ] Lock-free MPSC queue, when the first consumer exists (networking)
- [x] Structured logging: levels, per-module categories, compile-time checked formatting, `ConsoleSink`, a *thread-safe* `Logger` and `RateLimit` (tested under ThreadSanitizer)
- [ ] File and JSON lines sinks
- [x] `nx::hash64` (`constexpr` XXH64, verified against the reference implementation) and `nx::StableId`
- [ ] BLAKE3 for content hashing, with its first consumer (`.nxpak` packages, resource downloads or file consistency)
- [x] Unit tests for what already exists in `nx.core` (including death tests of the default handler)

### `nx.meta`
- [x] Reflection facade: names, fields and annotations (`nx::meta::qualifiedName`, `forEachField`, `typeAnnotation`); `enumName` stays in `nx.core:introspection`
- [x] Native path (P2996 + P3394): GCC 16 with `-freflection` checks every description against its type, including annotations written on it (`NX_REFLECTION_BACKEND`)
- [x] Fallback path (explicit registration: `nx::meta::Description<T>`)
- [x] Test that guarantees identical metadata on both paths (`nxMetaTests`: matching descriptions and every kind of drift)
- [ ] Annotations on the types themselves (`[[=...]]`) instead of the descriptions, when every supported compiler accepts the syntax (MSVC does not)

### `nx.data`
- [x] TOML schemas derived from described structs (toml++ without exceptions), with typed failures that name the file, line, column and key path
- [x] `Range` annotations checked when the description compiles: only on numbers, minimum <= maximum, exact whole-number bounds
- [x] A field set and written by its key path (`nx.data:fields`: `assignField`, `formatField`, `listFields`), read as a file's value is, the struct as it was on a failure

---

## M1 — Runtime core

### `nx.jobs`
- [x] Workers and Chase-Lev deques (one per priority in each worker)
- [ ] CPU affinity for the workers, if a benchmark shows a gain (OS thread control in `nx.jobs` or `nx.core`, which `nx.jobs` may use; [04 §2](docs/04-job-system.md#2-model))
- [x] Global queue per priority (`Critical`, `Normal`, `Low`, `Background`), producers never wait
- [x] Counters with generation, dependencies and continuations
- [x] Lock-free pools for descriptors, counters and continuations
- [x] `wait` with *help-while-waiting*
- [x] `parallelFor` with automatic and fixed granularity
- [x] `JobSystem::stats()` and serial mode (`workerCount = 0`)
- [ ] `jobs_workers` cvar, `jobs_stats` and `jobs_serial` commands (with the console)
- [ ] Tracy zones (with the Tracy dependency)
- [ ] `std::execution`-compatible scheduler (stdexec), with asset streaming (M2)
- [ ] Per-job scratch arena in `JobContext`, when the first consumer needs it
- [x] Tests, including an identical result with 0, 1, 3 and 15 workers, under ThreadSanitizer in CI
- [x] Benchmarks (`nxBenchJobs`: empty jobs, dependency chains and `parallelFor` with 0, 1, 3 and all workers)

### `nx.ecs`
- [x] Generational `Entity`, FIFO slots, retirement on generation exhaustion
- [x] Explicit component registration with a declared name (`ComponentTypeId`, `StableTypeId`), also for runtime components
- [ ] Component registration through reflection (with `nx.meta`)
- [x] Archetypes, 16 KiB SoA chunks (larger blocks for larger rows), archetype graph
- [x] Change versions per chunk and column
- [x] Typed queries (`With`, `Without`, `Changed`, `Optional`, `Entity`)
- [ ] `Added<T>` with a per-row version, when the first consumer exists
- [x] `forEach`, `forEachChunk`, `parallelForEachChunk`, `get`
- [x] Phase scheduler with a conflict DAG, `before`/`after`, cycle detection
- [x] Command buffers with deterministic application and `PendingEntity`
- [ ] Per-chunk command buffers and event writers inside `parallelForEachChunk`
- [x] *Double-buffered* events
- [x] Resources (singletons)
- [ ] Relations (parent/child, owner) with cleanup at the *sync point* (with gameplay, M3)
- [x] World determinism test with 0, 1, 3 and 15 workers
- [ ] `ent_list`, `ent_dump`, `ecs_stats`
- [x] Benchmarks with the targets of [03 §16](docs/03-ecs.md#16-performance-targets-reference-hardware-to-be-defined-in-m1) (`nxBenchEcs`)
- [x] Command buffers within the target of [03 §16](docs/03-ecs.md#16-performance-targets-reference-hardware-to-be-defined-in-m1): runs of spawns and destroys applied as batches

### Platform, math and console
- [x] `nx.platform`: clocks (monotonic, wall, `Stopwatch`) and a precise deadline wait for tick and frame pacing (`SleepTimer`)
- [x] `fps_max` and `fps_override` pace the client's frames as the original's `Host_FilterTime` does (`nx::settings::frameIntervalOf`, [05 §12](docs/05-rendering.md#12-latency-and-frame-pacing))
- [ ] OS threads in `nx.platform` (name, priority, affinity), with the first consumer (audio thread, Tracy or affinity benchmark)
- [x] `nx.math`: vectors, matrices (`Mat3`, `Mat3x4`, `Mat4`), quaternions, GoldSrc angles (`angleVectors`, `vectorAngles`), AABBs and planes, with deterministic floating point ([07 §6](docs/07-simulation-and-physics.md#6-determinism))
- [ ] SIMD in `nx.math`, when a benchmark shows a gain
- [x] Typed cvar system (flags `Archive`, `Notify`, `Cheat`, `ServerProtected`) in `nx.console` ([12 §5.3](docs/12-ui-hud-and-console.md#53-the-nxconsole-module))
- [x] Command system (aliases, `+`/`-`, `wait`, `exec`, `;`, quotes, `echo`, `toggle`, `incrementvar`)
- [x] `.cfg` parser compatible with the original
- [x] `cvarlist`, `cmdlist` and `find` commands ([09 §8](docs/09-dedicated-server.md#8-server-console-commands))
- [x] Log levels per cvar (`log_level_<category>`, [02 §10](docs/02-coding-standards.md#10-logging))
- [x] The player's settings (`nx.settings`): the user's folder (`nx::platform::userDirectory`), `config.cfg`, `userconfig.cfg` and `autoexec.cfg` run at startup and `config.cfg` written at exit, whole or not at all; the mouse's and the video's cvars with the original's names and values, read by `next`
- [x] Binds (`nx.settings`): `bind`, `unbind`, `unbindall` and `bindlist` by the original's key names and numbers, Counter-Strike 1.6's binds, written to `config.cfg`, and the keys, mouse buttons and wheel running them as the original does; the player's `+` buttons making the user command (`cs.client`: `CL_CreateMove`'s shares and speeds, `CL_AdjustAngles`'s turning), read by `next`
- [ ] `in_keymap`: binds by the layout's characters; `+attack`, `+reload` and the other buttons with what reads them
- [x] Determinism test with 0, 1, 3 and 15 workers (`nxIntegrationTests`: `nx.math` in parallel ECS systems, world *hash* every tick)

---

## M2 — Assets and visible world

### Assets
- [x] VFS with mounts, priority, case-insensitive lookup and path validation (`nx.assets`), over the native file system of `nx.platform`
- [ ] Content sources (`engine`, `devContent`, `openContent`, `goldsrcInstall`, mods) with a redistribution flag
- [x] Reading of `content.toml` (order: `--content-config`, working directory, user profile): the `classic` profile and its `goldsrcInstall.path` (`nx::assets::readContentConfig`, `findContentConfig`)
- [ ] `open`, `dev` and `custom` profiles, with their content
- [x] Steam installation detection (registry and `libraryfolders.vdf` on Windows; `~/.steam`, `~/.local/share/Steam` and Flatpak on Linux): `nx::platform::steamFolders`, `nx::assets::detectGoldsrcInstall`
- [x] Validation against `data/content/goldsrcCstrike.toml` with `ContentError::MissingEssentialFiles` (`GoldsrcInstall::open`)
- [ ] First-start wizard ("use the original assets?") that writes `content.toml`
- [x] `-basedir` and `--content-config` options
- [ ] `--content <profile>`, with the profiles
- [ ] `content_list`, `content_scan`, `content_add`, `content_profile` commands
- [ ] Code-generated `devContent`: grid textures, test map, capsule player, simple weapons, synthesized sounds
- [x] BSP v30 reader (all lumps) with fuzzing (`nx.assets:bsp`, `fuzzBsp`): every index checked, the visible tree checked to be a tree, the collision hulls to have no cycle (clip nodes shared as VHLT writes them)
- [x] WAD3 reader with fuzzing (`nx.assets:wad`, `fuzzWad`; fuzz infrastructure: `nxAddFuzzer`, the `fuzz-windows-msvc` preset and the nightly `fuzz` workflow): mip textures, pictures (`CONBACK`) and console fonts (`CONCHARS`, `fonts.wad`)
- [x] The HUD's sprite lists (`hud.txt`, `sprites/weapon_*.txt`: `nx.assets:spriteList`), read by `nxcheck`
- [x] VGUI2's KeyValues: schemes, layouts and localized strings in UTF-16 (`nx.assets:keyValues`), read by `nxcheck` and fuzzed
- [x] VGUI2's schemes typed: colors, settings, fonts and borders (`nx.assets:scheme`), read by `nxcheck`
- [x] VGUI2's localized strings: the text of each token, found as layouts name them (`nx.assets:localization`), read by `nxcheck`
- [x] MDL v10 reader (including `*T.mdl` and `*01.mdl`) with fuzzing (`nx.assets:mdl`, `fuzzMdl`): structure, geometry, textures, skins, sequences and animations decoded from the model or its sequence groups
- [x] SPR v2 reader with fuzzing (`nx.assets:spr`, `fuzzSpr`)
- [x] Counter-Strike Online's SPR v3, frames of BC3 (DXT5) blocks, decoded (`nx.assets:spr`, `nx.assets:blockCompression`, `fuzzSpr`)
- [x] WAV reader (PCM 8/16, cue points) with fuzzing (`nx.assets:wav`, `fuzzWav`), and every other uncompressed layout (24/32-bit PCM, 32/64-bit float, extensible) at any rate and channel count
- [x] TGA reader with fuzzing (`nx.assets:tga`, `fuzzTga`): color-mapped, true-color and grayscale, raw or run-length encoded, from any corner, to RGBA
- [x] `nxcheck`: every file of the Counter-Strike 1.6 installation through the VFS and the readers, joins included (`*T.mdl`, sequence groups), on the Job System ([18](docs/18-testing-and-quality.md#real-content))
- [ ] Internal representation without fixed caps (attachments, body parts, bones, controllers, blends) ([06 §3.4](docs/06-assets-and-resources.md#34-removed-content-limits))
- [ ] Generational `AssetHandle<T>`, states and typed errors
- [ ] Asynchronous pipeline (I/O → decoding → dependencies → upload)
- [ ] Per-category budgets with LRU eviction
- [ ] Asset hot reload in development
- [ ] `nxbake`: content-addressed cache

### RHI and Vulkan
- [x] `nx.platform`: SDL3 window, events and input (vcpkg `sdl3` port), keys by physical position, raw mouse, and the native handles the swapchain is made from ([decision](#pending-decisions))
- [x] `nx.rhi` interface: device and frames, command list, swapchain, generational texture handles, transitions, render passes
- [x] `nx.rhi`: buffers, textures (and texture arrays), samplers, shaders, bind groups, render pipelines, copies and draws, with deferred destruction
- [ ] `nx.rhi`: queries, compute pipelines, dynamic uniform offsets
- [x] Vulkan 1.3 backend with volk: instance and adapter choice, device, frames in flight on a timeline semaphore, validation counted
- [x] VMA in the Vulkan backend (with buffers and textures)
- [x] Swapchain, present modes and recreation (resize, minimized, out of date)
- [ ] *Device lost*: recreate the device and its resources once, then a crash report
- [ ] Parallel recording (one command pool per worker per frame)
- [ ] Persistent staging ring
- [x] On-disk pipeline cache (`Device::pipelineCache`, `vulkan::DeviceDesc::pipelineCache`, `nx::platform::cacheDirectory`; [05 §4](docs/05-rendering.md#4-vulkan-backend))
- [x] Slang shaders compiled to SPIR-V in the build (`nxAddShaders`, `bin/shaders/`)
- [ ] Slang layout reflection checked against the C++ bind group layouts, and shader hot reload
- [ ] Built-in fallback shaders with `#embed`
- [ ] RHI conformance tests (today: offscreen draws read back, on lavapipe in CI)

### Rendering
- [x] View and projection matrices with the renderer's *clip space* and depth conventions (`nx.render:camera`: reversed infinite depth, `fov` as the original's 4:3 angle, Hor+) ([05](docs/05-rendering.md))
- [ ] Render graph (barriers, aliasing, pass culling, parallel recording)
- [x] BSP world (`nx.render:world`): every face of the world and of the brush entities in one vertex and index buffer, batched by texture; textures of the map and of its WADs with their mips; triggers and tool textures not drawn
- [x] BSP world: PVS and frustum culling (`BspView::leafAt` and `potentiallyVisible`; leaves against the view's planes, faces facing the camera, as the original culls them; indices gathered each frame)
- [x] Lightmap atlas (a layer for each of a face's light styles, extents computed as the compilers compute them)
- [x] Lightstyles combined in the shader (`nx.render:lightStyle`: the original's integer light per luxel, then blended; patterns stepped ten letters a second)
- [x] Masked transparency (`{` textures)
- [x] Sky: the faces of `sky` show the sides of `gfx/env/<skyname>*.tga` as the original projects them (texture arrays in `nx.rhi`)
- [x] Animated textures (`+0`..`+9`, `+a`..`+j`: every face of a sequence on the same frame, ten a second)
- [x] Water (turbulent faces: cut along the 64-unit grid and warped at each corner as the original warps them, unlit)
- [x] The waves of `func_water` (`nx::game::Waves`, an eighth of its `WaveHeight`; `ModelPlacement::waveHeight` raises the corners of its turbulent faces as `EmitWaterPolys` does)
- [x] The alternate animations of brush entities (`+a`..`+j` for `+0`..`+9`, and back, when their frame is not 0: a button in, a `func_wall` used)
- [x] Brush entities with render modes (`rendermode`, `renderamt`, `rendercolor`: solid, color, texture, glow, additive, blended back to front as the original sorts them)
- [x] Brush entities drawn where the game puts them each frame (`WorldFrame::placements`)
- [x] Brush entities turned by the game (`ModelPlacement::angles`), culled as the original culls them
- [ ] `renderfx` of brush entities
- [x] Decals on the world's faces (`nx.render:decals`: images of `decals.wad` as the original makes them, stamped near a point on the faces its tree reaches, cut to each, lit as the face, the original's pool and overlap rule; drawn after the world's faces, pulled toward the eye; depth bias in `nx.rhi`)
- [x] Decals on brush entities (`BspWorld::shootDecal` on a `DecalTarget`: in the model's space, carried with it, drawn as its render mode draws them, a glass face's from behind too)
- [x] Permanent decals: the maps' `infodecal`s (`nx::game::staticDecalsOf`, `DecalLife::Permanent`), stamped as the scene is made
- [ ] An `infodecal` with a `targetname`, stamped when it is used (`TriggerDecal`), with the game's decal events
- [x] `r_decals` and `mp_decals`: the pool's limit read at each decal, `r_decals` held to `mp_decals` as a game of more than one player is entered
- [ ] Decals on masked faces (stencil)
- [x] Emulation of gamma, brightness, texgamma and lightgamma (`nx.render:gamma`: palettes through `texgamma`, the light through the original's `lightgammatable`, no overbright)
- [x] Studio models: bones posed on the CPU as the original poses them (`cs::poseBones`: one or two blends, bone controllers, the mouth), every model of the installation posed by `nxcheck`
- [ ] Studio models: animation evaluated in jobs, and the transitions between sequences
- [x] GPU skinning (vertex shader) with per-vertex and per-normal bone indices (`nx.render:studio`: every bone in a storage buffer a frame, each model lit as the original lights its vertices, chrome, masked, additive, flat shaded and fullbright textures, skins and body parts)
- [x] The view's own model kept out of walls, and `nxview` holding the knife as Counter-Strike deploys it
- [x] Sprites (`nx.render:sprite`: every orientation and format, groups, the render modes, glows hidden, dimmed and grown, alpha-tested sprites lit by the world), and `nxview` showing the maps' `env_sprite`, `env_glow` and `cycler_sprite` as the game animates them
- [x] Blended sprites, brush entities and models sorted together, as the original's one list of blended entities (`nx.render:blendedOrder`: each renderer prepared, then the solid things of each and the blended things of all in one order)
- [x] The maps' studio models in `nxview`: `cycler_sprite` models at their framerate, hostages idling, their heads straight ahead, dropped to the floor (`nx.game:placement`)
- [x] The hostages as entities of the game's world (`cs.shared:hostages`, [10 §4.2](docs/10-counter-strike-gameplay.md#42-hostages)): an idle sequence drawn by weight (`LookupActivity`), its frame advanced by the server a thirtieth of a second apart (`StudioFrameAdvance`, the kinds' `nx::game::ThinkMethod`) and shown as the client estimates it, put back where it started as each round starts (`RePosition`), the shots meeting its hitboxes where its last think left them
- [x] The render modes of studio models (`rendermode`, `renderamt`: solid, blended and added, the blended ones from the farthest)
- [ ] The render effects of studio models (`renderfx`), the players' colors, the bob of the view's model
- [ ] Optional compute skinning
- [ ] Model instancing
- [x] The light of studio models (`nx.render:light`: the light under them or the sky's, its way and its share of ambient and shade, as the original finds them)
- [x] The view model's client events, and the muzzle flashes they show (`cs.client:muzzleFlash`, [05 §9](docs/05-rendering.md#9-studio-models-and-gpu-skinning))
- [x] Muzzle flashes styled by their events and their weapon's data: the original's numbers, Counter-Strike Online's keys (animated sprites of v3), a weapon's `view.muzzleFlash` over them, its brightness, added as the original adds them (the enhanced look's layer of effects), and its light on the weapon in hand in the enhanced look ([10 §7.2](docs/10-counter-strike-gameplay.md#72-weapon-data-datacstrikeweaponsweapontoml-datacstrikeammotoml))
- [ ] The dynamic lights of studio models (*elights*, *dlights*), the muzzle flash's among them; the shells the guns eject
- [x] A frame of a map to a file without a window (`nxview --screenshot`, placed with `--position` and `--angles`, at `--time`), as the client draws it; `next` plays the maps
- [ ] Model viewer
- [ ] Reference images in CI (lavapipe)
- [ ] All official maps rendering without errors

---

## M3 — Local game

### Simulation
- [x] The client, `next` (`app.client`): the original's command line (`-basedir`, `+map de_dust2`), the player's settings and binds, `map`, `noclip` and `quit`, and a map played alone in a window (`cs.client`: `Level`, `Scene`, out of `nxview`)
- [x] The game played by its server, `cs.server` (`cs::Game`), in the client's process as a listen server plays it ([01 §3](docs/01-architecture.md#3-execution-modes)): each tick the client's command goes to it and the client takes its snapshot (`cs::PlayerSnapshot`, `cs::ClientGame`), its hostages as their entity state tells them (`cs::HostageState`); the game's content and its map read once and shared (`cs::GameContent`, `cs::GameMap`), the server's studio models read without their textures (`cs::GameModels`); an entity's angles read one way for every entity, in the order of its keys (`nx::game::anglesOf`)
- [ ] The light styles the game sets, told by the server to its clients (the client sets them from `data/cstrike/lightStyles.toml` today, `cs::ClientContent`)
- [x] Several players in the server's game (`cs::Game::join`, JoiningThink): spawned at once but 20 seconds into a round of a game with both teams (`FPlayerCanRespawn`, `cs::kJoinSpawnWindow`), else watching until the next round; each team's spawn points in turn for all its players; the round's win conditions (`cs::Rounds::checkWinConditions`: Game Commencing, `TeamExterminationCheck`) and their money by whether the map has a bomb target (`RR_CTS_WIN` 3000 or `RR_BOMB_DEFUSED` 3250, `bombTargetMoney`); the players alive solid to each other in movement (`SV_AddLinksToPM`'s boxes, standing or ducked)
- [x] Tick-based `GameTime`, with no degradation over uptime (`nx.core:gameTime`: ticks and a fraction; `nx.game:clock`: each world's `TickTime`, the pushers' clocks on it)
- [ ] Usercmd generated at a fixed rate (`cl_movement_rate`)
- [x] Traces on per-hull clipnodes (`nx.collision`: the server's rules and player movement's, as reconstructed by the GPL engines; every map's hulls built by `nxcheck`)
- [ ] Trace parity tests against traces recorded from the original
- [ ] Dynamic spatial partition (AABB tree) and linking to leafs
- [x] Traces against animated hitboxes (`nx::collision::traceHitboxes`, `cs::placedHitboxesOf`): the hostages' stop the shots
- [x] One server trace for the game (`nx::game::traceSolidsHit`, the original's `SV_Move`, [07 §4.2](docs/07-simulation-and-physics.md#42-entities)): the world first, then the entities on the way to where it stopped, the brush entities and those solid as boxes (`nx::game::BoxSolidity`, a kind's `nx::game::ClipMethod`), none when the world stops it at once or once all in solid; the attacks', the pushers', the drops' to the floor
- [ ] The pushers moving the hostages and the hostages' own movement (`MOVETYPE_STEP`), and a trace that leaves out an entity of no size for one of some ("points never interact"): with the hostages' movement
- [x] pmove in `cs.shared` as a pure function (`cs::simulateMovement`): walking, the air, jumps with their penalty and the bunny hop limit, ducking (the ducktap included), stairs, sliding, water and ladders, among the world and brush entities at rest (solid, volumes, ladders); `nxview` walks through the maps with it
- [x] pmove: noclip (`MoveType::Noclip`, pushers pass through it), the `noclip` of `next`
- [x] pmove: unsticking, by the server's rules (`PM_CheckStuck`: a nudge tried each 50 ms, the original's 54 in their order)
- [ ] pmove: observers; the body of a player killed by another, tossed (`PM_Physics_Toss`, `pmove->dead`'s branches), pushed by pushers without blocking them until its death is played (`SV_PushMove`'s corpses)
- [x] pmove: a dead player killed by nothing (`MovementState::isDead`): it observes its own body, which no command moves (`PM_SpectatorMove` of `OBS_CHASE_FREE` on itself), its eyes 8 units under its origin (`PM_DEAD_VIEWHEIGHT`), pushers passing through it
- [x] pmove among entities: hostages as boxes (`cs::SolidBox`, grown by the player's box and traced by `nx::collision::traceBoxMovement`)
- [x] pmove among entities: the other players alive as boxes, standing or ducked (`cs::Game`, `SV_AddLinksToPM`)
- [ ] pmove among entities: pushables as boxes, a player stuck in another one pushed out by force, base velocity (conveyors, `trigger_push`, water currents)
- [x] Brush entities made by the game when the map starts (`nx.game`, by the classes of `data/cstrike/entities.toml`), listed for movement by `cs::brushEntitiesOf`
- [ ] Parity: ground acceleration and friction (including `edgefriction`)
- [ ] Parity: air acceleration and strafing
- [ ] Parity: jump and stamina penalty
- [ ] Parity: bunny hop limiters
- [ ] Parity: crouching on the ground and in the air (duck jump, ducktap)
- [ ] Parity: steps and ramps (surf)
- [ ] Parity: ladders
- [ ] Parity: water
- [ ] Parity: fall damage (the rule is done, `cs::landingDamageOf`; the parity test against falls recorded on the original remains)
- [x] Sliding doors (`func_door`, `func_water`): opened by a player's touch, moved by the pusher physics of the original (`SV_PushMove`), pushing bodies and blocked by them (`nx.game:movers`); `nxview` opens them
- [x] Rotating doors (`func_door_rotating`): opened away from the player, turned by the pusher physics (`SV_PushRotate`), met turned by movement and the server's traces
- [x] Doors opened by others: used by the targets that name them (`CBaseDoor::Use`), firing their own targets when they arrive
- [x] Buttons (`nx.game:buttons`: `func_button` used, touched or used by targets, toggles, waits, firing their targets) and a player's use (`playerUse`), the moves of doors and buttons shared (`nx.game:toggles`)
- [x] The player's use as the original's `ImpulseCommands` runs it in `ItemPostFrame` ([07 §5.2](docs/07-simulation-and-physics.md#52-think-touch-and-triggers)): after the command moved it, before the weapon's frame, not while it is held back (`m_flNextAttack`, `cs::Inventory::isHeldBack`), heard finding something to use or nothing (`data/cstrike/use.toml`)
- [ ] Platforms, trains, `func_rotating`, `func_rot_button`; `master` (multisource); shot buttons; a blocked door's damage; the player's view turned with a door that carries it
- [ ] Entity movement types (`TOSS`, `BOUNCE`, `FOLLOW`, `FLY`, `STEP`, `NOCLIP`; `PUSH` beyond doors)
- [x] Deterministic think and targets (`nx.game:thinking`: `NextThink` on the game's clock, by time then entity; `nx.game:targets`: names, `FireTargets`, delays, kill targets) and `trigger_multiple` / `trigger_once` (`nx.game:triggers`, `SV_TouchLinks`)
- [ ] The other triggers (`trigger_hurt`, `trigger_push`, `trigger_teleport`, `trigger_gravity`, `trigger_relay`, `multi_manager`...) and how a target is used (`USE_TYPE`)

### Player and weapons
- [x] The weapons' data: what each is (slot, team, price, ammo, clip, speed, models) for the knife, Glock, USP, AK-47 and M4A1 (`cs.shared:weapons`, `data/cstrike/weapons/`, `data/cstrike/ammo.toml`, [10 §7.2](docs/10-counter-strike-gameplay.md#72-weapon-data-datacstrikeweaponsweapontoml-datacstrikeammotoml)); the knife's speed and view model from it
- [ ] Player animation (torso sequence, gait, aim blends)
- [ ] Viewmodel with its own FOV
- [x] Weapons made as the game runs, the game's and a mod's in one catalog ([10 §7.5](docs/10-counter-strike-gameplay.md#75-weapons-made-as-the-game-runs)): standalone, from nothing or from another's values copied once, complete before they are given; their values set by the key paths of their data (`nx::data::assignField`); replaced wherever the game makes them; their view models read as they are made or changed; no limit of weapons nor of kinds of ammo; `create_weapon`, `set_weapon_var`, `get_weapon_var`, `list_weapons`, `replace_weapon`, `give`
- [ ] A weapon's own behavior and hooks (overriding its attack, its reload) with Rook ([15](docs/15-mod-api.md)); kinds of ammo made as the game runs
- [x] The player's weapons (`cs.shared:inventory`): given by its team's loadout (`data/cstrike/loadouts.toml`), drawn when heavier, chosen by `weapon_<name>` and `lastinv`, drawn and idling as their data says; each view model in the player's hand, the guns mirrored as the original draws them ([10 §7.3](docs/10-counter-strike-gameplay.md#73-behaviors), [05 §9](docs/05-rendering.md#9-studio-models-and-gpu-skinning))
- [x] The guns' fire and reload (`cs::Inventory::frame`, the original's `ItemPostFrame`): cycle, once a press for a pistol, the empty click, burst and paced accuracy, the spread by movement, the rifles' `KickBack` and the USP's punch, dropped back in the movement, the reload and its timing; the timers counted down in float as the original's ([10 §7.3](docs/10-counter-strike-gameplay.md#73-behaviors))
- [x] `+attack2`: the USP's and the M4A1's silencers, the Glock's burst and its follow-up rounds ([10 §7.3](docs/10-counter-strike-gameplay.md#73-behaviors))
- [x] The knife's slash and stab: what they meet (`nx::game::traceSolids`), their sequences and timings ([10 §7.3](docs/10-counter-strike-gameplay.md#73-behaviors))
- [ ] Weapon state machine: holster, drop; which shoot sequence the client plays, measured on the original; the Glock's switch message (center text)
- [x] `cl_righthand`: the player's hand (`cs::HudCvars`, archived, 1 the right), the view models mirrored by it each frame (`ViewModel::isMirroredIn`, `SceneView::hand`)
- [x] A seed per command: `nx::Random`'s streams, seeded by the command's number ([07 §2.3](docs/07-simulation-and-physics.md#23-shared-seed)); the spread's comes with the spread
- [x] The shots' traces (`FireBullets3`'s 8192 units, `nx::game::traceSolidsHit` telling what stopped them) and their bullet holes on the world (`{shot1` to `{shot5`)
- [x] The marks the shots leave on brush entities (`cs::bulletMarkOf`: `DamageDecal` by render mode, a breakable's by its material: `{bproof1`, `{break1` to `{break3`)
- [x] The knife's damage (`cs::Blow`, `nx::game::attackEntity`, [07 §5.3](docs/07-simulation-and-physics.md#53-damage)): 15 for the slash, 65 for the stab, dealt to what it meets as its kind takes it
- [x] Hostages hurt and killed (`cs::harmHostage`, [10 §4.2](docs/10-counter-strike-gameplay.md#42-hostages)): the hit groups' shares (`data/cstrike/hitGroups.toml`), the flinch, the death of where it was met, no longer solid nor thinking, put back whole, the attacker's money (`hostageHarm`)
- [ ] The hostages' pain sounds (`headshot1`, `headshot2`, with spatial sound), their blood (with the temporary entities), `#Injured_Hostage` and `#Killed_Hostage` (with the center text), their fall as they die (`MOVETYPE_TOSS`) and `mp_hostagepenalty`
- [x] What the shots hit (`cs::followRound`, `FireBullets3`, [10 §7.4](docs/10-counter-strike-gameplay.md#74-damage)): their damage by the distance they went (`rangeModifier`), on through what they meet by its material and their ammo's bullet type, a mark on each wall they meet
- [x] A player hurt by its landings and killed (`cs.shared:playerHarm`, `data/cstrike/playerHarm.toml`, [10 §7.4](docs/10-counter-strike-gameplay.md#74-damage)): beyond 500 units a second, out of water, as `PostThink` and `FlPlayerFallDamage` weigh them; the whole points of the damage off its health (`cs::harmPlayer`), its pain, its splat and its death heard; dead, its weapons gone with it, doing nothing until it spawns again without armor and with its team's loadout
- [ ] Hitgroups of the players, kevlar and helmet, and the armor's share of a harm (slime's and lava's too); the hit group a player was last met in, which the original's world harms keep for their pain; the attacks of other players; `trigger_hurt`
- [ ] What a dead player sees and its HUD (the original's client follows its body, `OBS_CHASE_FREE`, then spectates; to be measured on the original), its body's death played (with the players' models: the original's `WaterMove` breathes on for a player killed with exactly no health while its death plays); the box of weapons it drops (with dropping)
- [x] Penetration by material (`materials.txt`): `FireBullets3`'s share of the power and of the damage kept by each material

### Audio
- [x] The mixer (`nx.audio:mixer`): voices fed by a command queue, a windowed-sinc resampler from any rate and pitch, 32-bit float mixing with headroom and a limiter; sounds of any WAV layout (`nx.audio:sound`) ([11 §2](docs/11-audio.md#2-architecture))
- [x] The mixer on the device's real-time thread (`nx.platform:audio`, SDL3's default output, followed when it changes), the game's sounds by name (`nx.audio:library`), `volume`, and the client's `play` and `playvol` ([11 §2](docs/11-audio.md#2-architecture))
- [x] The weapons' sounds: the view model's sound events (5004), and each weapon's draw, fire, empty click and knife sounds as data (`cs::GameSound`), on the player's channels
- [x] The weapons' sounds read before a frame plays them: every weapon's as a map starts, a weapon's as it is made or changed (`cs::soundFilesOf`, `ClientAudio::preload`), never in the frame of the first shot ([11 §2](docs/11-audio.md#2-architecture))
- [ ] The guns' fire sounds as the original's client events choose them (which file, what pitch): to be measured on the original
- [x] The knife's sounds on flesh (`knife_hit1`-`4`, `knife_stab`) as it meets a hostage (`cs::WeaponHit`); on the world `knife_hitwall1` alone, as the original's multiplayer rules play no texture sound (`CHalfLifeMultiplay::PlayTextureSounds`)
- [ ] The bullets' sounds where they hit, as the original's client events choose them: to be measured on the original
- [x] `s_device`: the sound output by its name, which `s_devices` lists, the system's default without one ([11 §8](docs/11-audio.md#8-cvars))
- [x] `s_play_unfocused` (0: silent while the window is not focused, as the original is; [11 §8](docs/11-audio.md#8-cvars))
- [ ] Streaming ahead in jobs with a prefetch margin: an underrun is survived, counted (`snd_stats`) and reported, never a silent stop; tested with slow, simulated I/O
- [ ] Device loss and change: reopen the default and resume every voice and stream where it was; tested with a fake device
- [ ] Per-entity channels with replacement as in the original
- [ ] Attenuation, pitch and volume with parity
- [ ] `room_type` DSP
- [ ] Sentences (`sentences.txt`)
- [x] Footsteps by material (`cs.shared:footsteps`, `data/cstrike/footsteps.toml`, the installation's `sound/materials.txt` read as `cs::TextureMaterials`, [11 §3](docs/11-audio.md#3-original-semantics-to-preserve)): each foot's step on the ground by what its texture is made of, on a ladder and in water, slower and quieter ducked, the jump's and the landing's (with its roll of the view), the splash in and out of water, `mp_footsteps`
- [x] The breath in water (`cs::Breath`, `data/cstrike/breath.toml`, [11 §3](docs/11-audio.md#3-original-semantics-to-preserve)): the gasp of a player coming up after a while under water and once its air ran out, and the bubbles under it, the likelier the less air is left (`CBasePlayer::WaterMove`)
- [x] Drowning, its health given back, and the harm of slime and lava (`cs::Breath`, `CBasePlayer::WaterMove`; `cs::TimedHarms`, `CheckTimeBasedDamage`; `data/cstrike/playerHarm.toml`, [10 §7.4](docs/10-counter-strike-gameplay.md#74-damage)): 3, 4 and 5 a second apart once the air ran out under water, 10 at a time given back with the head out of water as the harms kept over time are looked at, slime's 4 and lava's 10 each command times the water level
- [ ] Music (`mp3`): `media/*.mp3` streamed as a voice of the mixer (no second player), `bgmvolume` as its gain; the MP3 decoder fuzzed
- [ ] Voice pool with priority and documented stealing

### Basic HUD
- [x] HUD sprite and font renderer (`nx.render:screen`: pictures of sprites and WADs, console fonts, drawn as they are, added or blended, in pixels)
- [x] Health, armor and money where the original draws them, measured on it (`cs.client:hud`, [12 §3.2](docs/12-ui-hud-and-console.md#32-scale-and-resolution)); `hud_draw`
- [x] The crosshair with the original cvars (`cl_crosshair_color`, `cl_crosshair_size`, `cl_crosshair_translucent`), drawn by the rule measured on the original ([12 §3.2](docs/12-ui-hud-and-console.md#32-scale-and-resolution))
- [x] The ammo of the weapon in hand: its clip, the bar, its reserve and the picture of its kind of ammo (`data/cstrike/ammo.toml`, no weapon's list read), where the original draws a Glock's ([12 §3.2](docs/12-ui-hud-and-console.md#32-scale-and-resolution))
- [ ] Each weapon's crosshair (measured: the knife 7, the Glock and the Desert Eagle 8), opening as the player moves (`cl_dynamiccrosshair`); the weapon menu (`slot1`–`slot5`, `invnext`, `invprev`, `hud_fastswitch`), its layout measured on the original; the first row of each HUD picture, which the original leaves out
- [x] Numbers without truncation: health above 255, armor, money and ammo ([12 §3.4](docs/12-ui-hud-and-console.md#34-no-value-truncation))
- [x] HUD regression test with health 5000, 100000 and the type's maximum
- [x] `cl_showfps`: the frames a second where, and as, the original writes them, measured on it to the pixel (`cs.client:fpsLabel`, VGUI2's drop shadow in `nx.ui`), one measure of the frame rate for it and `net_graph` (`nx::platform::FrameRateMeter`, [12 §3.2](docs/12-ui-hud-and-console.md#32-scale-and-resolution))

---

## M4 — Networking and server

### Networking
- [ ] UDP IPv4/IPv6 transport and connectionless packets (`0xFFFFFFFF`)
- [ ] Handshake with a stateless cookie and an Ed25519 identity
- [ ] Encrypted session (X25519 + HKDF + ChaCha20-Poly1305) and anti-replay
- [ ] Bitfield acks, RTT, jitter and loss
- [ ] Rate control (`rate`, `sv_minrate`, `sv_maxrate`)
- [ ] Channels and fragmentation, `net_max_message_size`
- [ ] Typed messages with a reflection-generated serializer and a checked `BitReader`
- [ ] Delta snapshots against an acknowledged baseline
- [ ] Field width from the domain type (varint, no *wrap*) ([08 §6.1](docs/08-networking.md#61-snapshots))
- [ ] Interest management (PVS, PAS, owner audience, optional distance)
- [ ] Per-client priority and budget, with the `net_budget_overflow` metric
- [ ] Snapshots built in parallel per client
- [ ] Local player prediction and reconciliation (with the client's small nudges of a stuck player, `PM_CheckStuck`)
- [ ] Interpolation (`cl_interp`/`ex_interp`) and jitter buffer
- [ ] Lag compensation (`sv_unlag`, `sv_maxunlag`, `sv_unlagpush`)
- [ ] Predicted weapon events not resent to the shooter
- [ ] Opus voice with relay rules
- [ ] Schema hash and typed rejection
- [ ] Loopback for the listen server
- [ ] `net_graph`, `net_stats`, `net_fakelag`/`fakeloss`/`fakejitter`
- [ ] Decoder fuzzing

### Dedicated server
- [ ] *Headless* `nextds` with a compatible command line
- [ ] `maxplayers` 1–1024 with `kMaxPlayersSupported` and recommendations above 64
- [ ] `server.cfg`, `listenserver.cfg`, `mapcycle.txt`, `motd.txt`, `listip.cfg`, `banned.cfg`, per-map config
- [ ] Server cvars with the original's names, defaults and effects
- [ ] Commands (`status`, `kick`, `banid`, `addip`, `changelevel`, `users`, `stats`…)
- [ ] A2S_INFO, A2S_PLAYER and A2S_RULES with a challenge
- [ ] `nextmaster` master server with an authenticated heartbeat
- [ ] LAN discovery
- [ ] RCON over the game session and secure RCON over TCP + TLS
- [ ] Logs in the HLDS format and JSON lines
- [ ] Graceful shutdown and map change without reconnecting
- [ ] Metrics endpoint
- [ ] Docker image
- [ ] Built-in and HTTP resource download with hash verification
- [ ] Refuse to send non-redistributable content (`ResourceError::NotRedistributable`) and announce it as a local requirement
- [ ] Server content requirement (`requires`) in the A2S rule `content_requires`, in the handshake and in the server browser
- [ ] `RejectReason::MissingLocalContent` with the list of what is missing and how to configure it
- [ ] Resource manifest, `ResourceAdded` and unavailable-resource policy
- [ ] File consistency (`consistency = "exact"`)
- [ ] Test with 32 real players and stress test with 256 simulated clients

### Launcher and updates ([21](docs/21-launcher-and-updates.md), [ADR-0019](docs/adr/0019-launcher-and-automatic-updates.md))
- [ ] Two products, `client` and `server`, each with its own releases, versions, channels (`stable`, `beta`) and manifest; compatibility through the protocol major
- [ ] `nx.update`: manifests signed with Ed25519 (offline release key, short-lived online timestamp), rollback and freeze protection, key rotation
- [ ] Content-defined chunks addressed by BLAKE3, shared by both products: a release downloads only the chunks the installation lacks, in parallel, resumable, with a bandwidth limit
- [ ] Side-by-side releases per product, atomic switch of `current`, automatic rollback when the new release fails to start, crash-safe at every step
- [ ] `nextLauncher`: updates itself, then the client, then starts `next`; typed `UpdateState`/`UpdateError` shown to the player
- [ ] Background pre-download of the next client release while the game runs
- [ ] `nextds`: updates at start; `sv_update_policy` (`empty`, `mapchange`, `notify`, `off`), `sv_update_interval` and the `update` command; never switches during a match
- [ ] Release pipeline: CI builds the chunks and the manifests of each product; the release key signs offline
- [ ] Package-manager installs and the Docker image leave updates to the package manager or the image

---

## M5 — Complete Counter-Strike

### Rules
- [x] Rounds (`cs.shared:rounds`, [10 §3](docs/10-counter-strike-gameplay.md#3-match-and-round-flow)): each frozen for `mp_freezetime` (the first two seconds longer), the players barely moving and their attack held back, then running for `mp_roundtime`, ended as its time runs out by what the map is played for, the next five seconds later; `sv_restart` and `sv_restartround`; the local game's player spawning again at its team's spawn points in turn, the game restarted as it joined (`mp_startmoney`, `cs::GameCvars`)
- [x] A round ended by what the players do, once at most (`cs::Rounds::endRound`, `TerminateRound`): the draw of a game with players on one team alone once none is alive (`RoundDraw`, `TeamExterminationCheck`), the local game's player killed
- [ ] Game Commencing, and the rounds' ends by a team eliminated by the other (with more than one player)
- [ ] All round-end reasons per map type (the time run out's are done: `cs::timeOutReasonOf`)
- [ ] `mp_timelimit`, `mp_maxrounds`, `mp_winlimit`, intermission and next map
- [ ] Teams, characters, `mp_limitteams`, `mp_autoteambalance`
- [x] The money of the rounds (`cs.shared:economy`, `data/cstrike/economy.toml`, [10 §6](docs/10-counter-strike-gameplay.md#6-economy)): each end's money to its winners, the hostages still held, the loser bonus and its series, the living losers left unpaid, a player's money held from none to 16000; the local game's player paid as each round starts
- [ ] Economy with auditable transactions (`Economy::apply`, `MoneyChanged`), and the kills', the bomb's and the hostages' money

### Objectives
- [ ] C4: handout, planting, timer, defuse with and without a kit, explosion
- [ ] Hostages: follow, stop, rescue, penalties
- [ ] VIP: escort and escape zones
- [ ] Escape (`es_`)

### Weapons and equipment
- [ ] All pistols
- [ ] Shotguns (including shell-by-shell reload)
- [ ] SMGs
- [ ] Rifles (including the FAMAS burst and the M4A1 silencer)
- [ ] Snipers (one- and two-level zoom)
- [ ] M249
- [ ] Grenades: HE, flashbang (blindness by angle and distance), smoke
- [ ] Kevlar, helmet, defuse kit, NVG, tactical shield

### Buying and communication
- [ ] Buy zones and buy time, `info_map_parameters` restrictions
- [ ] Classic and VGUI buy menus
- [ ] Buy commands, `autobuy` and `rebuy`
- [ ] Radio (menus, commands, icons and sounds)
- [ ] Chat (`say`, `say_team`, rules for dead players)
- [ ] Sprays

### Interface
- [ ] Complete HUD (money, timer, status icons, radar, kill feed, progress bars, damage indicator): the round's time is drawn where the original draws it (`cs::layoutTimer`, [12 §3.2](docs/12-ui-hud-and-console.md#32-scale-and-resolution)); its color as the round runs out and with the bomb planted are to be measured on the original
- [ ] Scoreboard
- [ ] Team and character menus
- [ ] MOTD with restricted HTML
- [ ] Main menu, options and server browser
- [ ] Complete console (history, autocomplete, suggestions): the window over `nx.console`; its history, completions and suggestions are in `nx.console` ([12 §5.3](docs/12-ui-hud-and-console.md#53-the-nxconsole-module))
- [x] Binds with the original key names (`nx.settings`, above)
- [ ] Localization with typed keys
- [ ] Spectator (free, chase, first person, overview) and `mp_forcecamera`

### Maps
- [ ] All entities of [10 §5](docs/10-counter-strike-gameplay.md#5-map-entities)
- [ ] `data/cstrike/entities.toml` with keyvalue schemas
- [ ] CI loading all official maps without warnings
- [ ] Green gameplay parity suite

---

## M6 — Rook and plugins

### `rookc` compiler
- [ ] Error-tolerant lexer and parser with a lossless CST
- [ ] Name, module and import resolution (including `prelude`)
- [ ] Type checking with local bidirectional inference
- [ ] Newtypes, units and literal suffixes
- [ ] `Option`/`Result`, `error enum` with `from` and `?`
- [ ] Exhaustive `match` with patterns, guards and tuples
- [ ] Ephemeral handle analysis (no escaping and no `await`)
- [ ] `pure fn` verification
- [ ] `vars` catalogs with typed literal keys and `get*Var`/`set*Var` generation, and their underscored spelling (`get_player_var`)
- [ ] AMX Mod X's spelling: `new` as a contextual keyword, `#include` of the `amxx` headers ([ADR-0021](docs/adr/0021-rook-amxx-names.md))
- [ ] Known forwards with name and signature checking
- [ ] Interpolated strings and checked translation keys
- [ ] Lints (`magicNumber`, `stringLookupInPrecache`, `expectInGameplay`, naming, `compatibilityName`…)
- [ ] RIR (typed SSA) and optimizations
- [ ] `.rbc` bytecode generation with metadata and debug info
- [ ] Query-driven incremental compilation
- [ ] Diagnostics with a code, a suggestion and `--explain`, in English and Portuguese
- [ ] Conformance suite

### VM and host
- [ ] Bytecode verifier
- [ ] Register-based interpreter
- [ ] Precise, incremental GC per isolate
- [ ] Instruction fuel, heap and stack limits, typed failures
- [ ] Cross-plugin calls with value copying and a signature hash
- [ ] Bindings and `.rookd` generation from C++ annotations
- [ ] Forwards, pre and post hooks, `registerHook`/`disableHook`
- [ ] Commands with typed arguments and typed cvars
- [ ] `setTask`, coroutines (`wait`, `nextTick`, `until`) and owned tasks
- [ ] ECS components and systems in Rook
- [ ] `PlayerMap` and `EntityMap` with automatic cleanup
- [ ] `runJob` for `pure fn`

### Plugins and mods
- [ ] `mod.toml` and `plugins.toml` with validation
- [ ] Load order from the dependency graph, and cycle detection
- [ ] Capabilities granted by the admin
- [ ] Lifecycle (`pluginPrecache`, `pluginInit`, `pluginCfg`, `pluginEnd`, pause)
- [ ] Failure policy (`rook_fault_policy`)
- [ ] Hot reload with `persist var` migration
- [ ] `plugins list|load|unload|reload|info|pause` and `mods list|info` commands

### API
- [ ] `vars` catalogs for `Entity`, `Player`, `Weapon`, `WeaponClass` and the game rules
- [ ] Body groups by name and unlimited attachments by name
- [ ] All hooks of [15 §5](docs/15-mod-api.md#5-hookable-actions)
- [ ] Weapon classes with inheritance, virtuals and `callWeaponBase`
- [ ] Typed effects (`nx.fx`), HUD, menus, `clientPrintColor`
- [ ] `nx.storage` (KV and SQLite) and `nx.http`
- [ ] Plugin network messages
- [ ] API compatibility check in CI
- [ ] The `amxx.*` modules: AMX Mod X's natives that no catalog covers, over the canonical API ([ADR-0021](docs/adr/0021-rook-amxx-names.md))

### Tools
- [ ] `rook new`, `build`, `check`, `test`, `fmt`, `lint`, `doc`, `pack`
- [ ] `rookLsp` (`vars` key autocompletion, diagnostics, rename, go to definition)
- [ ] DAP debugger
- [ ] VS Code extension
- [ ] `rook migrate` (Pawn → Rook) with a report, and `--keep-amxx-names`

### Built-in mods
- [ ] `admin` (with `admins.toml`, typed permissions and `users.ini` import)
- [ ] `admin.menus`
- [ ] `votes` (voting and mapchooser)
- [ ] `maps` (nextmap, timeleft)
- [ ] `chat` (antiflood, messages)
- [ ] `stats`
- [ ] `pausecfg`

### Reference mod
- [ ] Port ReZombie: API plugins (classes, subclasses, props, models, modes, game vars, players, weapons)
- [ ] Port the classes (Human, Zombie, Assassin, Nemesis, Survivor, Sniper, Fleshpound, Banshee)
- [ ] Port the modes (Infection, Nemesis, Survivor, Assassin, Sniper)
- [ ] Port the weapons (Cold Steel, Drakar III, USP, AK-47) with prediction (`shared`)
- [ ] Port the HUD, menus and core
- [ ] Zombies and bosses as kinds of agents of the mod, each its own module over `nx.agents` ([ADR-0024](docs/adr/0024-agents.md))
- [ ] Freeze the 1.0 API

---

## M7 — Fine parity and ecosystem

- [ ] `nx.navigation` ([ADR-0023](docs/adr/0023-navigation.md)): meshes of convex polygons by Recast and Detour for each profile of `data/cstrike/navigation.toml`, built from a map's walkable geometry and cached by its content hash; `findPath`, `nearestPoint`, `raycast`, `randomPoint`, `isReachable` and the corridor that follows an agent; `nav_generate`, `nav_draw` and `nav_check`
- [ ] Navigation in a match: ladders, jumps, drops and teleporters as links; doors and breakables turning their areas and links on and off; the tactical layer (hiding, sniper and encounter spots, places); a `.nav` file's places and hiding spots; `nav_edit`
- [ ] `nx.agents` ([ADR-0024](docs/adr/0024-agents.md)): perception (sight within a budget, the game's sounds heard, damage), memory (perceived records whose confidence falls, a typed working board, a team's board fed by the radio, the danger learned on a map), behaviors (a state machine chosen by utility), steering along the corridor with local avoidance, intents alone; benchmarks at 64, 256 and 1024 agents
- [ ] `cs.bots` ([10 §11](docs/10-counter-strike-gameplay.md#11-bots-milestone-m7)): Counter-Strike's bot, a player of the server's game driven by its commands: moving, fighting, buying, the objectives and the radio, by `bot_difficulty`; `bot_add`, `bot_add_t`, `bot_add_ct`, `bot_kick`, `bot_quota`, `bot_difficulty`, `bot_chatter`
- [ ] Demos (`record`, `playdemo`, `timedemo`)
- [ ] Spectator relay (HLTV equivalent)
- [ ] Sandboxed client plugins and `cl_plugins`
- [ ] Security audit of the VM on the client
- [ ] `.nxspr` format (high-resolution RGBA sprites) in `nxbake` and in the renderer
- [ ] `r_fidelity enhanced` mode ([ADR-0025](docs/adr/0025-enhanced-rendering.md), proposed): material maps, calibrated effects and hardware performance validation; linear HDR scene blending/filtering/probes, GGX reflections, emission and color-preserving normalized bloom are implemented
- [ ] Optional server occlusion (`sv_occlusion`)
- [ ] Extended `.nxmap` map format, compiled from TrenchBroom maps
- [ ] Repository and first `openContent` package (own map, player, weapons, HUD and sounds, original or CC0/CC-BY)
- [ ] Stress test with 1024 players and profiling-guided optimizations
- [ ] 24 h *soak test*

---

## M8 — Release 1.0 and beyond

- [ ] DirectX 11 backend (`nx.rhi.d3d11`) with Slang → HLSL → DXBC
- [ ] RHI conformance tests on D3D11
- [ ] Linux ARM64 dedicated server
- [ ] Legal review and `THIRD_PARTY_NOTICES.md`
- [ ] Documentation site
- [ ] Mod portal
- [ ] Release 1.0
