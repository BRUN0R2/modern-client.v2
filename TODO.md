# TODO — Modern-Client.v2

Backlog do projeto, por marco ([docs/19-roadmap.md](docs/19-roadmap.md)).
`[x]` = concluído conforme a [definição de pronto](docs/18-testes-e-qualidade.md#definição-de-pronto). `[ ]` = pendente.

---

## Decisões pendentes

- [x] Nome do projeto: **Modern-Client.v2**
- [x] Limite de jogadores: 1 a 1024, padrão 32 ([ADR-0011](docs/adr/0011-limite-de-jogadores-1024.md))
- [x] Padrão `get*Var`/`set*Var` tipado para plugins ([ADR-0012](docs/adr/0012-padrao-get-set-vars-tipado.md))
- [x] Convenção camelCase em todo o projeto, C++ e Rook ([ADR-0013](docs/adr/0013-convencao-camelcase.md))
- [x] Arquivos, diretórios, alvos de build e scripts também em camelCase (`contractsTests.cpp`)
- [x] Framework de testes C++: GoogleTest (suporte oficial a builds sem exceções)
- [x] Licença do projeto: opção A, GPL-3.0-or-later + MIT (Rook e SDK) + CC BY 4.0 (docs) ([ADR-0010](docs/adr/0010-licenca.md))
- [x] Repositório público: https://github.com/BRUN0R2/modern-client.v2
- [ ] Nome definitivo da linguagem de plugins (provisório: **Rook**)
- [ ] Toolkit de menus: próprio ou RmlUi ([12 §2](docs/12-interface-hud-e-console.md#2-componentes-de-ui))
- [ ] Definir hardware de referência para as metas de desempenho
- [x] Conteúdo livre por padrão; assets originais opcionais e configurados localmente ([ADR-0015](docs/adr/0015-conteudo-livre-e-assets-originais-opcionais.md))
- [x] Rook Plugin Exception: permissão adicional da seção 7 da GPL para plugins e mods ([ADR-0010](docs/adr/0010-licenca.md))
- [x] SDL3 (janela, eventos, entrada, mouse raw) entra no M2, junto com o swapchain do Vulkan, o primeiro consumidor. No M1 nada usa janela nem entrada, e o port `sdl3` do vcpkg traz X11, Wayland e D-Bus para o Linux do CI sem nada para testar; o `nx.platform` do M1 tem só os relógios (2026-10-20)
- [ ] Revisão jurídica antes do primeiro release público, incluindo o texto da Rook Plugin Exception ([20](docs/20-legal-e-licenciamento.md))

---

## Documentação

- [x] Regras do projeto (`docs/RULES.md`): prioridades, princípios e padrões proibidos
- [x] Índice da documentação (`docs/README.md`)
- [x] 00 — Visão geral e limites removidos
- [x] 01 — Arquitetura em camadas, threads e ciclo de frame
- [x] 02 — Padrões de código C++26 (erros tipados, sem números mágicos)
- [x] 03 — ECS por arquétipos
- [x] 04 — Job System
- [x] 05 — Renderização (RHI, Vulkan, DirectX 11 futuro, GPU skinning)
- [x] 06 — Assets, fim do precache e limites de conteúdo removidos
- [x] 07 — Simulação, movimento e física
- [x] 08 — Rede
- [x] 09 — Servidor dedicado
- [x] 10 — Gameplay Counter-Strike
- [x] 11 — Áudio
- [x] 12 — Interface, HUD (sem truncamento de valores) e console
- [x] 13 — Sistema de plugins e mods
- [x] 14 — Linguagem Rook
- [x] 15 — API de plugins
- [x] 16 — Migração AMXX → Rook (exemplos do ReZombie)
- [x] 17 — Build, dependências e ferramentas
- [x] 18 — Testes e qualidade
- [x] 19 — Roadmap
- [x] 20 — Legal e licenciamento
- [x] Glossário
- [x] ADRs 0001–0015
- [x] Regra camelCase documentada ([02 §3.1](docs/02-padroes-de-codigo.md#31-regra-do-projeto-camelcase))
- [x] Licenças no padrão REUSE: `LICENSES/`, `LICENSE`, `REUSE.toml` e cabeçalhos SPDX
- [ ] `CONTRIBUTING.md` (fluxo de PR, DCO, padrões, licença das contribuições, como rodar testes)
- [ ] `SECURITY.md` (como reportar vulnerabilidades)
- [ ] Especificação completa da gramática Rook (`rook/spec/grammar.ebnf`)
- [ ] Catálogo completo de cvars em `data/cstrike/cvars.toml`
- [ ] Levantamento dos valores de economia e armas do original para os dados oficiais

---

## M0 — Fundação

### Repositório e build
- [x] Inicializar repositório git (`main`), `.gitignore`, `.gitattributes`, `.editorconfig`
- [x] Estrutura de pastas ([17 §1](docs/17-build-e-ferramentas.md#1-layout-do-repositório))
- [x] `CMakeLists.txt` raiz com módulos C++ e `import std`
- [x] Módulo `std` compilado pelo projeto (`nx::std`) a partir do `modules.json`, sem recursos experimentais do CMake ([ADR-0014](docs/adr/0014-modulo-std-compilado-pelo-projeto.md))
- [x] Helpers CMake: `nxAddModuleLibrary`, `nxAddTest`, política de warnings por alvo
- [x] `CMakePresets.json`: dev/release MSVC, dev/release GCC, dev Clang, ASan + UBSan, TSan
- [ ] Presets `server-headless` e `tools` (quando os alvos existirem)
- [ ] Presets Windows com GCC (MinGW-w64) e Clang
- [x] `vcpkg.json` com baseline fixa e feature `tests`
- [x] Triplets `x64-windows-nx` e `x64-linux-nx` (dependências sem exceções e sem RTTI)
- [x] `.clang-format` e `.clang-tidy` com `readability-magic-numbers` e naming como erro
- [x] Warnings como erro, ponto flutuante determinístico, sem exceções e sem RTTI
- [x] `scripts/devShell.ps1` para builds de linha de comando no Windows
- [ ] Rodar `clang-tidy` com módulos (exige build com Clang e `compile_commands.json`)
- [ ] Configurações recomendadas de editor em `.vscode/`
- [x] Eliminar a dependência do `import std` experimental e do contorno de `@cmake_cxx_std.lib`
- [x] Normalizar quebras de linha da árvore conforme o `.gitattributes`

### Toolchain
- [x] Validar MSVC 14.51 (VS 2026) com CMake 4.3.1 e 4.4.4, Debug e Release
- [x] Validar GCC 16 no Linux (GCC 16.2.0 no CI, 84 testes)
- [ ] Validar Clang no Linux
- [ ] Validar GCC (MinGW-w64) e Clang no Windows
- [x] Documentar recursos nativos e com fallback do MSVC ([17 §2.1](docs/17-build-e-ferramentas.md#21-matriz-de-compiladores))

### CI
- [x] Workflow `.github/workflows/ci.yml` (push no `main`, pull requests, sob demanda)
- [x] Job `windows-msvc`: Debug e Release com build e testes
- [x] Job `linux-gcc` (GCC 16) obrigatório
- [ ] Job Linux com Clang
- [x] Formatação (`clang-format --dry-run --Werror`) no job `checks`
- [ ] `clang-tidy` no CI (exige build com Clang e módulos)
- [ ] Job `layers` (verificação do grafo de dependências)
- [x] ThreadSanitizer em todos os testes (preset `tsan-linux-gcc` no job `linux-gcc`)
- [ ] Jobs de ASan + UBSan (com o Clang)
- [x] Quebras de linha (`git ls-files --eol`) no job `checks`
- [x] `reuse lint` no job `checks`

### `nx.core`
- [x] `nx::Failure<E>`, `nx::Result<T, E>`, `nx::fail`, causa encadeada, `nx::describe` e `std::formatter`
- [x] Contratos `nx::expects`, `nx::ensures`, `nx::invariant` com handler *enforce*/*observe*
- [ ] Handler de contratos: stack trace, `breakpoint_if_debugging` e crash report
- [x] Introspecção: `typeName`, `shortTypeName`, `enumName`, `enumCount` (fallback por assinatura)
- [ ] Introspecção pelo caminho nativo (P2996) quando um compilador suportado oferecer
- [x] `nx::Quantity` e unidades (`HammerUnits`, `UnitsPerSecond`, `HitPoints`, `ArmorPoints`, `Damage`, `Money`, `Scale`, ângulos, tempo) com literais
- [x] `nx::narrow`, `nx::tryNarrow`, `nx::truncate`, `nx::tryRound`, aritmética verificada e saturada
- [x] `EnumArray` e `Flags<E>`
- [ ] `inplace_vector` (fachada), quando o primeiro consumidor existir (ECS ou jobs)
- [x] `FrameArena` com falhas tipadas (`Exhausted`, `InvalidAlignment`)
- [ ] `PoolAllocator<T>` genérico (os chunks do ECS usam o pool de blocos do próprio `World`) e `TrackedHeapAllocator`
- [ ] Filas SPSC/MPSC sem lock com erro tipado de fila cheia, quando o primeiro consumidor existir (rede, áudio)
- [x] Log estruturado: níveis, categorias por módulo, formatação verificada em compilação, `ConsoleSink`, `RateLimit` *thread-safe*
- [ ] Sinks de arquivo e JSON lines
- [x] `nx::hash64` (XXH64 `constexpr`, verificado contra a implementação de referência) e `nx::StableId`
- [ ] BLAKE3 para hash de conteúdo (com o VFS, M2)
- [x] Testes unitários do que já existe no `nx.core` (84 testes, incluindo teste de morte do handler padrão)

### `nx.meta`
- [ ] Fachada de reflexão: membros, anotações, `enumName`
- [ ] Caminho nativo (P2996 + P3394)
- [ ] Caminho fallback (registro explícito)
- [ ] Teste que garante metadados idênticos nos dois caminhos
- [ ] Esquemas TOML derivados de structs (toml++ sem exceções) com erros por arquivo/linha

---

## M1 — Núcleo de runtime

### `nx.jobs`
- [x] Workers e deques Chase-Lev (um por prioridade em cada worker)
- [ ] Afinidade de CPU dos workers (com o `nx.platform`, se um benchmark mostrar ganho)
- [x] Fila global por prioridade (`Critical`, `Normal`, `Low`, `Background`), sem espera para produtores
- [x] Contadores com geração, dependências e continuações
- [x] Pools sem lock para descritores, contadores e continuações
- [x] `wait` com *help-while-waiting*
- [x] `parallelFor` com granularidade automática e fixa
- [x] `JobSystem::stats()` e modo serial (`workerCount = 0`)
- [ ] Comandos `jobs_stats` e `jobs_serial` (com o console)
- [ ] Zonas Tracy (com a dependência do Tracy)
- [ ] Scheduler compatível com `std::execution` (stdexec), com o streaming de assets (M2)
- [ ] Arena de rascunho por job no `JobContext`, quando o primeiro consumidor precisar
- [x] Testes, inclusive resultado idêntico com 0, 1, 3 e 15 workers, sob ThreadSanitizer no CI
- [ ] Benchmarks (`nxBenchJobs`)

### `nx.ecs`
- [x] `Entity` geracional, slots FIFO, aposentadoria por esgotamento de geração
- [x] Registro de componentes explícito com nome declarado (`ComponentTypeId`, `StableTypeId`), também para componentes de tempo de execução
- [ ] Registro de componentes por reflexão (com o `nx.meta`)
- [x] Arquétipos, chunks de 16 KiB SoA (blocos maiores para linhas maiores), grafo de arquétipos
- [x] Versões de mudança por chunk e coluna
- [x] Queries tipadas (`With`, `Without`, `Changed`, `Optional`, `Entity`)
- [ ] `Added<T>` com versão por linha, quando o primeiro consumidor existir
- [x] `forEach`, `forEachChunk`, `parallelForEachChunk`, `get`
- [x] Agendador por fases com DAG de conflitos, `before`/`after`, detecção de ciclos
- [x] Command buffers com aplicação determinística e `PendingEntity`
- [ ] Command buffers e escritores de eventos por chunk dentro de `parallelForEachChunk`
- [x] Eventos *double-buffered*
- [x] Recursos (singletons)
- [ ] Relações (pai/filho, dono) com limpeza no *sync point* (com o gameplay, M3)
- [x] Teste de determinismo do mundo com 0, 1, 3 e 15 workers
- [ ] `ent_list`, `ent_dump`, `ecs_stats`
- [ ] Benchmarks com as metas de [03 §16](docs/03-ecs.md#16-metas-de-desempenho-hardware-de-referência-a-definir-no-m1)

### Plataforma, matemática e console
- [x] `nx.platform`: relógios (monotônico, de parede, `Stopwatch`) e espera precisa por prazo para o ritmo de tick e de frame (`SleepTimer`)
- [ ] Threads do SO no `nx.platform` (nome, prioridade, afinidade), com o primeiro consumidor (thread de áudio, Tracy ou benchmark de afinidade)
- [x] `nx.math`: vetores, matrizes (`Mat3`, `Mat3x4`, `Mat4`), quaternions, ângulos GoldSrc (`angleVectors`, `vectorAngles`), AABB e planos, com ponto flutuante determinístico ([07 §6](docs/07-simulacao-e-fisica.md#6-determinismo))
- [ ] SIMD no `nx.math`, quando um benchmark mostrar ganho
- [x] Sistema de cvars tipadas (flags `Archive`, `Notify`, `Cheat`, `ServerProtected`) no `nx.console` ([12 §5.3](docs/12-interface-hud-e-console.md#53-módulo-nxconsole))
- [x] Sistema de comandos (aliases, `+`/`-`, `wait`, `exec`, `;`, aspas, `echo`, `toggle`, `incrementvar`)
- [x] Parser de `.cfg` compatível com o original
- [ ] Comandos `cvarlist`, `cmdlist` e `find` ([09 §8](docs/09-servidor-dedicado.md#8-comandos-de-console-do-servidor))
- [ ] Níveis de log por cvar (`log_level_<categoria>`, [02 §10](docs/02-padroes-de-codigo.md#10-log))
- [x] Teste de determinismo com 0, 1, 3 e 15 workers (`nxIntegrationTests`: `nx.math` em sistemas paralelos do ECS, *hash* do mundo a cada tick)

---

## M2 — Assets e mundo visível

### Assets
- [ ] VFS com montagens, prioridade, busca sem diferenciar maiúsculas e validação de caminho
- [ ] Fontes de conteúdo (`engine`, `devContent`, `openContent`, `goldsrcInstall`, mods) com flag de redistribuição
- [ ] Perfis `classic`, `open`, `dev` e `custom`, e leitura do `content.toml` (ordem: `--content-config`, diretório de trabalho, perfil do usuário)
- [ ] Detecção da instalação Steam (registro e `libraryfolders.vdf` no Windows; `~/.steam`, `~/.local/share/Steam` e Flatpak no Linux)
- [ ] Validação contra `data/content/goldsrcCstrike.toml` com `ContentError::MissingEssentialFiles`
- [ ] Assistente de primeiro start ("usar os assets originais?") que grava o `content.toml`
- [ ] Opções `--content`, `-basedir` e `--content-config`
- [ ] Comandos `content_list`, `content_scan`, `content_add`, `content_profile`
- [ ] `devContent` gerado por código: texturas em grade, mapa de teste, jogador em cápsula, armas simples, sons sintetizados
- [ ] Leitor BSP v30 (todos os lumps) com fuzzing
- [ ] Leitor WAD3 com fuzzing
- [ ] Leitor MDL v10 (incluindo `*T.mdl` e `*01.mdl`) com fuzzing
- [ ] Leitor SPR v2 com fuzzing
- [ ] Leitor WAV (PCM 8/16, cue points) com fuzzing
- [ ] Representação interna sem tetos fixos (attachments, body parts, ossos, controllers, blends) ([06 §3.4](docs/06-assets-e-recursos.md#34-limites-de-conteúdo-removidos))
- [ ] `AssetHandle<T>` geracional, estados e erros tipados
- [ ] Pipeline assíncrono (E/S → decodificação → dependências → upload)
- [ ] Orçamentos por categoria com despejo LRU
- [ ] Hot reload de assets em desenvolvimento
- [ ] `nxbake`: cache endereçado por conteúdo

### RHI e Vulkan
- [ ] `nx.platform`: janela, eventos e entrada SDL3 (port `sdl3` do vcpkg), mouse raw, junto com o swapchain ([decisão](#decisões-pendentes))
- [ ] Interface `nx.rhi` (device, queue, command list, buffers, texturas, samplers, bind groups, pipelines, queries)
- [ ] Backend Vulkan 1.3 com volk e VMA
- [ ] Swapchain, present modes, recriação e *device lost*
- [ ] Gravação paralela (command pool por worker por frame)
- [ ] Anel de staging persistente
- [ ] Pipeline cache em disco
- [ ] Pipeline de shaders Slang (SPIR-V) no build, reflexão de layouts e hot reload
- [ ] Shaders de fallback embutidos com `#embed`
- [ ] Testes de conformidade da RHI

### Render
- [ ] Matrizes de visão e projeção no `nx.math`, com as convenções de *clip space* e profundidade do renderer ([05](docs/05-renderizacao.md))
- [ ] Render graph (barreiras, aliasing, eliminação de passes, gravação paralela)
- [ ] Mundo BSP: PVS, frustum culling, agrupamento por textura e lightmap
- [ ] Atlas de lightmaps e lightstyles combinados no shader
- [ ] Céu, água, texturas animadas, transparência por máscara
- [ ] Entidades brush com transformações e render modes
- [ ] Emulação de gamma, brightness, texgamma e lightgamma
- [ ] Modelos de estúdio: avaliação de animação na CPU em jobs
- [ ] GPU skinning (vertex shader) com índices de osso por vértice e por normal
- [ ] Compute skinning opcional
- [ ] Instancing de modelos
- [ ] Visualizador de mapas e modelos com câmera livre
- [ ] Imagens de referência no CI (lavapipe)
- [ ] Todos os mapas oficiais renderizando sem erros

---

## M3 — Jogo local

### Simulação
- [ ] `GameTime` por ticks, sem degradação com uptime
- [ ] Usercmd com geração a taxa fixa (`cl_movement_rate`)
- [ ] Traces em clipnodes por hull com paridade de resultado
- [ ] Partição espacial dinâmica (árvore AABB) e vínculo com leafs
- [ ] Traces contra hitboxes animadas
- [ ] pmove em `cs.shared` como função pura
- [ ] Paridade: aceleração e atrito no chão (incluindo `edgefriction`)
- [ ] Paridade: aceleração aérea e strafe
- [ ] Paridade: pulo e penalidade de stamina
- [ ] Paridade: limitadores de bunny hop
- [ ] Paridade: agachar no chão e no ar (duck jump, ducktap)
- [ ] Paridade: degraus e rampas (surf)
- [ ] Paridade: escadas
- [ ] Paridade: água
- [ ] Paridade: dano de queda
- [ ] Tipos de movimento de entidades (`TOSS`, `BOUNCE`, `PUSH`, `FOLLOW`, `FLY`, `STEP`, `NOCLIP`)
- [ ] Think, touch e triggers determinísticos

### Jogador e armas
- [ ] Animação de jogador (sequência de tronco, gait, blends de mira)
- [ ] Viewmodel com FOV próprio
- [ ] Sistema de classes de arma (originais e customizadas no mesmo sistema)
- [ ] Máquina de estados de arma (deploy, idle, ataque, reload, holster, drop)
- [ ] Semente compartilhada de spread por comando
- [ ] Spread, recoil e dano para faca, Glock, USP, AK-47 e M4A1
- [ ] Hitgroups, colete e capacete
- [ ] Penetração por material (`materials.txt`)

### Áudio
- [ ] Mixer em thread de tempo real com fila de comandos
- [ ] Canais por entidade com substituição como no original
- [ ] Atenuação, pitch e volume com paridade
- [ ] DSP de `room_type`
- [ ] Frases (`sentences.txt`)
- [ ] Passos por material
- [ ] Música (`mp3`)
- [ ] Pool de vozes com prioridade e roubo documentado

### HUD básico
- [ ] Renderer de sprites e fontes do HUD
- [ ] Vida, colete, munição e crosshair (com cvars originais)
- [ ] Números sem truncamento: vida acima de 255, munição alta ([12 §3.4](docs/12-interface-hud-e-console.md#34-sem-truncamento-de-valores))
- [ ] Teste de regressão do HUD com vida 5000, 100000 e máximo do tipo

---

## M4 — Rede e servidor

### Rede
- [ ] Transporte UDP IPv4/IPv6 e pacotes sem conexão (`0xFFFFFFFF`)
- [ ] Handshake com cookie sem estado e identidade Ed25519
- [ ] Sessão criptografada (X25519 + HKDF + ChaCha20-Poly1305) e anti-replay
- [ ] Acks com bitfield, RTT, jitter e perda
- [ ] Controle de taxa (`rate`, `sv_minrate`, `sv_maxrate`)
- [ ] Canais e fragmentação, `net_max_message_size`
- [ ] Mensagens tipadas com serializador gerado por reflexão e `BitReader` verificado
- [ ] Snapshots delta contra baseline confirmada
- [ ] Largura de campo pelo tipo de domínio (varint, sem *wrap*) ([08 §6.1](docs/08-rede.md#61-snapshots))
- [ ] Gerenciamento de interesse (PVS, PAS, audiência de dono, distância opcional)
- [ ] Prioridade e orçamento por cliente, com métrica `net_budget_overflow`
- [ ] Snapshots montados em paralelo por cliente
- [ ] Predição e reconciliação do jogador local
- [ ] Interpolação (`cl_interp`/`ex_interp`) e jitter buffer
- [ ] Compensação de lag (`sv_unlag`, `sv_maxunlag`, `sv_unlagpush`)
- [ ] Eventos de arma preditos não reenviados ao atirador
- [ ] Voz Opus com regras de repasse
- [ ] Hash de esquema e rejeição tipada
- [ ] Loopback para listen server
- [ ] `net_graph`, `net_stats`, `net_fakelag`/`fakeloss`/`fakejitter`
- [ ] Fuzzing do decodificador

### Servidor dedicado
- [ ] `nextds` *headless* com linha de comando compatível
- [ ] `maxplayers` 1–1024 com `kMaxPlayersSupported` e recomendações acima de 64
- [ ] `server.cfg`, `listenserver.cfg`, `mapcycle.txt`, `motd.txt`, `listip.cfg`, `banned.cfg`, config por mapa
- [ ] Cvars de servidor com nomes, padrões e efeitos do original
- [ ] Comandos (`status`, `kick`, `banid`, `addip`, `changelevel`, `users`, `stats`…)
- [ ] A2S_INFO, A2S_PLAYER e A2S_RULES com desafio
- [ ] Master server `nextmaster` com heartbeat autenticado
- [ ] Descoberta de LAN
- [ ] RCON via sessão do jogo e RCON seguro TCP + TLS
- [ ] Logs no formato HLDS e JSON lines
- [ ] Desligamento gracioso e troca de mapa sem reconectar
- [ ] Endpoint de métricas
- [ ] Imagem Docker
- [ ] Download de recursos embutido e HTTP com verificação de hash
- [ ] Recusar o envio de conteúdo não redistribuível (`ResourceError::NotRedistributable`) e anunciá-lo como requisito local
- [ ] Exigência de conteúdo do servidor (`requires`) na regra A2S `content_requires`, no handshake e no server browser
- [ ] `RejectReason::MissingLocalContent` com a lista do que falta e como configurar
- [ ] Manifesto de recursos, `ResourceAdded` e política de recurso indisponível
- [ ] Consistência de arquivos (`consistency = "exact"`)
- [ ] Teste com 32 jogadores reais e estresse com 256 clientes simulados

---

## M5 — Counter-Strike completo

### Regras
- [ ] Máquina de estados de round (game commencing, freeze, running, ended)
- [ ] Todos os motivos de fim de round por tipo de mapa
- [ ] `mp_timelimit`, `mp_maxrounds`, `mp_winlimit`, intermissão e próximo mapa
- [ ] Times, personagens, `mp_limitteams`, `mp_autoteambalance`
- [ ] Economia com transação auditável e valores validados

### Objetivos
- [ ] C4: entrega, plantio, timer, defuse com e sem kit, explosão
- [ ] Reféns: seguir, parar, resgatar, penalidades
- [ ] VIP: escolta e zonas de fuga
- [ ] Escape (`es_`)

### Armas e equipamentos
- [ ] Todas as pistolas
- [ ] Escopetas (incluindo reload cartucho a cartucho)
- [ ] SMGs
- [ ] Rifles (incluindo rajada da FAMAS e silenciador da M4A1)
- [ ] Snipers (zoom de um e dois níveis)
- [ ] M249
- [ ] Granadas: HE, flashbang (cegueira por ângulo e distância), smoke
- [ ] Colete, capacete, kit de defuse, NVG, escudo tático

### Compra e comunicação
- [ ] Zonas e tempo de compra, restrições de `info_map_parameters`
- [ ] Menu de compra clássico e VGUI
- [ ] Comandos de compra, `autobuy` e `rebuy`
- [ ] Rádio (menus, comandos, ícones e sons)
- [ ] Chat (`say`, `say_team`, regras de mortos)
- [ ] Sprays

### Interface
- [ ] HUD completo (dinheiro, timer, ícones de status, radar, kill feed, barras de progresso, indicador de dano)
- [ ] Scoreboard
- [ ] Menus de time e personagem
- [ ] MOTD com HTML restrito
- [ ] Menu principal, opções e server browser
- [ ] Console completo (histórico, autocompletar, sugestões)
- [ ] Binds com nomes de tecla originais
- [ ] Localização com chaves tipadas
- [ ] Espectador (livre, chase, primeira pessoa, overview) e `mp_forcecamera`

### Mapas
- [ ] Todas as entidades de [10 §5](docs/10-gameplay-counter-strike.md#5-entidades-de-mapa)
- [ ] `data/cstrike/entities.toml` com esquemas de keyvalues
- [ ] CI carregando todos os mapas oficiais sem avisos
- [ ] Suíte de paridade de gameplay verde

---

## M6 — Rook e plugins

### Compilador `rookc`
- [ ] Lexer e parser com CST sem perdas e tolerante a erros
- [ ] Resolução de nomes, módulos e imports (incluindo `prelude`)
- [ ] Checagem de tipos com inferência local bidirecional
- [ ] Newtypes, unidades e sufixos literais
- [ ] `Option`/`Result`, `error enum` com `from` e `?`
- [ ] `match` exaustivo com padrões, guardas e tuplas
- [ ] Análise de handles efêmeros (proibição de escape e de `await`)
- [ ] Verificação de `pure fn`
- [ ] Catálogos `vars` com chaves literais tipadas e geração de `get*Var`/`set*Var`
- [ ] Forwards conhecidos com verificação de nome e assinatura
- [ ] Strings interpoladas e chaves de tradução verificadas
- [ ] Lints (`magicNumber`, `stringLookupInPrecache`, `expectInGameplay`, naming…)
- [ ] RIR (SSA tipada) e otimizações
- [ ] Geração de bytecode `.rbc` com metadados e debug info
- [ ] Compilação incremental orientada a consultas
- [ ] Diagnósticos com código, sugestão e `--explain`, em inglês e português
- [ ] Suíte de conformidade

### VM e host
- [ ] Verificador de bytecode
- [ ] Interpretador de registradores
- [ ] GC preciso e incremental por isolate
- [ ] Combustível de instruções, limites de heap e de pilha, falhas tipadas
- [ ] Chamadas entre plugins com cópia de valores e hash de assinatura
- [ ] Geração de bindings e `.rookd` a partir de anotações C++
- [ ] Forwards, hooks pré e pós, `registerHook`/`disableHook`
- [ ] Comandos com argumentos tipados e cvars tipadas
- [ ] `setTask`, coroutines (`wait`, `nextTick`, `until`) e tarefas com dono
- [ ] Componentes e sistemas ECS em Rook
- [ ] `PlayerMap` e `EntityMap` com limpeza automática
- [ ] `runJob` para `pure fn`

### Plugins e mods
- [ ] `mod.toml` e `plugins.toml` com validação
- [ ] Ordem de carga pelo grafo de dependências e detecção de ciclos
- [ ] Capabilities concedidas pelo admin
- [ ] Ciclo de vida (`pluginPrecache`, `pluginInit`, `pluginCfg`, `pluginEnd`, pausa)
- [ ] Política de falhas (`rook_fault_policy`)
- [ ] Hot reload com migração de `persist var`
- [ ] Comandos `plugins list|load|unload|reload|info|pause` e `mods list|info`

### API
- [ ] Catálogos `vars` de `Entity`, `Player`, `Weapon`, `WeaponClass` e regras do jogo
- [ ] Body groups por nome e attachments ilimitados por nome
- [ ] Todos os hooks de [15 §5](docs/15-api-de-mods.md#5-ações-hookáveis)
- [ ] Classes de arma com herança, virtuais e `callWeaponBase`
- [ ] Efeitos tipados (`nx.fx`), HUD, menus, `clientPrintColor`
- [ ] `nx.storage` (KV e SQLite) e `nx.http`
- [ ] Mensagens de rede de plugins
- [ ] Verificação de compatibilidade da API no CI

### Ferramentas
- [ ] `rook new`, `build`, `check`, `test`, `fmt`, `lint`, `doc`, `pack`
- [ ] `rookLsp` (autocompletar de chaves `vars`, diagnósticos, renomear, ir para definição)
- [ ] Depurador DAP
- [ ] Extensão VS Code
- [ ] `rook migrate` (Pawn → Rook) com relatório

### Mods embutidos
- [ ] `admin` (com `admins.toml`, permissões tipadas e importação de `users.ini`)
- [ ] `admin.menus`
- [ ] `votes` (votação e mapchooser)
- [ ] `maps` (nextmap, timeleft)
- [ ] `chat` (antiflood, mensagens)
- [ ] `stats`
- [ ] `pausecfg`

### Mod de referência
- [ ] Portar ReZombie: plugins de API (classes, subclasses, props, models, modes, game vars, players, weapons)
- [ ] Portar classes (Human, Zombie, Assassin, Nemesis, Survivor, Sniper, Fleshpound, Banshee)
- [ ] Portar modos (Infection, Nemesis, Survivor, Assassin, Sniper)
- [ ] Portar armas (Cold Steel, Drakar III, USP, AK-47) com predição (`shared`)
- [ ] Portar HUD, menus e core
- [ ] Congelar a API 1.0

---

## M7 — Paridade fina e ecossistema

- [ ] Bots com malha de navegação e importação de `.nav`
- [ ] Comandos e cvars de bot (`bot_add`, `bot_quota`, `bot_difficulty`…)
- [ ] Demos (`record`, `playdemo`, `timedemo`)
- [ ] Relay de espectadores (equivalente ao HLTV)
- [ ] Plugins de cliente com sandbox e `cl_plugins`
- [ ] Auditoria de segurança da VM no cliente
- [ ] Formato `.nxspr` (sprites RGBA de alta resolução) no `nxbake` e no renderer
- [ ] Modo `r_fidelity enhanced`
- [ ] Oclusão de servidor opcional (`sv_occlusion`)
- [ ] Formato de mapa estendido `.nxmap`, compilado a partir de mapas do TrenchBroom
- [ ] Repositório e primeiro pacote `openContent` (mapa próprio, jogador, armas, HUD e sons originais ou CC0/CC-BY)
- [ ] Estresse com 1024 jogadores e otimizações guiadas por profiling
- [ ] *Soak test* de 24 h

---

## M8 — Release 1.0 e futuro

- [ ] Backend DirectX 11 (`nx.rhi.d3d11`) com Slang → HLSL → DXBC
- [ ] Testes de conformidade da RHI no D3D11
- [ ] Servidor dedicado Linux ARM64
- [ ] Revisão jurídica e `THIRD_PARTY_NOTICES.md`
- [ ] Site de documentação
- [ ] Portal de mods
- [ ] Release 1.0
