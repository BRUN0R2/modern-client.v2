# ADR-0002 — ECS próprio por arquétipos

- **Status:** Aceito
- **Data:** 2026-10-20

## Contexto

A GoldSrc usa *edicts* com teto fixo e estruturas monolíticas (`entvars_t`), e a lógica de
entidades roda em uma única thread. O projeto precisa de entidades sem teto, iteração
eficiente em cache, paralelismo seguro, determinismo para replays e testes, integração
com replicação delta e extensão por plugins em tempo de execução.

## Decisão

Implementar um **ECS próprio** baseado em **arquétipos e chunks de 16 KiB (SoA)**, com
handles de 64 bits (índice + geração), queries tipadas, agendador que deriva paralelismo
do acesso declarado, command buffers com aplicação determinística e versões de mudança
por chunk e coluna ([03](../03-ecs.md)).

## Alternativas consideradas

| Alternativa | Por que não |
|-------------|-------------|
| EnTT (sparse sets) | Excelente, mas a iteração multi-componente é menos amigável a cache. Integração com agendador, replicação e componentes de plugins exigiria camadas por cima |
| flecs | Muito completo, mas API em C com conceitos próprios (relations, pipelines) e menos controle sobre determinismo e layout para a rede |
| Modelo de entidades orientado a objetos (como o original) | Sem paralelismo seguro, fragmentação de memória e acoplamento |

## Consequências

- ✅ Controle total sobre layout, determinismo, versões de mudança e integração com
  replicação e plugins Rook.
- ✅ Sem teto de entidades.
- ⚠️ Custo de implementação e manutenção. Mitigação: escopo mínimo no M1, benchmarks e
  testes extensivos.
- ⚠️ Mudanças estruturais frequentes custam movimentação entre arquétipos. Mitigação:
  tags com parcimônia e medição.
