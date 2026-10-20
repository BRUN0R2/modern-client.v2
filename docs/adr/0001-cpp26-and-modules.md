# ADR-0001 — C++26 with named modules

- **Status:** Accepted
- **Date:** 2026-10-20

## Context

The project recreates an entire engine and wants modern, safe, easy-to-maintain code.
C++26 brings features that eliminate repetitive, fragile code: static reflection (component
metadata, serialization, replication, plugin bindings), contracts (explicit invariants) and
`std::execution`. Modules reduce build time and isolate implementation details. Compiler
support for these features still varies.

## Decision

- **C++26** standard, without compiler extensions.
- The project's own code in **named modules**, with `import std;`. Third-party headers only
  in the *global module fragment*.
- Features without universal support (reflection, annotations, contracts, `std::execution`)
  sit **behind facades** in `nx.core`/`nx.meta`, with a fallback. CI guarantees that both
  paths produce the same result.
- Toolchain matrix validated in M0 and reviewed at every compiler release
  ([17 §2](../17-build-and-tools.md#2-toolchains)).

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| C++20/23 with headers | Loses reflection and contracts. Would require external code generators for metadata |
| Rust | Engine ecosystem and Vulkan/D3D integration are mature in C++, besides the explicit request for C++26 |
| C++26 without modules | Slower builds and details leaking through headers |

## Consequences

- ✅ Metadata generated from the code itself (components, TOML schemas, networking, Rook API).
- ✅ Clean module interfaces and fast incremental builds.
- ⚠️ Risk of compiler bugs with modules + reflection. Mitigation: tested fallback, CI with
  two compilers and, as a last resort, a mechanical conversion to headers.
- ⚠️ Requires a recent CMake and Ninja. The `std` module is built by the project itself
  ([ADR-0014](0014-std-module-built-by-the-project.md)).
