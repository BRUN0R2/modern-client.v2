# ADR-0003 — Job System com work-stealing, sem fibers na v1

- **Status:** Aceito
- **Data:** 2026-10-20

## Contexto

O original é praticamente single-thread. O projeto quer usar todos os núcleos em
simulação em lote, animação, visibilidade, replicação por cliente, gravação de comandos de
render e streaming de assets, sem criar threads ad-hoc e sem bloqueios.

## Decisão

- Pool de workers fixo com **deques Chase-Lev** (work-stealing) e fila global por
  prioridade.
- Dependências por **contadores atômicos**: continuações são enfileiradas pelo último job
  a terminar.
- `wait` com *help-while-waiting*, nunca bloqueando um worker com trabalho disponível.
- Scheduler compatível com **`std::execution`** para fluxos assíncronos longos (assets, E/S).
- **Sem fibers** na v1 ([04 §10](../04-job-system.md#10-fora-do-escopo-da-v1)).

## Alternativas consideradas

| Alternativa | Por que não |
|-------------|-------------|
| Fibers (estilo Naughty Dog) | Complica depuração, ferramentas e `thread_local`. O benefício só aparece com muitas esperas no meio de jobs, o que o desenho evita |
| oneTBB / Taskflow | Boas opções, mas com menos controle sobre prioridades por frame, alocação por arena e integração com Tracy e determinismo. Dependência grande |
| Só `std::execution` | Overhead e ergonomia ainda imaturos para o caminho quente do frame |

## Consequências

- ✅ Previsível, medível e determinístico (saídas por índice).
- ✅ Integração moderna com senders/receivers onde faz sentido.
- ⚠️ Jobs não podem esperar no meio. O código precisa ser estruturado em fases e
  continuações. Reavaliar fibers se o profiler mostrar perda relevante.
