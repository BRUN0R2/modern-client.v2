# ADR-0006 — Linguagem de plugins própria (Rook) com compilador próprio

- **Status:** Aceito
- **Data:** 2026-10-20

## Contexto

O ecossistema de CS 1.6 vive de plugins AMX Mod X em Pawn: célula sem tipo, tags fracas,
sem structs, strings em buffers, natives por offsets e callbacks por string. O pedido é uma
linguagem **nova**, fácil para quem vem do AMXX, com tipagem **mais forte que
TypeScript**, mantendo o modelo de plugins e o padrão `get*Var`/`set*Var`, e com um
compilador próprio e moderno.

## Decisão

Criar a linguagem **Rook** (nome provisório) e o compilador **`rookc`**
([14](../14-linguagem-rook.md)):

- Sintaxe C-like familiar, com os nomes de lifecycle do AMXX escritos em camelCase
  (`pluginPrecache`, `pluginInit`, `forward`), como todo o projeto ([ADR-0013](0013-convencao-camelcase.md)).
- Tipos nominais, sem `any` e sem nulo, `Result` com `error enum`, `match` exaustivo,
  unidades de medida, handles efêmeros verificados em compilação e catálogos `vars` com
  chaves literais verificadas.
- Plugins como unidade carregável e mods como pacote ([13](../13-sistema-de-mods.md)).
- Compilador incremental orientado a consultas, compartilhado por CLI e LSP.

## Alternativas consideradas

| Alternativa | Por que não |
|-------------|-------------|
| Manter Pawn (com melhorias) | A célula sem tipo é o problema de fundo. Corrigi-la seria criar outra linguagem |
| Lua / Luau | Tipagem gradual e opcional, fraca demais para o requisito |
| TypeScript (via engine JS) | O sistema de tipos é estrutural e intencionalmente não *sound*, e o runtime é pesado |
| C# (Mono/.NET) | Runtime grande, GC global, sandbox difícil para plugins de cliente |
| AngelScript | Tipagem melhor que Lua, mas sem unidades, `Result` nem exaustividade, e com sintaxe e ferramentas datadas |
| Plugins em C++ (DLL) | Sem sandbox, quebra o servidor em qualquer erro e impede plugins de cliente seguros |

## Consequências

- ✅ Migração natural para a comunidade e erros pegos em compilação.
- ✅ Controle total sobre diagnósticos, ferramentas e evolução.
- ⚠️ Projeto grande (compilador, VM, LSP, depurador). Mitigação: trilha paralela (M6)
  desde cedo, especificação e suíte de conformidade.
- ⚠️ Nome "Rook" provisório e a confirmar antes do release.
