# ADR-0012 — Padrão `get*Var`/`set*Var` com chaves verificadas em compilação

- **Status:** Aceito
- **Data:** 2026-10-20

## Contexto

A comunidade conhece e gosta do padrão get/set de variáveis do ReAPI e do ReZombie
(`get_entvar`, `set_member`, `set_class_var(class, "name", ...)`). No Pawn, porém, a chave é uma string livre: erro de digitação ou tipo errado só
aparece em runtime, quando aparece.

## Decisão

- O padrão `get<Handle>Var` / `set<Handle>Var` (`getEntityVar`, `setWeaponVar`,
  `setClassVar`) é a **forma principal** de acessar propriedades na API Rook. Os nomes, e
  também as chaves (`"maxClip"`), seguem a regra camelCase do projeto
  ([ADR-0013](0013-convencao-camelcase.md)).
- Cada handle tem um **catálogo `vars`** declarado (pela engine ou por plugins de API).
  O compilador gera as funções e **verifica em compilação** que a chave literal existe,
  que o valor tem o tipo (e a unidade) certo e que a variável não é `readonly`
  ([14 §13.2](../14-linguagem-rook.md#132-catálogos-de-variáveis-vars)).
- O tipo de retorno de `get*Var` é inferido da chave.
- Acesso dinâmico por string existe só em funções separadas, que retornam
  `Result<VarValue, VarError>`.

## Alternativas consideradas

| Alternativa | Por que não |
|-------------|-------------|
| Campos/métodos (`props.health = ...`) | Mais distante do que a comunidade conhece. A verificação de tipos seria equivalente |
| Chave como enum (`setPropsVar(props, PropsVar.Health, ...)`) | Mais verboso e menos familiar. A chave literal verificada dá a mesma segurança |
| Strings sem verificação (como no Pawn) | Viola o requisito de tipagem forte |

## Consequências

- ✅ Código praticamente igual ao dos plugins atuais, com erros pegos em compilação.
- ✅ Autocompletar das chaves no LSP.
- ⚠️ O compilador precisa tratar chaves literais como parte da assinatura. Isso é
  coberto por testes de conformidade.
