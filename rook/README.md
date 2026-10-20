# Rook

Toolchain e SDK da linguagem de plugins **Rook**
([docs/14-linguagem-rook.md](../docs/14-linguagem-rook.md)):

| Diretório | Conteúdo |
|-----------|----------|
| `spec/` | Gramática e suíte de conformidade da linguagem |
| `compiler/` | `rookc`: parser, checagem de tipos, RIR e geração de bytecode |
| `vm/` | Interpretador, verificador de bytecode e GC por isolate |
| `stdlib/` | Biblioteca padrão `std` |
| `lsp/` | `rookLsp`: language server |
| `cli/` | `rook`: build, test, fmt, lint, doc, pack, migrate |
| `vscode/` | Extensão do VS Code |

## Licença

Tudo neste diretório está sob a **[licença MIT](../LICENSES/MIT.txt)**, assim como as
declarações `.rookd` que o build gera para a API de plugins. A engine e o jogo estão sob a
GPL-3.0-or-later com a [Rook Plugin Exception](../LICENSES/LicenseRef-Rook-Plugin-Exception.txt).
Plugins e mods que usam apenas a interface de plugins podem adotar **qualquer licença**
([ADR-0010](../docs/adr/0010-licenca.md)).
