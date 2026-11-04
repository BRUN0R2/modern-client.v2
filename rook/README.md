# Rook

Toolchain and SDK of the **Rook** plugin language
([docs/14-rook-language.md](../docs/14-rook-language.md)), planned for M6. Each directory arrives
with its first code:

| Directory | Contents |
|-----------|----------|
| `spec/` | Grammar and conformance suite of the language |
| `format/` | The layout of `.rbc` and `.rookd`, their versions, readers and writers |
| `compiler/` | `rookc`: parser, type checking, RIR and bytecode generation |
| `verifier/` | The load-time check of `.rbc`, sharing only `format/` with the compiler |
| `vm/` | Interpreter, per-isolate GC, budgets |
| `stdlib/` | `std` standard library |
| `lsp/` | `rookLsp`: language server |
| `cli/` | `rook`: build, test, format, lint, document, pack |
| `vscode/` | VS Code extension |

This directory imports nothing of `engine/` or `game/`, and it builds on its own. The compiler and the
engine meet only in `.rookd` and `.rbc`, and servers and clients carry no compiler
([ADR-0033](../docs/adr/0033-rook-compiler-and-types.md)).

## License

Everything in this directory is under the **[MIT license](../LICENSES/MIT.txt)**, as are the
`.rookd` declarations that the build generates for the plugin API. The engine and the game
are under the GPL-3.0-or-later with the [Rook Plugin Exception](../LICENSES/LicenseRef-Rook-Plugin-Exception.txt).
Plugins and mods that use only the plugin interface may adopt **any license**
([ADR-0010](../docs/adr/0010-license.md)).
