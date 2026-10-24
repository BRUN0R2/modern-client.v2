# 02 — Coding standards (C++26)

This document is **normative** for all C++ code in the repository, within the principles of
[RULES.md](RULES.md). Code reviews use the
[checklist](#13-review-checklist) at the end.

## 1. Language version and features

- Standard: **C++26** (`CMAKE_CXX_STANDARD 26`), without compiler extensions
  (`CMAKE_CXX_EXTENSIONS OFF`).
- The project's own code lives in **named modules** (`export module nx.ecs;`), with `import std;`.
- **No exceptions** and **no RTTI** in the engine and game modules (see
  [§6](#6-typed-errors) and [ADR-0005](adr/0005-typed-errors-without-exceptions.md)).

### 1.1 Adopted features

| Feature | Proposal | Use in the project | Without compiler support |
|---------|----------|----------------|---------------------------|
| Static reflection | P2996 | Component metadata, serialization, TOML schemas, replication, Rook bindings, `enum` ↔ string | Type and enum names: `nx.core:introspection` (through compiler function signatures). Members and annotations: an explicit description per type in `nx.meta` (`nx::meta::Description<T>`), verified against the type where P2996 exists ([§1.2](#12-toolchain-and-fallback-policy)) |
| Annotations for reflection | P3394 | `[[=nx::net::Replicated{}]]`, `[[=nx::script::Expose{}]]`, `[[=nx::data::Range{...}]]` | Same fallback |
| Expansion statements (`template for`) | P1306 | Iterate over reflected members | Same fallback |
| Contracts | P2900 | `pre`, `post`, `contract_assert` for invariants | The `nx::expects`, `nx::ensures` and `nx::invariant` functions, with the same handler ([§6.5](#65-contracts)) |
| `std::execution` | P2300 | Job System integration with senders/receivers | `stdexec` reference implementation (planned, with the scheduler: [04 §4](04-job-system.md#4-integration-with-stdexecution)) |
| `std::inplace_vector` | P0843 | Small fixed-capacity collections without the heap | Own implementation in `nx.core` (planned, with its first consumer) |
| `#embed` | P1967 | Embed shaders and minimal fallback assets into the binary | Array generation at build time |
| `= delete("reason")` | P2573 | Forbid dangerous conversions with a clear message | Plain `= delete` |
| Pack indexing (`Ts...[I]`) | P2662 | ECS query metaprogramming | `std::tuple_element` |
| Placeholder variable `_` | P2169 | Ignore values in structured bindings | A descriptive name |
| Saturating arithmetic (`std::add_sat` etc.) | P0543 | Money, counters, accumulators | **Always** the project's own `nx.core` functions (`addSaturated`…), with the same results: GCC 16 advertises the feature in `<version>`, but its `std` module does not export it |
| `std::function_ref` | P0792 | Allocation-free callbacks | Own implementation in `nx.core` (planned, with its first consumer) |
| `<debugging>` | P2546 | `breakpoint_if_debugging()` in the contract handler (`nx::breakpointIfDebugging`, `nx::isDebuggerPresent`) | Per-platform primitives (`IsDebuggerPresent`/`__debugbreak`, `TracerPid`/`SIGTRAP`) |
| `std::optional<T&>` | P2988 | Optional references without raw pointers | `nx::OptionalRef<T>` in `nx.core` (planned, with its first consumer) |

C++20/23 features used freely: modules, concepts, ranges, `std::expected`,
`std::print`/`std::format`, `std::span`, `std::mdspan`, `std::flat_map`,
*deducing this*, `std::move_only_function`, `consteval`, `constinit`.

### 1.2 Toolchain and fallback policy

- The exact support matrix (GCC, Clang, MSVC) is kept in
  [17-build-and-tools.md](17-build-and-tools.md#2-toolchains) and reviewed with every
  compiler release.
- Every feature in the table above that does not yet have universal support sits **behind a
  facade** in `nx.core`/`nx.meta`. The rest of the code calls the facade, never the feature
  directly, so the implementation can be swapped without touching the callers.
- Detection through *feature-test macros* (`__cpp_impl_reflection`, `__cpp_contracts` etc.),
  never through the compiler version.
- CI compiles both paths (native and fallback), and a test ensures that the generated
  metadata is **identical**. For reflection, the metadata comes from the explicit
  descriptions on every compiler; GCC 16 (`-freflection`, `NX_REFLECTION_BACKEND`) compares
  each description with its type at compile time (name, fields in declaration order, members
  and the annotations written on the type), so a description that drifts from its struct
  stops the build. Annotations stay in the descriptions while a supported compiler (MSVC)
  rejects the `[[=...]]` syntax.

## 2. File and module organization

```
engine/ecs/
├── CMakeLists.txt
├── src/
│   ├── nx.ecs.cppm           # primary interface: export import :world; export import :query; ...
│   ├── world.cppm            # partition  nx.ecs:world
│   ├── world.cpp             # implementation (module nx.ecs;)
│   ├── archetype.cppm        # partition  nx.ecs:archetype
│   └── ...
└── tests/
    └── worldTests.cpp
```

- One module per library. Partitions (`:world`, `:query`) organize the inside.
- Module interfaces: `.cppm`. Implementation units: `.cpp`.
- **A function defined in a module interface is `inline`.** Unlike in a header, a function
  attached to a named module is not implicitly inline when it is defined in its interface,
  not even inside its class ([dcl.inline]): every importer calls it out of line, and a hot
  accessor costs a call each time. So such definitions are declared `inline`
  (`[[nodiscard]] inline auto rowCount() const noexcept -> std::uint32_t { ... }`); templates,
  `constexpr` and `consteval` functions already are. A function that importers need not
  inline is declared in the interface and defined in the implementation unit.
- Files and directories in `camelCase` (`jobSystem.cppm`, `worldTests.cpp`, `rhiVulkan/`).
  The primary interface is named after the module (`nx.ecs.cppm`). Module names follow
  `nx.<area>[.<subarea>]`.
- Every code file (C++, CMake, scripts) starts with the **SPDX header** of the license of
  its directory ([ADR-0010](adr/0010-license.md), `REUSE.toml`). `MIT` in `rook/`,
  `GPL-3.0-or-later` everywhere else:

<!-- REUSE-IgnoreStart -->
```cpp
// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception
```
<!-- REUSE-IgnoreEnd -->

- **Third-party headers** only in the *global module fragment*:

```cpp
module;
#include <vulkan/vulkan.h>   // third-party headers only here
#include <vk_mem_alloc.h>
export module nx.rhi.vulkan;

import std;
import nx.core;
import nx.rhi;
```

- Third-party macros never leak into exported interfaces. Third-party types do not appear
  in exported signatures outside the backend itself.

## 3. Naming

| Element | Style | Example |
|----------|--------|---------|
| Namespace | lowercase (`camelCase` if it has more than one word) | `nx::ecs`, `cs::weapons` |
| Module | `nx.area.sub` | `nx.rhi.vulkan` |
| Type, concept, enum | `PascalCase` | `CommandBuffer`, `ComponentType`, `BspError` |
| Enum value | `PascalCase` | `BspError::LumpOutOfBounds` |
| Function, method, variable | `camelCase` | `parseBsp`, `entityCount` |
| Private member | `m_camelCase` | `m_chunks` |
| Constant (`constexpr`/`constinit`) | `kPascalCase` | `kDefaultTickRate` |
| Template parameter | `PascalCase` | `template <class Component>` |
| Macro (avoid) | `NX_SCREAMING_CASE` | `NX_PLATFORM_WINDOWS` |
| File and directory | `camelCase` | `jobSystem.cppm`, `contractsTests.cpp`, `rhiVulkan/` |

### 3.1 Project rule: camelCase

**The whole project uses camelCase** for functions, methods, variables, parameters and
fields, in C++ as well as in Rook. This includes the plugin API (`getWeaponVar`,
`setEntityVar`, `precacheModel`, `pluginPrecache`) and the keys of the `vars` catalogs
(`"maxClip"`, `"viewModel"`). The `snake_case` of AMXX natives (`get_weapon_var`) is not used
in new code ([ADR-0013](adr/0013-camelcase-convention.md)).

| Element | C++ | Rook |
|----------|-----|------|
| Functions, methods, variables, parameters | `camelCase` (`parseBsp`) | `camelCase` (`setWeaponVar`) |
| Fields and data keys | `camelCase` (`clipSize`) | `camelCase` (`maxClip`) |
| Private members | `m_camelCase` (`m_chunks`) | `camelCase` |
| Types, concepts, enums and enum values | `PascalCase` | `PascalCase` |
| Constants | `kPascalCase` | `SCREAMING_SNAKE_CASE` |
| Namespaces and modules | lowercase (`nx::ecs`, `nx.core`) | lowercase (`rezombie.api.classes`) |
| Files and directories | `camelCase` (`contractsTests.cpp`, `enumArray.cppm`, `rhiVulkan/`) | `camelCase` (`infectionTests.rook`) |
| Build targets, executables, CMake functions and scripts | `camelCase` (`nxCoreTests`, `rookLsp`, `nxAddTest`, `devShell.ps1`) | — |

Exceptions, because the name comes from outside the code or is required by a tool:

- **Console cvars and commands** keep the classic names (`mp_roundtime`, `sv_gravity`,
  `rz_infection_delay`, `say /menu`). They are part of the compatibility with existing
  servers, configs and binds.
- **Map entity classnames** come from the BSP (`func_buyzone`, `info_player_start`).
- **Names required by tools:** `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`,
  `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes`, `.gitignore`, `.github/`,
  `.vscode/`, `REUSE.toml` and the license texts in `LICENSES/` (named by their SPDX
  identifiers), the vcpkg triplets
  (`x64-windows-nx.cmake`: the file must be named after the triplet, which only accepts
  lowercase letters and hyphens) and the CMake configuration variables (`NX_BUILD_TESTS`, in
  uppercase like CMake's own).
- **Conventional repository files:** `README.md`, `TODO.md`, `LICENSE`,
  `CONTRIBUTING.md`, `SECURITY.md`, `THIRD_PARTY_NOTICES.md` and `docs/RULES.md`.
- **Documentation in `docs/`:** numeric prefix and kebab-case English (`02-coding-standards.md`),
  to keep the reading order ([ADR-0016](adr/0016-english-documentation.md)).
- **Files of the original game:** `server.cfg`, `listenserver.cfg`, `mapcycle.txt`,
  `motd.txt`, `liblist.gam`.
- **Language tags** in translation files (`pt-BR.toml`).
- **Translation keys** (`RZ_INFECTED_BY`).
- **Names imposed by third parties** (standard library, Vulkan, file formats).
- **AMX Mod X's spelling in Rook**: the names of its API in the `amxx` compatibility layer
  (`register_clcmd`, `client_print`), `#include <amxmodx>` and `new`, and the underscored accessors
  the compiler generates beside the canonical ones (`get_player_var`, `set_weapon_var`), in the
  plugins that use them ([ADR-0021](adr/0021-rook-amxx-names.md)). The project's own Rook code (built-in mods, examples,
  documentation, the API's tests) uses the canonical names.

`.clang-tidy` encodes the rule for C++ (`readability-identifier-naming`, as an error); it runs
in CI once a Clang build with modules exists (planned, [17 §7](17-build-and-tools.md#7-continuous-integration)).
In Rook, the `naming` lint of `rookc` will check it, and the `compatibilityName` lint will point
each AMX Mod X name at its canonical form ([14 §22](14-rook-language.md#22-lints-and-magic-numbers)).

### 3.2 Other naming rules

- No obscure abbreviations (`ent`, `pl`, `wpn`). Established exceptions: `id`, `rhi`,
  `bsp`, `fps`, `rtt`, `pvs`.
- Booleans with a semantic prefix: `isAlive`, `hasDefuser`, `canBuy`.
- Functions that can fail describe the action (`loadModel`). Functions that look up
  something optional use `find*` and return `std::optional`. Functions that require
  existence use `require*` and return `Result`.

## 4. Types

### 4.1 Strong types and units

Values with a domain meaning **MUST NOT** circulate as raw `int`/`float`.

```cpp
export namespace nx {

template <class Rep, class Tag>
class Quantity {
public:
    constexpr explicit Quantity(Rep value) noexcept : m_value{value} {}
    [[nodiscard]] constexpr auto value() const noexcept -> Rep { return m_value; }
    constexpr auto operator<=>(const Quantity&) const = default;

    friend constexpr auto operator+(Quantity a, Quantity b) noexcept -> Quantity {
        return Quantity{a.m_value + b.m_value};
    }
    friend constexpr auto operator*(Quantity a, Rep scale) noexcept -> Quantity {
        return Quantity{a.m_value * scale};
    }

private:
    Rep m_value;
};

using HammerUnits     = Quantity<float, struct HammerUnitsTag>;      // 1 unit ≈ 1 inch
using UnitsPerSecond  = Quantity<float, struct UnitsPerSecondTag>;
using HitPoints       = Quantity<float, struct HitPointsTag>;     // float, as in the original: fractional damage accumulates
using ArmorPoints     = Quantity<float, struct ArmorPointsTag>;
using Money           = Quantity<std::int32_t, struct MoneyTag>;

namespace literals {
consteval auto operator""_hu(long double v)  -> HammerUnits    { return HammerUnits{static_cast<float>(v)}; }
consteval auto operator""_ups(long double v) -> UnitsPerSecond { return UnitsPerSecond{static_cast<float>(v)}; }
} // namespace literals

} // namespace nx
```

- Simulation time: `nx::Seconds`/`nx::Milliseconds` (`std::chrono::duration<float>`), and
  `nx::GameTime` (ticks + fraction) from M3 on. Wall clock only through `nx.platform`; the
  one exception is the time stamped on log records, which `nx.core` takes itself because it
  sits below `nx.platform`.
- Angles: `nx::Degrees` and `nx::Radians`, with no implicit conversion between them.
- IDs and handles: dedicated types (`Entity`, `AssetHandle<Model>`, `ClientSlot`),
  **never** raw indices.

### 4.2 Enums

- Always `enum class` with an explicit underlying type (`: std::uint8_t`).
- The `enum ↔ string` conversion comes from reflection (`nx::enumName(value)`, and
  `nx::enumerators<E>()` for the named values of a counted enum), never from hand-written
  tables or loops over raw indices.
- Enums used as array indices have a `Count` sentinel and `nx::EnumArray<E, T>`.

```cpp
enum class HitGroup : std::uint8_t { Generic, Head, Chest, Stomach, LeftArm, RightArm, LeftLeg, RightLeg, Count };

nx::EnumArray<HitGroup, float> damageMultiplier; // filled from data/cstrike/hitGroups.toml
```

### 4.3 Conversions

- Narrowing conversions are explicit: `nx::narrow<T>(v)` (checked by a contract)
  or `nx::truncate<T>(v)` (an intentional, documented loss).
- Single-argument constructors are `explicit`.
- Dangerous conversions are deleted with a reason:

```cpp
class Entity {
public:
    Entity(std::int32_t rawIndex) = delete("entities are handles issued by World; never build one from an index");
};
```

## 5. No magic numbers

Rule: **every numeric literal with a domain meaning has a name.** Only `0` and `1` are allowed
without a name, in contexts of identity, counting or obvious indexing.

| ❌ Forbidden | ✅ Correct |
|------------|-----------|
| `if (player.health < 25)` | `if (player.health < kLowHealthWarning)` |
| `velocity *= 0.8f;` | `velocity *= movement.duckSpeedMultiplier;` (a data value) |
| `bits.write(value, 13);` | `bits.write(value, kOriginBits);` |
| `std::array<Slot, 32> slots;` | `std::array<Slot, kMaxWeaponSlots> slots;` |
| `sleep(16)` | `sleep(kFrameBudget)` with `constexpr auto kFrameBudget = 16ms;` |

Hierarchy of where a value should live:

1. **Game data** (TOML in `data/`): anything a parity designer or a mod may want to tune
   (weapon price, damage, speed, round time).
2. **Cvar**: what the server operator or the player configures
   (`mp_roundtime`, `sv_gravity`).
3. **Named constant** (`constexpr`) in the owning module: technical invariants (format
   version, lump size, quantization bits).

`clang-tidy` runs with `readability-magic-numbers` and
`cppcoreguidelines-avoid-magic-numbers` as an **error**. In tests, literals are allowed as
input data and expected values ([18](18-testing-and-quality.md#c-test-conventions)).

## 6. Typed errors

### 6.1 Classification

| Kind of failure | Example | Mechanism |
|---------------|---------|-----------|
| **Expected** (external input, I/O, networking, mod data) | corrupted BSP file, malformed packet | `nx::Result<T, E>` (`std::expected`) |
| **Contract violation** (programming bug) | index outside the chunk, handle from another world | `pre`/`post`/`contract_assert` → contract handler |
| **Unrecoverable** (out of memory, device lost with no recovery) | repeated `VK_ERROR_DEVICE_LOST` | `nx::fatal(...)`: crash report and controlled termination |

### 6.2 Error types

Each module declares its own error codes as an `enum class`. There is no generic error code
("Unknown", "Failed") and no `bool` to signal failure.

```cpp
export namespace nx {

template <class Code>
    requires std::is_scoped_enum_v<Code>
struct Failure {
    Code                 code;
    std::string          detail;   // human-readable context (path, value, limit)
    std::source_location where;
    Cause                cause;    // optional type-erased chain; only allocated on error paths
};

template <class T, class Code>
using Result = std::expected<T, Failure<Code>>;

template <class Code>
[[nodiscard]] auto fail(Code code, std::string detail = {},
                        std::source_location where = std::source_location::current())
    -> std::unexpected<Failure<Code>>;

template <class Code, class InnerCode>
[[nodiscard]] auto fail(Code code, Failure<InnerCode> inner,
                        std::source_location where = std::source_location::current())
    -> std::unexpected<Failure<Code>>;   // chains `inner` as the cause

} // namespace nx
```

### 6.3 Example

A shortened `nx.assets:bsp`, the reader of GoldSrc maps:

```cpp
export module nx.assets:bsp;

import std;
import nx.core;

export namespace nx::assets {

inline constexpr std::int32_t kBspVersion = 30;

enum class BspError : std::uint8_t {
    TooSmall,
    UnsupportedVersion,
    LumpOutOfBounds,
    MisalignedLump,
    // ...
};

class BspView {
public:
    [[nodiscard]] static auto parse(std::span<const std::byte> bytes) -> nx::Result<BspView, BspError>;
    // ...
};

} // namespace nx::assets
```

```cpp
module nx.assets;

namespace nx::assets {

auto BspView::parse(std::span<const std::byte> bytes) -> nx::Result<BspView, BspError> {
    if (bytes.size() < kHeaderBytes) {
        return nx::fail(BspError::TooSmall, std::format("{} bytes, less than {}", bytes.size(), kHeaderBytes));
    }
    if (const auto version = loadLittleEndian<std::int32_t>(bytes); version != kBspVersion) {
        return nx::fail(BspError::UnsupportedVersion, std::format("version {}, not {}", version, kBspVersion));
    }
    // ... every lump, every record and every index it holds
}

} // namespace nx::assets
```

When crossing layers, the error is **translated** into the vocabulary of the layer above,
preserving the original as the cause:

```cpp
auto MapLoader::load(MapName name) -> nx::Result<Map, MapLoadError> {
    auto bytes = m_vfs.readAll(name.bspPath());
    if (!bytes) {
        return nx::fail(MapLoadError::NotFound, std::move(bytes.error()));
    }
    auto bsp = assets::BspView::parse(*bytes);
    if (!bsp) {
        return nx::fail(MapLoadError::CorruptBsp, std::move(bsp.error()));
    }
    return buildMap(*bsp);
}
```

### 6.4 Rules

- Every function that can fail returns `nx::Result` and is `[[nodiscard]]`. The attribute goes
  on each function: `nx::Result` is an alias of `std::expected`, which the standard does not
  mark `[[nodiscard]]`.
- Ignoring a `Result` is **forbidden**. To discard one on purpose:
  `std::ignore = ...;` with a comment stating the reason.
- Silent fallback is **forbidden**: if a resource is missing, the error propagates or
  becomes a visible diagnostic event. Never silently swap it for another resource.
- Prefer the monadic operations (`and_then`, `transform`, `or_else`) or an explicit early
  return. Do not use propagation macros.
- `detail` messages in English, with the offending value and the expected limit.
- Errors exposed to mods are mapped to Rook `error enum`s generated by reflection
  ([15](15-mod-api.md#9-error-catalog)).

### 6.5 Contracts

```cpp
auto Chunk::row(RowIndex row) const -> RowView
    pre(row.value() < m_count)
{
    return RowView{m_columns, row};
}

auto JobSystem::submit(JobDesc desc) -> JobHandle
    pre(desc.function != nullptr)
    post(handle : handle.isValid());
```

- A violation calls the project's contract handler. The default one (`nx::enforceContract`)
  writes the violation and the *stack trace* (`nx::currentStackTrace`) to `stderr`, stops in
  the debugger when one is attached (`nx::breakpointIfDebugging`) and aborts; the crash
  report on that abort comes from the application's crash reporter (`nx.platform`, with the
  first application).
- The handler is one for the whole process: `enforce` by default, and tests install one that
  records violations and returns (`observe`, `ContractRecorder`). A semantic chosen per module
  (`enforce` at the boundaries with external data, `observe` on hot paths measured by
  benchmarks) is planned, with native C++26 contracts.
- **After a violation under `observe`,** execution continues on a defined path written next to
  the check, never into undefined behavior: an out-of-range key is clamped, a bad alignment
  becomes the next power of two, an invalid handle reads as complete, a dead entity is left
  alone. That path is not an error the caller handles: a violation is a bug, reported once,
  and never also an expected failure (`Result`).

## 7. Memory and ownership

- RAII always. Raw `new`/`delete` outside allocators is **forbidden**.
- Unique ownership: `std::unique_ptr`. `std::shared_ptr` only with a written justification
  (ownership truly shared between threads with independent lifetimes).
- Non-owning parameters: `T&`, `const T&`, `std::span`, `std::string_view`,
  `std::function_ref`. A raw pointer only for "maybe null and non-owning", and even then
  prefer `std::optional<T&>`.
- Specialized allocators (in `nx.core`):
  - `FrameArena`: linear per frame, reset at the end of the frame. For temporary job data.
  - Planned, with their first consumers: `PoolAllocator<T>` (fixed-size blocks; the ECS chunks
    and the job pools have their own today) and `TrackedHeapAllocator` (memory accounted per
    category: assets, scripts, networking).
- Engine containers receive the allocator explicitly when the memory is short-lived. Do not
  use `std::pmr` on hot paths without measuring.

## 8. Concurrency

- All data shared between threads has **a documented owner** and an access protocol: an ECS
  phase, a lock-free queue, an atomic or a mutex.
- `std::mutex` is allowed only outside hot paths. Never hold a lock while calling code from
  another module or a mod callback.
- Atomics with an explicit `memory_order` and a comment justifying the order when it is not
  `seq_cst`.
- No `thread_local` with state that outlives the job (jobs migrate between threads).
- `std::this_thread::sleep_for` is forbidden in jobs. Waits use Job System counters.
- All new concurrent code comes with a test under **ThreadSanitizer**.

## 9. Functions and classes

- Small functions with one responsibility. More than about 60 lines requires a
  justification in review.
- Trailing return types (`auto f() -> T`) in exported APIs.
- `[[nodiscard]]` on every function whose return value matters. `noexcept` on everything
  that neither allocates nor fails.
- `constexpr`/`consteval` whenever possible (format parsers, tables, hashing).
- Positional boolean parameters are forbidden in public APIs. Use an `enum class`
  or an options struct with designated initialization:

```cpp
// ❌ trace(start, end, true, false);
// ✅
auto hit = collision.trace(TraceQuery{
    .start  = eyePosition,
    .end    = eyePosition + forward * weapon.range,
    .hull   = Hull::Point,
    .filter = TraceFilter::ignoring(shooter),
});
```

- Inheritance only for interfaces. Composition and ECS for behavior.
- Classes with invariants have private members. Plain data (components, descriptors)
  are aggregate structs.

## 10. Logging

```cpp
inline constexpr nx::log::Category kAssetsLog{"assets"};
inline constexpr nx::log::Category kNetLog{"net"};

nx::log::info(kAssetsLog, "loaded map {} in {}", map.name(), elapsed);
nx::log::warning(kNetLog, "client {} exceeded snapshot budget by {} bytes", slot, overflow);
```

- Formatting checked at compile time (`std::format_string`). Messages of a disabled level
  are not formatted.
- Each record carries the level (`Trace`, `Debug`, `Info`, `Warning`, `Error`), category,
  call site and time (UTC).
- Categories are constants of the **module that logs**. `nx.core` does not know the
  subsystems. The level per category is adjustable: `Logger::setCategoryLevel` in code and
  the `log_level_<category>` cvars in the console (`log_level_net debug`,
  [12 §5.3](12-ui-hud-and-console.md#53-the-nxconsole-module)).
- Sinks are `nx::log::Sink` (console today; file and JSON lines next).
- Logs on hot paths go through `nx::log::RateLimit`, which is *thread-safe*. It is state of
  the system that logs, a member like any other, never a `static`:

```cpp
// In the class: nx::log::RateLimit m_budgetWarnings{std::chrono::seconds{1}};
if (m_budgetWarnings.allow()) {
    nx::log::warning(kNetLog, "snapshot budget exceeded ({} suppressed)", m_budgetWarnings.takeSuppressed());
}
```

## 11. Comments and documentation

- Code comments in **English**, explaining the *why*, not the *what*, like every document of the
  repository ([ADR-0016](adr/0016-english-documentation.md)).
- Exported APIs have a `///` documentation comment with preconditions and possible errors.
- Behavior inherited from the original that looks odd is documented with the
  `// Parity:` tag and a link to the parity test that covers it.

```cpp
// Parity: CS 1.6 scales horizontal speed after landing from a jump (stamina);
// removing this breaks bunny-hop feel. Covered by tests/parity/movement/bhopPenalty.
```

- `TODO` only with a reference to the [TODO.md](../TODO.md) item or to an issue.

## 12. Formatting and static analysis

- `clang-format` with the repository's `.clang-format` (LLVM base, 4 spaces, column 110).
- `clang-tidy` with the repository profile. Enabled groups: `bugprone-*`, `concurrency-*`,
  `cppcoreguidelines-*`, `misc-*`, `modernize-*`, `performance-*`, `readability-*`. Exceptions
  are listed in `.clang-tidy` with a justification. It runs in CI once a Clang build with
  modules exists (planned).
- Warnings as errors (`-Werror` / `/WX`) in all of the project's own targets.
- Sanitizers in CI: TSan on the tests of every pull request and of every push to `main` (GCC,
  `tsan-linux-gcc`); ASan + UBSan and MSan arrive with Clang in CI (planned,
  [17 §7](17-build-and-tools.md#7-continuous-integration)).

## 13. Review checklist

- [ ] Are there magic numbers? Domain literals have a name, a cvar or data.
- [ ] Does every failure path return a typed error with context? Is any `Result` ignored?
- [ ] Is there a silent fallback?
- [ ] Do new dependencies between modules respect the layers ([01](01-architecture.md))?
- [ ] Is there global state, a singleton or a stateful `thread_local`?
- [ ] Does concurrent access have a documented owner and protocol?
- [ ] Does changed parity behavior have a parity test?
- [ ] Are there unit tests for new logic and a benchmark for a new hot path?
- [ ] Does the exported public API have `///` documentation?
- [ ] Has any third-party type leaked into the exported interface?
- [ ] Is every function defined in a module interface `inline` (or a template, `constexpr` or
      `consteval`)?
