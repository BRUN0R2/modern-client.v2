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
- [ ] Menu toolkit: in-house or RmlUi ([12 §2](docs/12-ui-hud-and-console.md#2-ui-components))
- [ ] Define the reference hardware for the performance targets
- [x] Free content by default; original assets optional and configured locally ([ADR-0015](docs/adr/0015-free-content-and-optional-original-assets.md))
- [x] Rook Plugin Exception: additional permission under section 7 of the GPL for plugins and mods ([ADR-0010](docs/adr/0010-license.md))
- [x] SDL3 (window, events, input, raw mouse) arrives in M2, together with the Vulkan swapchain, its first consumer. In M1 nothing uses a window or input, and the vcpkg `sdl3` port pulls X11, Wayland and D-Bus into the CI Linux with nothing to test; the M1 `nx.platform` has only the clocks (2026-10-20)
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
- [x] Glossary
- [x] ADRs 0001–0016
- [x] camelCase rule documented ([02 §3.1](docs/02-coding-standards.md#31-project-rule-camelcase))
- [x] Licenses in the REUSE format: `LICENSES/`, `LICENSE`, `REUSE.toml` and SPDX headers
- [ ] `CONTRIBUTING.md` (PR flow, DCO, standards, license of contributions, how to run the tests)
- [ ] `SECURITY.md` (how to report vulnerabilities)
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
- [ ] `server-headless` and `tools` presets (once the targets exist)
- [ ] Windows presets with GCC (MinGW-w64) and Clang
- [x] `vcpkg.json` with a pinned baseline and a `tests` feature
- [x] `x64-windows-nx` and `x64-linux-nx` triplets (dependencies without exceptions and without RTTI)
- [x] `.clang-format` and `.clang-tidy` with `readability-magic-numbers` and naming as errors
- [x] Warnings as errors, deterministic floating point, no exceptions and no RTTI
- [x] `scripts/devShell.ps1` for command-line builds on Windows
- [ ] Run `clang-tidy` with modules (requires a Clang build and `compile_commands.json`)
- [ ] Recommended editor settings in `.vscode/`
- [x] Remove the dependency on the experimental `import std` and the `@cmake_cxx_std.lib` workaround
- [x] Normalize the tree's line endings according to `.gitattributes`

### Toolchain
- [x] Validate MSVC 14.51 (VS 2026) with CMake 4.3.1 and 4.4.4, Debug and Release
- [x] Validate GCC 16 on Linux (GCC 16.2.0 in CI, 84 tests)
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
- [ ] `layers` job (dependency graph check)
- [x] ThreadSanitizer on all tests (`tsan-linux-gcc` preset in the `linux-gcc` job)
- [ ] ASan + UBSan jobs (with Clang)
- [x] Line endings (`git ls-files --eol`) in the `checks` job
- [x] `reuse lint` in the `checks` job

### `nx.core`
- [x] `nx::Failure<E>`, `nx::Result<T, E>`, `nx::fail`, chained cause, `nx::describe` and `std::formatter`
- [x] Contracts `nx::expects`, `nx::ensures`, `nx::invariant` with an *enforce*/*observe* handler
- [ ] Contract handler: stack trace, `breakpoint_if_debugging` and crash report
- [x] Introspection: `typeName`, `shortTypeName`, `enumName`, `enumCount` (signature-based fallback)
- [ ] Introspection through the native path (P2996) when a supported compiler offers it
- [x] `nx::Quantity` and units (`HammerUnits`, `UnitsPerSecond`, `HitPoints`, `ArmorPoints`, `Damage`, `Money`, `Scale`, angles, time) with literals
- [x] `nx::narrow`, `nx::tryNarrow`, `nx::truncate`, `nx::tryRound`, checked and saturating arithmetic
- [x] `EnumArray` and `Flags<E>`
- [ ] `inplace_vector` (facade), when the first consumer exists (ECS or jobs)
- [x] `FrameArena` with typed failures (`Exhausted`, `InvalidAlignment`)
- [ ] Generic `PoolAllocator<T>` (the ECS chunks use the `World`'s own block pool) and `TrackedHeapAllocator`
- [ ] Lock-free SPSC/MPSC queues with a typed queue-full error, when the first consumer exists (networking, audio)
- [x] Structured logging: levels, per-module categories, compile-time checked formatting, `ConsoleSink`, *thread-safe* `RateLimit`
- [ ] File and JSON lines sinks
- [x] `nx::hash64` (`constexpr` XXH64, verified against the reference implementation) and `nx::StableId`
- [ ] BLAKE3 for content hashing (with the VFS, M2)
- [x] Unit tests for what already exists in `nx.core` (84 tests, including a death test of the default handler)

### `nx.meta`
- [ ] Reflection facade: members, annotations, `enumName`
- [ ] Native path (P2996 + P3394)
- [ ] Fallback path (explicit registration)
- [ ] Test that guarantees identical metadata on both paths
- [ ] TOML schemas derived from structs (toml++ without exceptions) with per-file/line errors

---

## M1 — Runtime core

### `nx.jobs`
- [x] Workers and Chase-Lev deques (one per priority in each worker)
- [ ] CPU affinity for the workers (with `nx.platform`, if a benchmark shows a gain)
- [x] Global queue per priority (`Critical`, `Normal`, `Low`, `Background`), producers never wait
- [x] Counters with generation, dependencies and continuations
- [x] Lock-free pools for descriptors, counters and continuations
- [x] `wait` with *help-while-waiting*
- [x] `parallelFor` with automatic and fixed granularity
- [x] `JobSystem::stats()` and serial mode (`workerCount = 0`)
- [ ] `jobs_stats` and `jobs_serial` commands (with the console)
- [ ] Tracy zones (with the Tracy dependency)
- [ ] `std::execution`-compatible scheduler (stdexec), with asset streaming (M2)
- [ ] Per-job scratch arena in `JobContext`, when the first consumer needs it
- [x] Tests, including an identical result with 0, 1, 3 and 15 workers, under ThreadSanitizer in CI
- [ ] Benchmarks (`nxBenchJobs`)

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
- [ ] Benchmarks with the targets of [03 §16](docs/03-ecs.md#16-performance-targets-reference-hardware-to-be-defined-in-m1)

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
- [x] Determinism test with 0, 1, 3 and 15 workers (`nxIntegrationTests`: `nx.math` in parallel ECS systems, world *hash* every tick)

---

## M2 — Assets and visible world

### Assets
- [ ] VFS with mounts, priority, case-insensitive lookup and path validation
- [ ] Content sources (`engine`, `devContent`, `openContent`, `goldsrcInstall`, mods) with a redistribution flag
- [ ] `classic`, `open`, `dev` and `custom` profiles, and reading of `content.toml` (order: `--content-config`, working directory, user profile)
- [ ] Steam installation detection (registry and `libraryfolders.vdf` on Windows; `~/.steam`, `~/.local/share/Steam` and Flatpak on Linux)
- [ ] Validation against `data/content/goldsrcCstrike.toml` with `ContentError::MissingEssentialFiles`
- [ ] First-start wizard ("use the original assets?") that writes `content.toml`
- [ ] `--content`, `-basedir` and `--content-config` options
- [ ] `content_list`, `content_scan`, `content_add`, `content_profile` commands
- [ ] Code-generated `devContent`: grid textures, test map, capsule player, simple weapons, synthesized sounds
- [ ] BSP v30 reader (all lumps) with fuzzing
- [ ] WAD3 reader with fuzzing
- [ ] MDL v10 reader (including `*T.mdl` and `*01.mdl`) with fuzzing
- [ ] SPR v2 reader with fuzzing
- [ ] WAV reader (PCM 8/16, cue points) with fuzzing
- [ ] Internal representation without fixed caps (attachments, body parts, bones, controllers, blends) ([06 §3.4](docs/06-assets-and-resources.md#34-removed-content-limits))
- [ ] Generational `AssetHandle<T>`, states and typed errors
- [ ] Asynchronous pipeline (I/O → decoding → dependencies → upload)
- [ ] Per-category budgets with LRU eviction
- [ ] Asset hot reload in development
- [ ] `nxbake`: content-addressed cache

### RHI and Vulkan
- [ ] `nx.platform`: SDL3 window, events and input (vcpkg `sdl3` port), raw mouse, together with the swapchain ([decision](#pending-decisions))
- [ ] `nx.rhi` interface (device, queue, command list, buffers, textures, samplers, bind groups, pipelines, queries)
- [ ] Vulkan 1.3 backend with volk and VMA
- [ ] Swapchain, present modes, recreation and *device lost*
- [ ] Parallel recording (one command pool per worker per frame)
- [ ] Persistent staging ring
- [ ] On-disk pipeline cache
- [ ] Slang shader pipeline (SPIR-V) in the build, layout reflection and hot reload
- [ ] Built-in fallback shaders with `#embed`
- [ ] RHI conformance tests

### Rendering
- [ ] View and projection matrices in `nx.math`, with the renderer's *clip space* and depth conventions ([05](docs/05-rendering.md))
- [ ] Render graph (barriers, aliasing, pass culling, parallel recording)
- [ ] BSP world: PVS, frustum culling, batching by texture and lightmap
- [ ] Lightmap atlas and lightstyles combined in the shader
- [ ] Sky, water, animated textures, masked transparency
- [ ] Brush entities with transforms and render modes
- [ ] Emulation of gamma, brightness, texgamma and lightgamma
- [ ] Studio models: animation evaluation on the CPU in jobs
- [ ] GPU skinning (vertex shader) with per-vertex and per-normal bone indices
- [ ] Optional compute skinning
- [ ] Model instancing
- [ ] Map and model viewer with a free camera
- [ ] Reference images in CI (lavapipe)
- [ ] All official maps rendering without errors

---

## M3 — Local game

### Simulation
- [ ] Tick-based `GameTime`, with no degradation over uptime
- [ ] Usercmd generated at a fixed rate (`cl_movement_rate`)
- [ ] Traces on per-hull clipnodes with result parity
- [ ] Dynamic spatial partition (AABB tree) and linking to leafs
- [ ] Traces against animated hitboxes
- [ ] pmove in `cs.shared` as a pure function
- [ ] Parity: ground acceleration and friction (including `edgefriction`)
- [ ] Parity: air acceleration and strafing
- [ ] Parity: jump and stamina penalty
- [ ] Parity: bunny hop limiters
- [ ] Parity: crouching on the ground and in the air (duck jump, ducktap)
- [ ] Parity: steps and ramps (surf)
- [ ] Parity: ladders
- [ ] Parity: water
- [ ] Parity: fall damage
- [ ] Entity movement types (`TOSS`, `BOUNCE`, `PUSH`, `FOLLOW`, `FLY`, `STEP`, `NOCLIP`)
- [ ] Deterministic think, touch and triggers

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
- [ ] Mixer on a real-time thread with a command queue
- [ ] Per-entity channels with replacement as in the original
- [ ] Attenuation, pitch and volume with parity
- [ ] `room_type` DSP
- [ ] Sentences (`sentences.txt`)
- [ ] Footsteps by material
- [ ] Music (`mp3`)
- [ ] Voice pool with priority and documented stealing

### Basic HUD
- [ ] HUD sprite and font renderer
- [ ] Health, armor, ammo and crosshair (with the original cvars)
- [ ] Numbers without truncation: health above 255, high ammo counts ([12 §3.4](docs/12-ui-hud-and-console.md#34-no-value-truncation))
- [ ] HUD regression test with health 5000, 100000 and the type's maximum

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
- [ ] Complete console (history, autocomplete, suggestions)
- [ ] Binds with the original key names
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
