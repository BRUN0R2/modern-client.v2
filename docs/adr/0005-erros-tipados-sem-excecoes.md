# ADR-0005 — Erros tipados com `std::expected` e contratos, sem exceções

- **Status:** Aceito
- **Data:** 2026-10-20

## Contexto

O pedido é "erros totalmente tipados". Exceções escondem caminhos de falha nas assinaturas,
têm custo imprevisível e são difíceis de usar com jobs paralelos e fronteiras de plugins.
Códigos de erro genéricos (`bool`, `-1`) perdem informação.

## Decisão

- Falhas **esperadas**: `nx::Result<T, E>` = `std::expected<T, nx::Failure<E>>`, em que `E`
  é um `enum class` do módulo e `Failure` carrega contexto, `source_location` e causa
  encadeada ([02 §6](../02-padroes-de-codigo.md#6-erros-tipados)).
- **Violações de invariantes**: contratos C++26 (`pre`, `post`, `contract_assert`) com
  semântica por módulo.
- **Irrecuperáveis**: `nx::fatal` com crash report.
- Compilação **sem exceções e sem RTTI** nos módulos próprios. Bibliotecas que lançam
  exceções ficam isoladas atrás de fronteiras que convertem para `Result`.
- Erros expostos aos plugins viram `error enum` Rook gerados por reflexão.

## Alternativas consideradas

| Alternativa | Por que não |
|-------------|-------------|
| Exceções | Falhas invisíveis nas assinaturas, custo e complexidade com jobs |
| `std::error_code` | Categoria dinâmica e pouco tipada, sem contexto rico |
| Macros de propagação (`TRY`) | Não portáveis (dependem de *statement expressions*) e escondem fluxo |

## Consequências

- ✅ Todo caminho de falha aparece na assinatura e é verificado pelo compilador
  (`[[nodiscard]]`).
- ✅ Mesma filosofia no C++ e no Rook.
- ⚠️ Um pouco mais de verbosidade na propagação. Mitigação: operações monádicas e funções
  pequenas.
