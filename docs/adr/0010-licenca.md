# ADR-0010 — Licença do projeto

- **Status:** **Proposto** (aguarda decisão do mantenedor e revisão jurídica)
- **Data:** 2026-10-20

## Contexto

O projeto é de código aberto. Ele precisa de uma licença que proteja a engine, mas que
permita mods com qualquer licença (inclusive fechada) e seja compatível com as
dependências ([17 §5](../17-build-e-ferramentas.md#5-dependências)).

## Proposta

| Componente | Opção A (copyleft forte) | Opção B (copyleft fraco) |
|------------|--------------------------|--------------------------|
| Engine, jogo, servidor | GPL-3.0-or-later | MPL-2.0 |
| Toolchain Rook e SDK de plugins | MIT ou Apache-2.0 | MIT ou Apache-2.0 |
| Documentação | CC BY 4.0 | CC BY 4.0 |

- **Opção A**: garante que forks da engine permaneçam abertos. Exige cuidado para que a
  fronteira da API de plugins não torne mods obra derivada.
- **Opção B**: copyleft por arquivo. Mais simples para quem integra a engine, com proteção
  menor.

## Pendências

- [ ] Escolha entre A e B pelo mantenedor.
- [ ] Revisão jurídica da fronteira de plugins e da política de clean room.
- [ ] Verificação de compatibilidade de todas as dependências com a opção escolhida.
