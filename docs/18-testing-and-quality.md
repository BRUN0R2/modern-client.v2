# 18 — Testing and quality

## Test pyramid

**Running today:**

| Level | Tool | Scope | When it runs |
|-------|------|-------|--------------|
| Unit | GoogleTest | Isolated functions and types (`nx.core`, parsers, ECS, math) | Every PR |
| Integration | GoogleTest (`nxIntegrationTests`) | Modules together: a world moved by parallel ECS systems, the same with any number of workers ([below](#determinism)) | Every PR |
| Render | GoogleTest on lavapipe | Offscreen draws read back, with no validation error ([below](#c-test-conventions)) | Every PR |
| Performance | Google Benchmark (`nxBenchEcs`, `nxBenchJobs`, `nxBenchLightStyles`, `nxBenchClient`) | ECS, Job System, light-style snapshots and client hot paths | Built on every PR, run by hand ([below](#performance)) |
| Fuzzing | libFuzzer | The readers of the GoldSrc formats | Nightly; every PR replays the corpora as tests |
| Real content | `nxcheck` | Every file of a legitimate Counter-Strike 1.6 installation through the readers | By hand, after a reader changes ([below](#real-content)) |

**Planned:**

| Level | Tool | Scope | When it runs |
|-------|------|-------|--------------|
| Integration | GoogleTest + *headless* server | Subsystems together (load a map, connect a client, a full round with test bots) | Every PR |
| Rook plugins | `rook test` | Built-in plugins and examples | Every PR |
| Rook conformance | Suite in `rook/spec/` | Every language rule and every diagnostic (`R0xxx`) | Every PR |
| Determinism | Replays | State hash per tick | Every PR |
| Render | Reference images | Renderer passes with tolerance | Every PR |
| Parity | Fixtures from the original | Movement, weapons, economy, rules | Nightly |
| Performance | Benchmarks on a dedicated machine | Hot paths and the full frame | Nightly |
| Fuzzing (more targets) | libFuzzer | Network decoder, `rookc` | Nightly |
| Stress | Simulated clients | 1024 players, mass entities | Weekly |
| *Soak* | Long-running server | 24 h+ with bots: stable memory, no timing degradation | Per release |

## Parity tests

The goal is to prove, with numbers, that the behavior is the same as in CS 1.6.

### Reference capture

1. The `tools/parityRecorder` tool runs scenarios **against the original game** (a
   legitimate copy), with a server plugin that records the game's output for scripted
   inputs.
2. Scenarios are deterministic sequences of usercmds (e.g. "strafe jump to the left for
   1 s on `de_dust2` from spawn X").
3. The recorded output is **numeric only**: position, velocity, flags, health, punch angles
   and impact points per tick. It becomes a *fixture* in `tests/parity/`. No Valve asset or
   code enters the repository ([20](20-legal-and-licensing.md)).

### Execution

1. The same scenario runs in Modern-Client.v2 with the same inputs and cvars.
2. The comparison is field by field, tick by tick, with a tolerance declared per field
   (e.g. position ≤ 0.01 unit).
3. A divergence produces a report with the first divergent tick, the expected and actual
   values, and a plot of the trajectory.

### Minimum coverage

| Area | Scenarios |
|------|-----------|
| Movement | Every item of [07 §3.2](07-simulation-and-physics.md#32-behaviors-that-must-be-preserved) |
| Weapons | Spread per weapon (samples with fixed seeds), recoil per burst, deploy/reload times, damage by distance and hitgroup, penetration by material |
| Grenades | Trajectory, bounces, flash blindness by angle and distance, smoke duration |
| Economy | Rewards per round-end reason, loss bonus, penalties |
| Round | State transitions and end reasons per map type |
| Input | Sensitivity in degrees per mouse count |

## Real content

Fuzzing proves that the readers survive hostile input; they must also read every file of the
real game. `nxcheck` (`tools/nxcheck`) mounts the user's own Counter-Strike 1.6 installation,
found through Steam or given with `--install <folder>`, and reads each file through the VFS with
the reader of its format, on the workers of the Job System: every lump of a map and its collision
hulls, every mip texture, picture and console font of a WAD, a model with its `*T.mdl` and the animation of each sequence from
its sequence group file, its bones posed at every frame and halfway to the next at both ends of its
first blending (`cs::poseBones`, each bone's transform rigid), every sprite, every TGA image, every
sound and every sprite list of the HUD (`sprites/*.txt`). It prints what each format read and every failure with its causes, and exits with 1 when a
file failed, 2 when the installation cannot be used.

It runs on a legitimate local copy, never in CI: no Valve file enters the repository or its
runners ([20](20-legal-and-licensing.md)). Run it after changing a reader:

```powershell
.\build\dev-windows-msvc\bin\nxcheck.exe
```

The Steam installation (2026-10-21: 5048 files, about 640 MB, read and every model posed in about
three seconds by a release build once the system has cached them) reads whole except
`valve/models/doctor.mdl`, an MDL v9 from Half-Life's development that the original refuses too.
Its first run found 12 files that their editors and compilers wrote differently from what the
readers expected, and the readers now follow them
([06 §3.1](06-assets-and-resources.md#31-goldsrc-direct-reading)).

## C++ test conventions

- One executable per module (`nxCoreTests`). Each case becomes a test in CTest.
- Tests do not use exceptions: GoogleTest's fatal assertions (`ASSERT_*`) return from the
  test function.
- **Contracts:** to verify that a check fired without aborting, the test installs a handler
  that records and returns (`ContractRecorder`, *observe* mode). The default handler
  (*enforce*) is covered by death tests (`EXPECT_DEATH`).
- **GPU:** the Vulkan tests run on whatever device the machine has, with the validation layers,
  and require that validation reports no error. In CI they run on lavapipe, Mesa's software
  Vulkan, in the Linux jobs; a machine without Vulkan skips them.
  A device requested with validation must have the Khronos layer and debug messenger extension;
  otherwise it returns `RhiError::ValidationUnavailable`. `nxVulkanValidationTests` verifies the
  missing-layer failure in a separate process with an empty `VK_LAYER_PATH`. It checks layer
  discovery and skips only when the operating system still exposes installed layers (notably
  elevated Windows), or when Vulkan is unavailable.
- **Windows:** the window tests run on SDL's offscreen video driver (`ENVIRONMENT` of
  `nxAddTest`): real windows without a display, in CI and on a developer's desktop alike.
- **Files:** a test that reads the file system writes its input into a
  `TemporaryDirectory`, removed at the end of the test. A test whose subject depends on a
  property of the file system (case sensitivity) checks the property at run time and skips
  where it does not hold, instead of a platform `#if`.
- Literals are allowed in tests as input data and expected values. The `.clang-tidy` of the
  test directories turns off only the magic-number rules and the ones that conflict with the
  GoogleTest macros.

## Determinism

- Every match can be recorded as an **input replay** (usercmds + seeds + config).
- Mandatory test: the same replay with `jobs_workers` 0, 1, 3 and 15 produces an
  **identical state hash at every tick** ([04 §8](04-job-system.md#8-determinism)).
- Gameplay bugs are reported with a replay. The replay becomes a regression test.
- Until the replay exists (M3), `DeterminismTest` (`nxIntegrationTests`) covers the rule in
  the runtime core: a world moved by `nx.math` in parallel ECS systems produces the same
  *hash* at every tick with 0, 1, 3 and 15 workers.

## Networking

- Integration tests with `net_fakelag`, `net_fakeloss` and `net_fakejitter` verify:
  prediction without visible errors under moderate loss, lag compensation hitting rewound
  targets, reconnection and map change.
- Property tests of the delta encoder: `decode(encode(state, base), base) == state` for
  random states.
- Continuous fuzzing of the decoder with a corpus of real packets.

## Render

- A software renderer (lavapipe) in CI generates images of fixed scenes (map, camera,
  entities), compared with references by a perceptual metric with tolerance.
- RHI conformance tests run the same set on every backend (Vulkan and, in the future,
  D3D11).
- Content limit tests: models with dozens of attachments, hundreds of body parts,
  high-resolution `.nxspr` sprites, health above 255 on the HUD
  ([06 §3.4](06-assets-and-resources.md#34-removed-content-limits),
  [12 §3.4](12-ui-hud-and-console.md#34-no-value-truncation)).

## Performance

| Metric | Where | Regression threshold |
|--------|-------|----------------------|
| Server tick time (p50/p99) with 32, 128 and 1024 players | `nxBenchServer` (planned) | 5% |
| Client frame time in `timedemo` | Planned with demo playback | 5% |
| Client frame extraction, hostage lookup, aim posing, light selection and grouped view-model reads | `nxBenchClient` | 5% |
| ECS iteration, scheduler and command buffers | `nxBenchEcs` | 5% |
| Job System throughput | `nxBenchJobs` | 5% |
| Light-style payload copy, unchanged snapshot comparison and frame evaluation (12 and 64 animated styles) | `nxBenchLightStyles` | 5% |
| Map loading (warm cache) | `nxBenchAssets` (planned) | 10% |
| Rook compile time per 1000 lines | `rookBench` (planned) | 10% |

Planned: the benchmarks run every night on a dedicated, stable machine, which fails a regression
above the threshold and publishes the results as a historical series
([17 §7](17-build-and-tools.md#7-continuous-integration)). Today the development and release
presets build them (`NX_BUILD_BENCHMARKS`), so the CI keeps them compiling, and they
are run by hand from a release build: `build/release-windows-msvc/bin/nxBenchEcs`.

`nxBenchClient` uses only synthetic content. It measures frame extraction with 32, 128 and 1024
players, brush placements and hostage-model lookups with 32 and 1024 entities, a four-bone player's
nine-blend aim, selecting zero or four of 4096 reaching lights, and loading a view model whose one
or 64 sequences share a 1 MiB external group. Fixture creation and initial decoding are outside
frame measurements; file reads use the operating system's normal cache. Compare the same Release
build, inputs and machine, using repeated runs and their medians; these are component measurements,
not a measurement of the complete frame or a promise about player FPS:

```powershell
.\build\release-windows-msvc\bin\nxBenchClient.exe --benchmark_min_time=0.25s --benchmark_repetitions=5
```

## Sanitizers and analysis

- TSan on the tests of every pull request and every push to `main` (GCC, `tsan-linux-gcc`
  preset). Its suppressions (`tests/tsan.supp`) name only code that is not the project's, each
  with why: today Mesa's lavapipe, whose uninstrumented threads TSan sees through their pthread
  calls only. Planned: ASan + UBSan on every pull request and a nightly MSan, when Clang joins CI.
- `clang-tidy` runs locally against `build/dev-windows-clang/compile_commands.json` and the Clang
  modules produced by a complete build. Use LLVM 23.1.4's `clang-tidy-diff.py` with a zero-context
  Git diff and `-p1 -path build/dev-windows-clang`; the full repository profile applies to changed
  lines. Existing private `m_` declarations remain naming inconsistencies under [02 §3.1](02-coding-standards.md#31-project-rule-camelcase),
  recorded in `TODO.md`, without unrelated renames or disabled naming checks. An automated
  analysis gate remains planned; the Clang build and tests already run in CI.

  With LLVM on `PATH`, set `$reviewBase` to the commit before the changes being reviewed:

  ```powershell
  $tidyDiff = Join-Path $env:TEMP 'clang-tidy-diff.py'
  Invoke-WebRequest 'https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-23.1.4/clang-tools-extra/clang-tidy/tool/clang-tidy-diff.py' -OutFile $tidyDiff
  git diff --ignore-all-space --unified=0 $reviewBase -- '*.cpp' '*.cppm' '*.hpp' |
      python $tidyDiff -p1 -path build/dev-windows-clang -clang-tidy-binary clang-tidy -j 4
  ```

- Fuzzing of the parsers of external data (`tests/fuzz`, libFuzzer with AddressSanitizer): every
  night, and locally with the `fuzz-windows-msvc` preset. Each target has a versioned corpus of
  synthetic seeds that every build replays as a test (`fuzzWad.corpus`); every crash found joins
  the corpus, so it stays a test.

## Coverage targets

Planned: no CI job measures coverage yet. These targets, and the rule below, apply once one does.

| Area | Line target |
|------|-------------|
| `nx.core`, `nx.ecs`, `nx.jobs`, `nx.net` (encoding), format parsers | ≥ 85% |
| `cs.shared` | ≥ 80%, plus 100% of the parity items |
| `rookc` (checker) | 100% of the diagnostics with a conformance test |
| Others | ≥ 70% |

Coverage is an indicator, not a goal: a PR cannot **reduce** a module's coverage without a
justification.

## Definition of done

A [TODO](TODO.md) task is only checked `[x]` when:

- [ ] The code follows [02-coding-standards.md](02-coding-standards.md) and passed review.
- [ ] There are tests (unit, integration or parity, as the case requires) and CI is green.
- [ ] Changed parity behavior has a parity test.
- [ ] A new hot path has a benchmark.
- [ ] Documentation is updated (this directory, `///`, `README.md` and `TODO.md`).
- [ ] No `TODO` in the code without a reference.

## Bug policy

- Every fixed bug gets a **regression test** that failed before the fix.
- Parity bugs get the `parity` label and need a fixture.
- Production crashes (crash reports) are triaged weekly.
