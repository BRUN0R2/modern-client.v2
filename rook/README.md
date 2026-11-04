# Rook

Toolchain and SDK of the **Rook** plugin language
([docs/14-rook-language.md](../docs/14-rook-language.md)). The first part of step 1 of
[ADR-0033's order](../docs/adr/0033-rook-compiler-and-types.md#4-the-order) exists: a lexer, which reads any bytes into tokens and diagnostics. The parser, the conformance suite and the
programs come next, then name resolution, type checking, `rook/format`, the verifier, the VM and the LSP.

| Directory | Contents | State |
|-----------|----------|-------|
| `spec/` | [Grammar](spec/grammar.ebnf) and conformance suite of the language | The grammar exists; the suite is planned |
| `compiler/` | `rook.compiler`: lexer and diagnostics | The lexer exists; parser, type checking, RIR and bytecode generation are planned |
| `cli/` | `rook`: build, test, format, lint, document, pack | Planned |
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

The unit tests need GoogleTest, found through `CMAKE_PREFIX_PATH` or `GTest_DIR`; without it the
configuration says it leaves them out, and there is nothing yet for `ctest` to run. The
repository's own build adds this directory with its settings and its `std` module, runs every test,
including this build of its own (`Rook.BuildsOnItsOwn`), and refuses an edge from here into the rest of
the project ([01 §1.1](../docs/01-architecture.md#11-module-catalog)).

| Option | Default | Purpose |
|--------|---------|---------|
| `ROOK_BUILD_TESTS` | `ON` | The unit tests |
| `ROOK_WARNINGS_AS_ERRORS` | `ON` | `/WX`, `-Werror` |

## License

Everything in this directory is under the **[MIT license](../LICENSES/MIT.txt)**, as are the
`.rookd` declarations that the build generates for the plugin API. The engine and the game
are under the GPL-3.0-or-later with the [Rook Plugin Exception](../LICENSES/LicenseRef-Rook-Plugin-Exception.txt).
Plugins and mods that use only the plugin interface may adopt **any license**
([ADR-0010](../docs/adr/0010-license.md)).
