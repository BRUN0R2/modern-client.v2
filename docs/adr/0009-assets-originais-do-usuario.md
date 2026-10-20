# ADR-0009 — Assets originais lidos da instalação do usuário

- **Status:** Aceito
- **Data:** 2026-10-20

## Contexto

Recriar o jogo "igualmente ao original" exige os mapas, modelos, sons e sprites do CS 1.6,
que são propriedade da Valve e não podem ser redistribuídos.

## Decisão

- O jogo **lê diretamente** a instalação legítima do usuário (Steam), detectada
  automaticamente ou configurada, montada no VFS com prioridade abaixo de mods e downloads
  ([06](../06-assets-e-recursos.md)).
- Os formatos originais (BSP v30, MDL v10, WAD3, SPR, WAV) são lidos sem conversão
  obrigatória.
- O repositório e os binários **não contêm** assets da Valve. Testes usam assets
  sintéticos e dados numéricos de paridade.

## Alternativas consideradas

| Alternativa | Por que não |
|-------------|-------------|
| Distribuir os assets | Violação de direitos autorais |
| Exigir conversão prévia para formato próprio | Atrito para o usuário e risco de divergência visual |
| Só assets livres | Inviabiliza a paridade agora. Fica como projeto futuro da comunidade |

## Consequências

- ✅ Paridade visual e sonora total, legalmente.
- ✅ Mapas da comunidade funcionam sem conversão.
- ⚠️ Exige o jogo original instalado. Documentado no primeiro start com assistente.
