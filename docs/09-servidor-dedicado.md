# 09 — Servidor dedicado

## 1. Princípio

Quem administrava um HLDS de CS 1.6 deve subir um `nextds` **sem reaprender nada**:
mesmos parâmetros de linha de comando, mesmos arquivos de configuração, mesmas cvars e
comandos, o servidor aparecendo nos server browsers e funcionando com ferramentas de
monitoramento que usam A2S.

As novidades (mais jogadores, plugins Rook, RCON seguro, métricas) são **aditivas**.

## 2. Linha de comando

```bash
nextds -game cstrike -port 27015 +maxplayers 32 +map de_dust2 +sv_lan 0
```

| Parâmetro | Significado | Original |
|-----------|-------------|----------|
| `-game <dir>` | Jogo (`cstrike`) | ✅ |
| `-port <n>` | Porta UDP | ✅ |
| `-ip <addr>` | Endereço de bind | ✅ |
| `+maxplayers <n>` | Slots (1–**1024**, padrão 32) | ✅ (1–32) |
| `+map <nome>` | Mapa inicial | ✅ |
| `+<cvar> <valor>` / `+<comando>` | Executar no boot | ✅ |
| `-basedir <caminho>` | Instalação original do CS | novo |
| `-console` / `-noconsole` | Console interativo | ✅ |
| `-pidfile <arquivo>` | PID para supervisores | ✅ (ReHLDS) |
| `-config <arquivo.toml>` | Configuração nova estruturada | novo |
| `-tickrate <n>` | Atalho para `sv_tickrate` | novo |

## 3. Jogadores: de 1 a 1024

- `maxplayers` aceita de **1** (mínimo, como no original) até **1024**
  (`kMaxPlayersSupported`). O padrão continua **32**.
- O teto de 1024 é **de política, não de formato**. O slot do cliente é codificado como
  varint, e conjuntos de jogadores são bitsets de 16 palavras de 64 bits. Subir o teto no
  futuro é uma mudança de constante, sem quebrar o protocolo.
- Acima de 64 jogadores, o servidor emite recomendações no log ao iniciar
  (`sv_maxupdaterate`, `sv_relevance_distance`, tamanho do mapa), porque banda e CPU
  crescem perto de O(n²).

| Faixa | Status |
|-------|--------|
| 1–32 | Paridade com o original (validado continuamente) |
| 33–128 | Uso comum previsto (testado no CI de desempenho) |
| 129–1024 | Suportado, com teste de estresse por marco |

## 4. Arquivos de configuração

| Arquivo | Função | Compatível com o original |
|---------|--------|---------------------------|
| `cstrike/server.cfg` | Executado ao iniciar cada mapa | ✅ |
| `cstrike/listenserver.cfg` | Para listen server | ✅ |
| `cstrike/mapcycle.txt` | Rotação de mapas | ✅ |
| `cstrike/motd.txt` | Mensagem do dia (texto ou HTML seguro) | ✅ (HTML restrito, [12](12-interface-hud-e-console.md)) |
| `cstrike/listip.cfg` / `banned.cfg` | Banimentos por IP e por identidade | ✅ (identidade substitui SteamID) |
| `cstrike/maps/<mapa>.cfg` | Config por mapa | ✅ (comportamento comum de plugins) |
| `server/plugins.toml` | Plugins e mods habilitados e ordem ([13](13-sistema-de-mods.md)) | novo (substitui `plugins.ini`) |
| `server/admins.toml` | Administradores e permissões | novo (substitui `users.ini`) |
| `server/masterServers.toml` | Master servers anunciados | novo |
| `server/server.toml` | Configuração estruturada opcional (equivalente às cvars) | novo |

Erros em qualquer arquivo (cvar desconhecida, valor fora do intervalo, chave TOML
inválida) aparecem no console com arquivo e linha. Não existe falha silenciosa.

## 5. Consultas A2S e server browser

- Respostas compatíveis com o protocolo de consulta da Valve, para que GameTracker, bots
  de Discord e browsers de terceiros continuem funcionando:
  - `A2S_INFO`: nome, mapa, jogo, jogadores, máximo, bots, tipo, SO, senha, VAC (sempre
    falso) e versão;
  - `A2S_PLAYER`: nome, frags e tempo conectado;
  - `A2S_RULES`: cvars marcadas `Notify`;
  - **desafio** (`challenge`) obrigatório em todas, como a Valve passou a exigir, contra
    amplificação.
- Com mais de 255 jogadores, campos de 1 byte do A2S saturam em 255 e o valor real
  segue em uma regra `players_real` e no protocolo próprio. Isso fica documentado como
  limitação do formato legado.
- **Master server próprio**, de código aberto (`nextmaster`), com protocolo de heartbeat
  autenticado. Servidores anunciam para todos os masters de `masterServers.toml`.
- **LAN:** descoberta por broadcast na porta padrão (`sv_lan 1` restringe a LAN).

## 6. Cvars

### 6.1 Compatibilidade

Todas as cvars de servidor relevantes do CS 1.6 existem com **mesmo nome, mesmo padrão e
mesmo efeito**. Lista inicial (o catálogo completo vive em `data/cstrike/cvars.toml`):

| Grupo | Cvars |
|-------|-------|
| Round e partida | `mp_roundtime`, `mp_freezetime`, `mp_buytime`, `mp_c4timer`, `mp_timelimit`, `mp_maxrounds`, `mp_winlimit`, `mp_fraglimit`, `sv_restart`, `sv_restartround`, `mp_chattime` |
| Economia | `mp_startmoney` |
| Times | `mp_autoteambalance`, `mp_limitteams`, `mp_autokick`, `mp_tkpunish`, `mp_friendlyfire`, `mp_hostagepenalty` |
| Espectador | `mp_forcecamera`, `mp_forcechasecam`, `mp_fadetoblack`, `allow_spectators` |
| Comunicação | `sv_alltalk`, `sv_voiceenable`, `mp_flashlight`, `mp_footsteps`, `decalfrequency`, `mp_logmessages` |
| Física | `sv_gravity`, `sv_friction`, `sv_accelerate`, `sv_airaccelerate`, `sv_stopspeed`, `sv_maxspeed`, `sv_stepsize`, `edgefriction`, `sv_bounce`, `sv_maxvelocity` |
| Rede | `sv_maxrate`, `sv_minrate`, `sv_maxupdaterate`, `sv_minupdaterate`, `sv_unlag`, `sv_maxunlag`, `sv_unlagpush`, `sv_timeout` |
| Servidor | `hostname`, `sv_password`, `rcon_password`, `sv_lan`, `sv_region`, `sv_contact`, `sv_cheats`, `pausable`, `sv_allowdownload`, `sv_allowupload`, `sv_downloadurl`, `mp_consistency` |
| Logs | `log on/off`, `sv_logfile`, `sv_logecho`, `mp_logdetail` |

### 6.2 Cvars novas (prefixo próprio quando não há equivalente)

| Cvar | Padrão | Função |
|------|--------|--------|
| `sv_tickrate` | 100 | Frequência da simulação |
| `sv_relevance_distance` | 0 (desligado) | Distância máxima de relevância |
| `sv_download_rate` | conservador | Banda de download embutido por cliente |
| `rook_budget_ms` | ver [13](13-sistema-de-mods.md) | Orçamento de CPU por callback de plugin |
| `rcon_secure_port` | 0 (desligado) | Porta do RCON seguro (TCP + TLS) |
| `metrics_port` | 0 (desligado) | Endpoint de métricas (Prometheus) |

### 6.3 Tipagem das cvars

Cvars são **tipadas** (`bool`, inteiro com faixa, float com faixa, string, enum), mas
aceitam a sintaxe textual clássica no console. Valor inválido é recusado com mensagem
clara (`mp_freezetime: expected seconds in [0, 60], got "abc"`). Não há conversão
silenciosa para 0.

## 7. Identidade de jogadores

O original usava SteamID. Sem Steam, cada jogador tem uma **identidade criptográfica**:

- Par de chaves Ed25519 gerado na primeira execução e guardado no perfil do usuário.
- `PlayerId` = impressão digital da chave pública, exibida como
  `NX:XXXX-XXXX-XXXX-XXXX`.
- O handshake prova a posse da chave ([08 §4](08-rede.md#41-handshake)), então ninguém
  personifica outro jogador.
- Bans, admins e estatísticas usam `PlayerId`. Bans por IP continuam disponíveis.
- Limitação conhecida: o jogador pode gerar outra identidade (como em servidores
  *non-steam*). Mitigações: ban por IP, reputação por servidor e, no futuro, provedores de
  identidade opcionais.

## 8. Comandos de console do servidor

| Comando | Função |
|---------|--------|
| `status` | Lista jogadores (slot, nome, `PlayerId`, ping, perda, IP) |
| `users`, `listid`, `listip` | Listas de usuários e bans |
| `kick <#userid\|nome>`, `banid <minutos> <id>`, `addip <minutos> <ip>`, `removeid`, `removeip`, `writeid`, `writeip` | Moderação (sintaxe original) |
| `changelevel <mapa>`, `map <mapa>`, `restart` | Mapas |
| `say <texto>` | Mensagem do servidor |
| `exec <cfg>`, `cvarlist`, `cmdlist`, `find <texto>` | Configuração |
| `stats` | CPU, tick, banda, entidades |
| `quit` / `exit` | Encerrar |
| `plugins list\|load\|unload\|reload\|info` | Plugins Rook ([13](13-sistema-de-mods.md)) |
| `mods list\|info` | Mods instalados |

## 9. RCON

| Modo | Uso | Padrão |
|------|-----|--------|
| `rcon` no cliente do jogo | Via sessão criptografada do jogo, com `rcon_password` | Habilitado se `rcon_password` estiver definido |
| RCON seguro TCP + TLS | Ferramentas externas e painéis de hospedagem | Desligado (`rcon_secure_port`) |
| RCON legado UDP em texto puro | Ferramentas antigas | **Removido** por segurança. Ferramentas legadas usam um adaptador |

Tentativas de senha incorretas sofrem bloqueio progressivo por IP (`rcon_ban_after`,
`rcon_ban_minutes`), registrado em log.

## 10. Logs

- Formato de log compatível com o do HLDS para eventos de jogo (`L 10/20/2026 - 21:03:11:
  "nome<2><NX:...><CT>" killed ...`), para que parsers de estatística (ex.: HLstatsX)
  funcionem com pouca ou nenhuma adaptação.
- Log estruturado adicional em JSON lines (`log_format json`) para observabilidade
  moderna.

## 11. Operação

- **Desligamento gracioso:** `SIGTERM`/`quit` avisa os jogadores, salva o estado
  persistente dos plugins e fecha os sockets.
- **Troca de mapa sem reconectar** os clientes (como o original), com download incremental
  dos recursos novos.
- **Hot reload de plugins** sem troca de mapa (`plugins reload <id>`).
- **Métricas:** tick p50/p99, jogadores, banda, memória por plugin, filas de jobs.
- **Docker:** imagem oficial do `nextds` Linux. A instalação original é montada como
  volume somente leitura.

## 12. Erros tipados

| Enum | Exemplos |
|------|----------|
| `ServerError` | `PortInUse`, `MapNotFound`, `InvalidMaxPlayers{value, range}`, `ConfigInvalid` |
| `CvarError` | `Unknown`, `ReadOnly`, `OutOfRange`, `InvalidValue`, `CheatProtected` |
| `RconError` | `BadPassword`, `Locked{until}`, `Disabled` |
| `MasterError` | `Unreachable`, `Rejected`, `HeartbeatTimeout` |
