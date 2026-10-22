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
- [x] Menu toolkit: in-house, in VGUI2's model ([ADR-0020](docs/adr/0020-in-house-ui-toolkit.md))
- [x] `nx.ui` panels, placed by VGUI2's layouts and following their parent as it changes size
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
- [ ] `tools` preset (its targets, `nxcheck` and `nxview`, exist) and `server-headless` preset (once `nextds` exists)
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
- [ ] Lock-free SPSC/MPSC queues with a typed queue-full error, when the first consumer exists (networking, audio)
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
- [ ] `classic`, `open`, `dev` and `custom` profiles, and reading of `content.toml` (order: `--content-config`, working directory, user profile)
- [x] Steam installation detection (registry and `libraryfolders.vdf` on Windows; `~/.steam`, `~/.local/share/Steam` and Flatpak on Linux): `nx::platform::steamFolders`, `nx::assets::detectGoldsrcInstall`
- [x] Validation against `data/content/goldsrcCstrike.toml` with `ContentError::MissingEssentialFiles` (`GoldsrcInstall::open`)
- [ ] First-start wizard ("use the original assets?") that writes `content.toml`
- [ ] `--content`, `-basedir` and `--content-config` options
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
- [ ] On-disk pipeline cache
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
- [x] Emulation of gamma, brightness, texgamma and lightgamma (`nx.render:gamma`: palettes through `texgamma`, the light through the original's `lightgammatable`, no overbright)
- [x] Studio models: bones posed on the CPU as the original poses them (`cs::poseBones`: one or two blends, bone controllers, the mouth), every model of the installation posed by `nxcheck`
- [ ] Studio models: animation evaluated in jobs, and the transitions between sequences
- [x] GPU skinning (vertex shader) with per-vertex and per-normal bone indices (`nx.render:studio`: every bone in a storage buffer a frame, each model lit as the original lights its vertices, chrome, masked, additive, flat shaded and fullbright textures, skins and body parts)
- [x] The view's own model kept out of walls, and `nxview` holding the knife as Counter-Strike deploys it
- [x] Sprites (`nx.render:sprite`: every orientation and format, groups, the render modes, glows hidden, dimmed and grown, alpha-tested sprites lit by the world), and `nxview` showing the maps' `env_sprite`, `env_glow` and `cycler_sprite` as the game animates them
- [x] Blended sprites, brush entities and models sorted together, as the original's one list of blended entities (`nx.render:blendedOrder`: each renderer prepared, then the solid things of each and the blended things of all in one order)
- [x] The maps' studio models in `nxview`: `cycler_sprite` models at their framerate, hostages idling, their heads straight ahead, dropped to the floor (`nx.game:placement`)
- [x] The render modes of studio models (`rendermode`, `renderamt`: solid, blended and added, the blended ones from the farthest)
- [ ] The render effects of studio models (`renderfx`), the players' colors, the bob of the view's model
- [ ] Optional compute skinning
- [ ] Model instancing
- [x] The light of studio models (`nx.render:light`: the light under them or the sky's, its way and its share of ambient and shade, as the original finds them)
- [ ] The dynamic lights of studio models (*elights*, *dlights*)
- [x] A frame of a map to a file without a window (`nxview --screenshot`, placed with `--position` and `--angles`, at `--time`), as the client draws it; `next` plays the maps
- [ ] Model viewer
- [ ] Reference images in CI (lavapipe)
- [ ] All official maps rendering without errors

---

## M3 — Local game

### Simulation
- [x] The client, `next` (`app.client`): the original's command line (`-basedir`, `+map de_dust2`), the player's settings and binds, `map`, `noclip` and `quit`, and a map played alone in a window (`cs.client`: `Level`, `Scene`, `LocalGame`, out of `nxview`)
- [x] Tick-based `GameTime`, with no degradation over uptime (`nx.core:gameTime`: ticks and a fraction; `nx.game:clock`: each world's `TickTime`, the pushers' clocks on it)
- [ ] Usercmd generated at a fixed rate (`cl_movement_rate`)
- [x] Traces on per-hull clipnodes (`nx.collision`: the server's rules and player movement's, as reconstructed by the GPL engines; every map's hulls built by `nxcheck`)
- [ ] Trace parity tests against traces recorded from the original
- [ ] Dynamic spatial partition (AABB tree) and linking to leafs
- [ ] Traces against animated hitboxes
- [x] pmove in `cs.shared` as a pure function (`cs::simulateMovement`): walking, the air, jumps with their penalty and the bunny hop limit, ducking (the ducktap included), stairs, sliding, water and ladders, among the world and brush entities at rest (solid, volumes, ladders); `nxview` walks through the maps with it
- [x] pmove: noclip (`MoveType::Noclip`, pushers pass through it), the `noclip` of `next`
- [ ] pmove: observers and unsticking
- [ ] pmove among entities: boxes (players, hostages, pushables), base velocity (conveyors, `trigger_push`, water currents)
- [x] Brush entities made by the game when the map starts (`nx.game`, by the classes of `data/cstrike/entities.toml`), listed for movement by `cs::brushEntitiesOf`
- [ ] Parity: ground acceleration and friction (including `edgefriction`)
- [ ] Parity: air acceleration and strafing
- [ ] Parity: jump and stamina penalty
- [ ] Parity: bunny hop limiters
- [ ] Parity: crouching on the ground and in the air (duck jump, ducktap)
- [ ] Parity: steps and ramps (surf)
- [ ] Parity: ladders
- [ ] Parity: water
- [ ] Parity: fall damage
- [x] Sliding doors (`func_door`, `func_water`): opened by a player's touch, moved by the pusher physics of the original (`SV_PushMove`), pushing bodies and blocked by them (`nx.game:movers`); `nxview` opens them
- [x] Rotating doors (`func_door_rotating`): opened away from the player, turned by the pusher physics (`SV_PushRotate`), met turned by movement and the server's traces
- [x] Doors opened by others: used by the targets that name them (`CBaseDoor::Use`), firing their own targets when they arrive
- [x] Buttons (`nx.game:buttons`: `func_button` used, touched or used by targets, toggles, waits, firing their targets) and a player's use (`playerUse`), the moves of doors and buttons shared (`nx.game:toggles`)
- [ ] Platforms, trains, `func_rotating`, `func_rot_button`; `master` (multisource); shot buttons; a blocked door's damage; the player's view turned with a door that carries it
- [ ] Entity movement types (`TOSS`, `BOUNCE`, `FOLLOW`, `FLY`, `STEP`, `NOCLIP`; `PUSH` beyond doors)
- [x] Deterministic think and targets (`nx.game:thinking`: `NextThink` on the game's clock, by time then entity; `nx.game:targets`: names, `FireTargets`, delays, kill targets) and `trigger_multiple` / `trigger_once` (`nx.game:triggers`, `SV_TouchLinks`)
- [ ] The other triggers (`trigger_hurt`, `trigger_push`, `trigger_teleport`, `trigger_gravity`, `trigger_relay`, `multi_manager`...) and how a target is used (`USE_TYPE`)

### Player and weapons
- [ ] Player animation (torso sequence, gait, aim blends)
- [ ] Viewmodel with its own FOV
- [ ] Weapon class system (original and custom weapons in the same system)
- [ ] Weapon state machine (deploy, idle, attack, reload, holster, drop)
- [ ] Shared spread seed per command
- [ ] Spread, recoil and damage for the knife, Glock, USP, AK-47 and M4A1
- [ ] Hitgroups, kevlar and helmet
- [ ] Penetration by material (`materials.txt`)

### Audio
- [ ] Mixer on a real-time thread with a command queue, 32-bit float mixing with headroom and a limiter, and a high-quality resampler from any rate ([11 §2.1](docs/11-audio.md#21-the-originals-audio-problems-and-their-causes))
- [ ] Streaming ahead in jobs with a prefetch margin: an underrun is survived, counted (`snd_stats`) and reported, never a silent stop; tested with slow, simulated I/O
- [ ] Device loss and change: reopen the default and resume every voice and stream where it was; tested with a fake device
- [ ] Per-entity channels with replacement as in the original
- [ ] Attenuation, pitch and volume with parity
- [ ] `room_type` DSP
- [ ] Sentences (`sentences.txt`)
- [ ] Footsteps by material
- [ ] Music (`mp3`): `media/*.mp3` streamed as a voice of the mixer (no second player), `bgmvolume` as its gain; the MP3 decoder fuzzed
- [ ] Voice pool with priority and documented stealing

### Basic HUD
- [x] HUD sprite and font renderer (`nx.render:screen`: pictures of sprites and WADs, console fonts, drawn as they are, added or blended, in pixels)
- [x] Health, armor and money where the original draws them, measured on it (`cs.client:hud`, [12 §3.2](docs/12-ui-hud-and-console.md#32-scale-and-resolution)); `hud_draw`
- [x] The crosshair with the original cvars (`cl_crosshair_color`, `cl_crosshair_size`, `cl_crosshair_translucent`), drawn by the rule measured on the original ([12 §3.2](docs/12-ui-hud-and-console.md#32-scale-and-resolution))
- [ ] Ammo; the crosshair opening as the player moves (`cl_dynamiccrosshair`) with the weapons; the first row of each HUD picture, which the original leaves out
- [x] Numbers without truncation: health above 255, armor and money ([12 §3.4](docs/12-ui-hud-and-console.md#34-no-value-truncation)); high ammo counts with the ammo
- [x] HUD regression test with health 5000, 100000 and the type's maximum

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
- [ ] Local player prediction and reconciliation
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
- [ ] Round state machine (game commencing, freeze, running, ended)
- [ ] All round-end reasons per map type
- [ ] `mp_timelimit`, `mp_maxrounds`, `mp_winlimit`, intermission and next map
- [ ] Teams, characters, `mp_limitteams`, `mp_autoteambalance`
- [ ] Economy with auditable transactions and validated values

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
- [ ] Complete HUD (money, timer, status icons, radar, kill feed, progress bars, damage indicator)
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
- [ ] `vars` catalogs with typed literal keys and `get*Var`/`set*Var` generation
- [ ] Known forwards with name and signature checking
- [ ] Interpolated strings and checked translation keys
- [ ] Lints (`magicNumber`, `stringLookupInPrecache`, `expectInGameplay`, naming…)
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

### Tools
- [ ] `rook new`, `build`, `check`, `test`, `fmt`, `lint`, `doc`, `pack`
- [ ] `rookLsp` (`vars` key autocompletion, diagnostics, rename, go to definition)
- [ ] DAP debugger
- [ ] VS Code extension
- [ ] `rook migrate` (Pawn → Rook) with a report

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
- [ ] Freeze the 1.0 API

---

## M7 — Fine parity and ecosystem

- [ ] Bots with a navigation mesh and `.nav` import
- [ ] Bot commands and cvars (`bot_add`, `bot_quota`, `bot_difficulty`…)
- [ ] Demos (`record`, `playdemo`, `timedemo`)
- [ ] Spectator relay (HLTV equivalent)
- [ ] Sandboxed client plugins and `cl_plugins`
- [ ] Security audit of the VM on the client
- [ ] `.nxspr` format (high-resolution RGBA sprites) in `nxbake` and in the renderer
- [ ] `r_fidelity enhanced` mode
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
