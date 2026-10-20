# Rook

Toolchain and SDK of the **Rook** plugin language
([docs/14-rook-language.md](../docs/14-rook-language.md)), planned for M6. Each directory arrives
with its first code:

| Directory | Contents |
|-----------|----------|
| `spec/` | Grammar and conformance suite of the language |
| `compiler/` | `rookc`: parser, type checking, RIR and bytecode generation |
| `vm/` | Interpreter, bytecode verifier and per-isolate GC |
| `stdlib/` | `std` standard library |
| `lsp/` | `rookLsp`: language server |
| `cli/` | `rook`: build, test, fmt, lint, doc, pack, migrate |
| `vscode/` | VS Code extension |

## License

Everything in this directory is under the **[MIT license](../LICENSES/MIT.txt)**, as are the
`.rookd` declarations that the build generates for the plugin API. The engine and the game
are under the GPL-3.0-or-later with the [Rook Plugin Exception](../LICENSES/LicenseRef-Rook-Plugin-Exception.txt).
Plugins and mods that use only the plugin interface may adopt **any license**
([ADR-0010](../docs/adr/0010-license.md)).
