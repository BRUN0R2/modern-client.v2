# ADR-0014 — `std` module built by the project, without CMake's experimental `import std`

- **Status:** Accepted
- **Date:** 2026-10-20

## Context

The project's code uses `import std;` ([ADR-0001](0001-cpp26-and-modules.md)). The first
version of the build used CMake's own support, which brought three problems:

1. It is **experimental**: it requires a `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD` *gate* whose UUID
   changes with every CMake version. The project had to maintain a table of UUIDs that broke
   at every update.
2. On MSVC, CMake 4.4 names the module library `@cmake_cxx_std.lib`. `link.exe` reads every
   argument starting with `@` as a response file, so the library was never linked. This
   required a workaround in the output layout.
3. Out-of-tree sources get object directories with a long *hash*, which came close to the
   260-character Windows limit.

## Decision

- The project **does not use** CMake's experimental `import std` support.
- `cmake/nxStdModule.cmake` locates the module manifest that the standard library itself
  publishes (`modules.json` of the Microsoft STL, `libstdc++.modules.json`,
  `libc++.modules.json`), reads the source of the `std` module and the required options, and
  builds the module as the ordinary target **`nx::std`** (`FILE_SET CXX_MODULES`), a stable
  CMake feature.
- The source is **copied** to `build/<preset>/nxStd/` (`configure_file`, copied again when
  the toolchain changes), which keeps the object paths short.
- Only `std` is built. `std.compat` is left out because it re-exports the C library in the
  global namespace, and the project's code always uses `std::`.
- Every project target links `nx::std`. The flags that change the ABI stay global, so the
  module is built exactly like the code that imports it.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| Keep the experimental `import std` with the UUID table | Breaks with every CMake version and depends on a workaround for the `@` bug |
| Go back to `#include` of the standard library | Loses the main gain of modules (build time and isolation) |
| Header units (`import <vector>;`) | Inconsistent support across compilers and in CMake |

## Consequences

- ✅ No experimental feature in the build. Any CMake 4.3+ works (validated with 4.3.1 and
  4.4.4), without a UUID table or a library-name workaround.
- ✅ Short object paths.
- ⚠️ The manifest format may evolve. Mitigation: an unknown key in `local-arguments` is an
  explicit configuration error, not ignored.
- ⚠️ The GCC and Clang paths follow the same manifest, but will only be validated together
  with the Linux presets.
- When CMake stabilizes `import std`, the switch can be reevaluated in a new ADR.
