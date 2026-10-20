# ADR-0011 — `maxplayers` de 1 a 1024 como limite de política

- **Status:** Aceito
- **Data:** 2026-10-20

## Contexto

O original limita servidores a 32 jogadores. O pedido é manter o mínimo do original e
subir o máximo para cerca de mil. As opções discutidas foram 1000 e 1024.

## Decisão

- `maxplayers` aceita de **1 a 1024**, com padrão **32**.
- O teto é a constante nomeada `kMaxPlayersSupported = 1024`, **de política, não de
  formato**: o slot é codificado como varint e nenhum formato de rede ou arquivo depende
  do teto.
- Conjuntos de jogadores usam bitsets de exatamente 16 palavras de 64 bits.

## Alternativas consideradas

| Alternativa | Por que não |
|-------------|-------------|
| 1000 | Sobrariam 24 bits sem uso nos bitsets e valores inválidos no índice de 10 bits, com casos especiais em todo laço |
| Sem teto | Impossível validar e testar. O teto de política é testável e pode subir depois |

## Consequências

- ✅ Bitsets e índices exatos, sem casos especiais.
- ✅ Subir o teto no futuro é trocar uma constante, sem quebrar o protocolo.
- ⚠️ Banda e CPU crescem perto de O(n²). Mitigação: gerenciamento de interesse, snapshots
  por cliente em paralelo, recomendações automáticas no log e teste de estresse semanal
  ([08 §6.4](../08-rede.md#64-paralelismo-e-escala)).
- ⚠️ Campos de 1 byte do A2S saturam em 255 (limitação do formato legado, documentada).
