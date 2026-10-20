# ADR-0008 — Protocolo de rede próprio, sem compatibilidade com o protocolo 48

- **Status:** Aceito
- **Data:** 2026-10-20

## Contexto

O protocolo 48 da GoldSrc embute os limites que o projeto quer remover: índices de
entidade e de precache com poucos bits, mensagens de usuário pequenas, campos de HUD de 1
byte e teto de entidades por pacote. Também não tem criptografia e usa RCON em texto puro.
Ao mesmo tempo, os servidores precisam "funcionar como o antigo": cvars, comandos,
consultas A2S e server browsers.

## Decisão

- **Protocolo próprio** sobre UDP, com handshake por cookie sem estado, sessões
  criptografadas (libsodium), canais confiável/não confiável, fragmentação, snapshots delta
  gerados por reflexão, gerenciamento de interesse e orçamento por cliente
  ([08](../08-rede.md)).
- **Compatibilidade operacional** com o original: mesmas cvars de rede, predição,
  interpolação e compensação de lag, consultas **A2S** na mesma porta e logs no formato
  HLDS.
- **Sem** compatibilidade com clientes ou servidores GoldSrc.

## Alternativas consideradas

| Alternativa | Por que não |
|-------------|-------------|
| Implementar o protocolo 48 | Herdaria todos os limites e a insegurança, e exigiria engenharia reversa de partes não documentadas |
| GameNetworkingSockets | Sólida, mas assume o controle do socket (dificulta A2S e pacotes sem conexão na mesma porta) e traz dependências grandes |
| ENet / yojimbo | Úteis, mas o núcleo de valor (replicação, interesse, compensação) é próprio de qualquer forma, e o transporte é pequeno |

## Consequências

- ✅ Sem tetos de entidades, recursos e mensagens, e seguro por padrão.
- ✅ Ferramentas de monitoramento continuam funcionando via A2S.
- ⚠️ Clientes CS 1.6 originais não conectam (aceito como não-objetivo).
- ⚠️ Transporte e criptografia exigem cuidado. Mitigação: libsodium, fuzzing e revisão de
  segurança antes do release.
