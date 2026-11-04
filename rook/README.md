# Rook

Toolchain and SDK of the **Rook** plugin language
([docs/14-rook-language.md](../docs/14-rook-language.md)). The first part of step 1 of
[ADR-0033's order](../docs/adr/0033-rook-compiler-and-types.md#4-the-order) exists: a lexer and an error-tolerant parser, the conformance suite that proves them, a fuzz target,
and `rookc` and `rook check`, which parse and print diagnostics. Name resolution, type checking,
`rook/format`, the verifier, the VM and the LSP come next.

| Directory | Contents | State |
|-----------|----------|-------|
| `spec/` | [Grammar](spec/grammar.ebnf) and [conformance suite](spec/conformance/README.md) of the language | Exists |
| `compiler/` | `rook.compiler`: lexer, parser, lossless syntax tree, diagnostics | Its front end exists; type checking, RIR and bytecode generation are planned |
| `cli/` | `rookc`, and `rook` with `check` | `rookc` writes no bytecode yet; `rook` build, test, format, lint, document and pack are planned |
| `cmake/` | The build of the toolchain, which needs nothing of the repository | Exists |
| `format/` | The layout of `.rbc` and `.rookd`, their versions, readers and writers | Planned |
| `verifier/` | The load-time check of `.rbc`, sharing only `format/` with the compiler | Planned |
| `vm/` | Interpreter, per-isolate GC, budgets | Planned |
| `stdlib/` | `std` standard library | Planned |
| `lsp/` | `rookLsp`: language server | Planned |
| `vscode/` | VS Code extension | Planned |

## Building

This directory imports nothing of `engine/` or `game/`, and it builds on its own with a compiler that
has the `std` module, CMake and Ninja, and nothing installed: the compiler and the engine meet only in
`.rookd` and `.rbc`, and servers and clients carry no compiler
([ADR-0033](../docs/adr/0033-rook-compiler-and-types.md)). On Windows, from the developer shell of
[`scripts/devShell.ps1`](../scripts/devShell.ps1):

```bash
cmake -S rook -B build/rookOnItsOwn -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

```bash
cmake --build build/rookOnItsOwn
```

```bash
ctest --test-dir build/rookOnItsOwn --output-on-failure
```

That runs the conformance suite and the programs' tests. The unit tests need GoogleTest, found through
`CMAKE_PREFIX_PATH` or `GTest_DIR`; without it the configuration says it leaves them out. The
repository's own build adds this directory with its settings and its `std` module, runs every test,
including this build of its own (`Rook.BuildsOnItsOwn`), and refuses an edge from here into the rest of
the project ([01 §1.1](../docs/01-architecture.md#11-module-catalog)).

| Option | Default | Purpose |
|--------|---------|---------|
| `ROOK_BUILD_TESTS` | `ON` | The conformance suite, the unit tests and the fuzz target's replay |
| `ROOK_BUILD_PROGRAMS` | `ON` | `rookc` and `rook` |
| `ROOK_WARNINGS_AS_ERRORS` | `ON` | `/WX`, `-Werror` |
| `ROOK_FUZZ` | `OFF` | Link the fuzz target with libFuzzer, for which `CMAKE_CXX_FLAGS` must instrument every unit; off, it replays its corpus |
| `ROOK_FUZZ_SECONDS`, `ROOK_FUZZ_INPUT_SECONDS` | `60`, `30` | How long `ROOK_FUZZ` runs, and how long one input may |

## Using it

```bash
rook check plugins/
```

```text
error[R0030]: a condition goes in parentheses
 --> plugins/class/assassin.rook:5:8
  |
5 |     if count > 0 {
  |        ^^^^^ expected `(` here
  = fix: write `if (condition) { ... }`
```

`rookc <file or directory>...` and `rook check [<file or directory>...]` do the same today, the second
under the current directory by default. They exit with 0 when the sources are clean, 1 when they have
errors and 2 when they could not run. They treat a source as untrusted: no input crashes them, and
none of its bytes reach a terminal uncleaned.

## License

Everything in this directory is under the **[MIT license](../LICENSES/MIT.txt)**, as are the
`.rookd` declarations that the build generates for the plugin API. The engine and the game
are under the GPL-3.0-or-later with the [Rook Plugin Exception](../LICENSES/LicenseRef-Rook-Plugin-Exception.txt).
Plugins and mods that use only the plugin interface may adopt **any license**
([ADR-0010](../docs/adr/0010-license.md)).
