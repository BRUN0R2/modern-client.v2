# 17 — Build, dependencies and tools

## 1. Repository layout

```
modern-client.v2/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json                  # dependency manifest (pinned baseline)
├── content.example.toml        # model of the per-user content configuration (ADR-0015)
├── .clang-format / .clang-tidy / .editorconfig
├── cmake/                      # toolchain helpers, warnings, sanitizers, module support
├── engine/
│   ├── core/                   # nx.core
│   ├── math/                   # nx.math
│   ├── console/                # nx.console (cvars, commands, .cfg)
│   ├── meta/                   # nx.meta (reflection facade + fallback)
│   ├── data/                   # nx.data (TOML data and configuration files with schemas)
│   ├── jobs/                   # nx.jobs
│   ├── platform/               # nx.platform (SDL3)
│   ├── ecs/                    # nx.ecs
│   ├── rhi/                    # nx.rhi (interface)
│   ├── rhiVulkan/             # nx.rhi.vulkan
│   ├── rhiD3d11/              # nx.rhi.d3d11 (future)
│   ├── render/                 # nx.render
│   ├── assets/                 # nx.assets (+ goldsrc importers)
│   ├── audio/                  # nx.audio
│   ├── net/                    # nx.net
│   ├── collision/              # nx.collision
│   ├── ui/                     # nx.ui
│   ├── game/                   # nx.game
│   └── script/                 # nx.script (Rook VM host + bindings)
├── game/
│   ├── shared/                 # cs.shared (prediction-safe code)
│   ├── server/                 # cs.server
│   └── client/                 # cs.client
├── apps/
│   ├── client/                 # next
│   ├── server/                 # nextds
│   └── master/                 # nextmaster
├── rook/
│   ├── spec/                   # grammar, conformance tests
│   ├── compiler/               # rookc (frontend, checker, RIR, codegen)
│   ├── vm/                     # interpreter, GC, verifier
│   ├── stdlib/                 # std.* in Rook + native parts
│   ├── lsp/                    # rookLsp
│   ├── cli/                    # rook (build, test, fmt, lint, doc, pack, migrate)
│   └── vscode/                 # VS Code extension (syntax, LSP client, DAP)
├── shaders/                    # Slang sources
├── data/
│   ├── cstrike/                # gameplay data (TOML)
│   └── content/                # content source descriptions (essential files of each)
├── mods/                       # built-in mods written in Rook (admin, votes, maps, ...)
├── tools/
│   ├── nxbake/                 # asset import/cook/pack
│   └── parityRecorder/        # reference data capture from the original game
├── tests/
│   ├── parity/                 # parity fixtures and tests
│   ├── integration/
│   ├── fuzz/
│   └── bench/
├── scripts/
│   └── devShell.ps1           # loads the Visual Studio x64 environment for CLI builds
├── docs/
└── TODO.md
```

Each module of `engine/`, `game/` and `rook/` follows the organization of
[02 §2](02-coding-standards.md#2-file-and-module-organization).

## 2. Toolchains

| Tool | Version | Notes |
|------|---------|-------|
| CMake | 4.3+ (validated with 4.3.1 and 4.4.4) | Stable C++ modules. The `std` module is built by the project ([ADR-0014](adr/0014-std-module-built-by-the-project.md)) |
| Ninja | 1.12+ (the one shipped with Visual Studio works) | Mandatory (module scanning) |
| vcpkg | Manifest mode with a pinned *baseline* (the one shipped with Visual Studio works) | Reproducible builds, custom triplets without exceptions |
| Slang | Pinned version | Shader compilation |
| Python | 3.12+ | Build and CI scripts only (not a runtime dependency) |

### 2.1 Compiler matrix

The project requires the C++26 subset of [02 §1.1](02-coding-standards.md#11-adopted-features),
with fallbacks behind facades.

| Compiler | Platforms | Role | Status (2026-10-20) |
|----------|-----------|------|---------------------|
| MSVC 14.51 (Visual Studio 2026 18.x) | Windows | Development toolchain on Windows | **Validated**: CMake 4.3.1 and 4.4.4, Ninja, Debug and Release, every test passing |
| GCC 16+ | Linux, Windows (MinGW-w64 UCRT) | Reference for static reflection and contracts | **Validated on Linux by CI**: GCC 16.2.0 (`gcc:16` container), libstdc++'s `std` module, every test passing, C++26 reflection (`-freflection`) checking the `nx.meta` descriptions. Windows (MinGW) to be validated |
| Clang (latest stable version) | Linux, Windows | Sanitizers (ASan, TSan, MSan, UBSan), `clang-tidy`, `clangd` | To be validated (presets ready) |

C++26 features available in MSVC 14.51 (according to the *feature-test macros*): **none**
of those listed in [02 §1.1](02-coding-standards.md#11-adopted-features). All of them use
the fallback path (signature-based introspection, contracts through functions, in-house
saturated arithmetic). CMake does not know a `/std:c++26` switch for MSVC yet, so the
dialect is `/std:c++latest`.

### 2.2 Known quirks

| Problem | Workaround in the project |
|---------|---------------------------|
| CMake's `import std` support is still experimental (a *gate* with a UUID that changes with every version) and, on MSVC, generates `@cmake_cxx_std.lib`, which `link.exe` reads as a response file | **Fixed at the cause:** the project does not use that support. `cmake/nxStdModule.cmake` reads the standard library's `modules.json` manifest and compiles the `std` module as the regular target `nx::std` ([ADR-0014](adr/0014-std-module-built-by-the-project.md)) |
| Out-of-tree sources get object directories with a long *hash* and could exceed 260 characters | **Fixed at the cause:** the `std` module source is copied to `build/<preset>/nxStd/` and compiled from there, with short paths |
| Flags that change the ABI (exceptions, RTTI) must be the same in every unit, including the `std` module | They live in the global `CMAKE_CXX_FLAGS`. Warnings are per target and do not apply to `nx::std` |
| MSVC disables `std::println(std::ostream&, ...)` without exceptions (documented STL behavior) | Output through `std::println(stderr, ...)` or `std::fputs` (`FILE*`) |
| GCC 16 hits an *internal compiler error* (`module_state::mangle`) when another unit uses a `friend` comparison operator with `= default` of a (non-template) class declared in a module | In those classes, *defaulted* comparisons are members: `auto operator<=>(const T&) const = default` |
| libstdc++'s `std` module (GCC 16) advertises `__cpp_lib_saturation_arithmetic` but does not export `std::add_sat` and friends | `nx.core` uses its own saturated arithmetic on every compiler |
| MSVC 14.51 cannot materialize a `std::optional` returned by a `consteval` call inside a template (C2440, "too many initializers") | The `nx.meta` functions that return class types (`typeAnnotation`, `Field::annotation`) are `constexpr`: pure, still evaluated at compile time where a constant is required |
| libstdc++ keeps `std::stacktrace` in a separate library, `libstdc++exp` | `nx.core` links `stdc++exp` on GCC (`nx::currentStackTrace`) |
| GCC 16 also applies `-Wmissing-field-initializers` (from `-Wextra`) to designated initializers that omit a member without a default initializer; MSVC does not warn | Every aggregate member that can be omitted has a default initializer (`Entity entity{};`). The required ones, such as `JobDesc::label`, have types without a default constructor: omitting them does not even compile |
| GCC 16 spells a type imported from a named module with the module after it (`nx::ConversionError@nx.core`) in `__PRETTY_FUNCTION__`, where `nx.core:introspection` reads type names | `nx::typeName` drops every `@<module>`, so names read the same on every compiler (`Introspection.NamesTypesAttachedToAModuleWithoutTheModule`) |

## 3. Presets

| Preset | Use | Status |
|--------|-----|--------|
| `dev-windows-msvc` / `release-windows-msvc` | Development and release on Windows (Ninja + MSVC) | Validated |
| `dev-linux-gcc` / `release-linux-gcc` | Development and release on Linux with GCC | `dev-linux-gcc` validated in CI. `release-linux-gcc` to be validated |
| `dev-linux-clang` | Development with `clangd`/`clang-tidy` | To be validated |
| `tsan-linux-gcc` | Every test under ThreadSanitizer, with GCC | In CI (`linux-gcc` job) |
| `asan-linux-clang`, `tsan-linux-clang` | Sanitizers with Clang (ASan + UBSan, TSan) | To be validated |
| `server-headless`, `tools` | Only `nextds` or only the tools | When those targets exist |

Each configure preset has a build preset and a test preset with the same name. The build
directory is `build/<preset>`.

**Windows (command line):** the build uses Ninja + MSVC, which need the Visual Studio
environment. The `scripts/devShell.ps1` script loads that environment and sets
`VCPKG_ROOT` to Visual Studio's vcpkg. Visual Studio, VS Code (CMake Tools) and CLion do
this on their own.

```powershell
. .\scripts\devShell.ps1
cmake --preset dev-windows-msvc
cmake --build --preset dev-windows-msvc
ctest --preset dev-windows-msvc
```

**Linux:**

```bash
cmake --preset dev-linux-gcc
cmake --build --preset dev-linux-gcc
ctest --preset dev-linux-gcc
```

## 4. Build options

| Option | Default | Purpose |
|--------|---------|---------|
| `NX_BUILD_TESTS` | `ON` | Builds the tests and installs the vcpkg `tests` feature (GoogleTest) |
| `NX_WARNINGS_AS_ERRORS` | `ON` | Warnings as errors (`/WX`, `-Werror`) |
| `NX_BUILD_CLIENT` | `ON` | Builds `next` |
| `NX_BUILD_SERVER` | `ON` | Builds `nextds` |
| `NX_BUILD_TOOLS` | `ON` | Builds the tools |
| `NX_RHI_VULKAN` | `ON` | Vulkan backend |
| `NX_RHI_D3D11` | `OFF` | DirectX 11 backend (future) |
| `NX_ENABLE_TRACY` | `ON` in dev | Profiler |
| `NX_REFLECTION_BACKEND` | `auto` | `native` (P2996: adds `-freflection` and checks every `nx.meta` description against its type), `fallback` (descriptions as written) or `auto` (native where the compiler has it). `native` on a compiler without P2996 fails the configuration |
| `NX_CONTRACT_SEMANTIC` | per module | `enforce` or `observe` ([02 §6.5](02-coding-standards.md#65-contracts)) |

The options from `NX_BUILD_CLIENT` on arrive together with their modules. Today
`NX_BUILD_TESTS`, `NX_WARNINGS_AS_ERRORS` and `NX_REFLECTION_BACKEND` exist.

## 5. Dependencies

| Library | Use | License |
|---------|-----|---------|
| SDL3 (from M2 on) | Window, input, device audio, voice capture | Zlib |
| Vulkan Headers + volk | Vulkan API and loader | Apache-2.0 / MIT |
| Vulkan Memory Allocator | GPU memory | MIT |
| Slang | Shader compiler | Apache-2.0 with LLVM exception |
| stdexec | `std::execution` until the standard library supports it | Apache-2.0 with LLVM exception |
| libsodium | Network cryptography, signatures | ISC |
| zstd | Package and network compression | BSD-3-Clause |
| BLAKE3 | Content hashing | CC0 / Apache-2.0 |
| xxHash | Reference implementation used in the `nx::hash64` tests (the project's hash is its own `constexpr` XXH64) | BSD-2-Clause |
| Opus | Voice and audio | BSD-3-Clause |
| minimp3, stb_vorbis, dr_flac | Audio decoding | CC0 / public domain / MIT-0 |
| fastgltf | glTF import | MIT |
| KTX-Software | KTX2/Basis | Apache-2.0 |
| libspng or stb_image | PNG/TGA | BSD-2 / public domain |
| toml++ | Configuration and data (no-exceptions mode) | MIT |
| SQLite | `nx.storage` | Public domain |
| Tracy | Profiler | BSD-3-Clause |
| Dear ImGui | Development tools | MIT |
| GoogleTest | Unit tests, including death tests, with official support for builds without exceptions | BSD-3-Clause |

Rules:

- Every new dependency goes through a review of its license (compatible with the project's
  license, [20](20-legal-and-licensing.md)), active maintenance and security surface.
- Dependencies are built by the project triplets (`cmake/triplets/x64-windows-nx`,
  `x64-linux-nx`): static libraries, **without exceptions and without RTTI**, like our code.
  Libraries that require exceptions stay isolated behind a boundary that converts to
  `nx::Result`, or are not used.
- `THIRD_PARTY_NOTICES.md` is generated from `vcpkg.json` in the release build.

## 6. Targets

| Target | Description |
|--------|-------------|
| `next` | Client (includes the listen server) |
| `nextds` | *Headless* dedicated server |
| `nextmaster` | Master server |
| `rookc`, `rook`, `rookLsp` | Rook toolchain |
| `nxbake` | Asset tool |
| `<module>Tests` (e.g. `nxCoreTests`) | Per-module tests, registered in CTest one by one (`gtest_discover_tests`) |
| `nxIntegrationTests` | Tests that span modules (`tests/integration`), such as the determinism test with 0, 1, 3 and 15 workers |
| `nxBench*` | Benchmarks |
| `fuzz*` | Fuzzing targets (libFuzzer) |

## 7. Continuous integration

The `.github/workflows/ci.yml` workflow runs on every push to `main`, on every pull request
and on demand. **Running today:**

| Job | Contents | Blocking? |
|-----|----------|-----------|
| `checks` | `reuse lint` (license and copyright in every file), `clang-format --dry-run --Werror` and line endings (nothing but LF in the repository) | Yes |
| `windows-msvc` | `dev-windows-msvc` and `release-windows-msvc`: configure, build and tests, through the same `scripts/devShell.ps1` used for development | Yes |
| `linux-gcc` | `dev-linux-gcc` and `tsan-linux-gcc` (every test under ThreadSanitizer) in the `gcc:16` container: configure, build and tests | Yes |

CMake and Ninja come from `pip` with a version range, and vcpkg is cloned on the fly, with
a binary cache between runs. CI does not depend on what comes preinstalled on the runner
image.

**Planned:**

| Job | Trigger | Contents |
|-----|---------|----------|
| `linux-clang` | Every PR | Clang on Linux, in addition to GCC |
| `lint` | Every PR | `clang-tidy` (with modules), `rook fmt --check`, `rook lint` |
| `layers` | Every PR | Check of the dependency graph between modules ([01 §1.1](01-architecture.md#11-module-catalog)) |
| `sanitizers` | Every PR | ASan + UBSan on the tests, with Clang (TSan already runs with GCC) |
| `api-compat` | Every PR | Diff of the `.rookd` files against the last release ([15 §10](15-mod-api.md#10-versioning-and-stability)) |
| `determinism` | Every PR | The same demo with 0, 1, 3 and 15 workers and an identical hash |
| `render-golden` | Every PR | Reference images with a software renderer (lavapipe) |
| `parity` | Nightly | Parity suite on a dedicated *runner* with a legitimate copy of the game ([18](18-testing-and-quality.md#parity-tests)) |
| `bench` | Nightly | Benchmarks on a dedicated machine. A regression above the threshold fails |
| `fuzz` | Nightly | Parsers (BSP, MDL, WAD, SPR, WAV), network decoder and `rookc` |
| `stress` | Weekly | Server with 1024 simulated clients |
| `package` | Release tag | Binaries, `THIRD_PARTY_NOTICES.md`, symbols, Docker image of `nextds` |

## 8. Workflow

- **Trunk-based:** a protected `main`. Every change comes in through a pull request from a
  short-lived branch; it merges only when the five CI jobs of [§7](#7-continuous-integration)
  pass on the branch brought up to date with `main`, and only as a **squash merge**, which
  GitHub signs: every commit on `main` is a green, *Verified* commit, one per logical change.
- **Branch protection of `main`** (repository settings, also binding administrators): pull
  request required, the five CI jobs required and up to date, signed commits, linear
  history, no force push and no deletion; merge commits and rebase merges are disabled, and
  merged branches are deleted. GitHub applies the signature rule to the commits of the pull
  request's branch too, so contributors sign every commit they make
  ([CONTRIBUTING.md](../CONTRIBUTING.md#commits)); the squash commit is signed by GitHub.
- The squash commit takes the pull request title as its subject and an explicit message with
  the body and the trailers (`Signed-off-by`, `Co-Authored-By`), given at merge time:
  `gh pr merge --squash --subject "<title>" --body "<message>"`.
- **Conventional Commits** (`feat(ecs): ...`, `fix(net): ...`) and a generated changelog.
- **SemVer** for the project and, separately, for the plugin API.
- Commits with *Signed-off-by* (DCO), because of the clean room policy
  ([20](20-legal-and-licensing.md#3-clean-room)).

## 9. Development environment

- **VS Code** with `clangd`, CMake Tools and the Rook extension (syntax, LSP, debugger). The
  repository ships `.vscode/` with the recommended extensions and settings: CMake Tools uses
  the presets and copies the active preset's `compile_commands.json` to `build/`, where
  `clangd` reads it (with its module support on); formatting follows `.clang-format` on save.
  `clangd` understands the project's modules only when the active preset builds with Clang;
  with MSVC presets, Visual Studio is the IDE that understands them.
- **Visual Studio** and **CLion** supported through CMake presets.
- **RenderDoc** for frame capture and **Tracy** for profiling.
- Optional *pre-commit* hooks: formatting and lint of the changed files.
