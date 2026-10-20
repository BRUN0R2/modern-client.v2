# 00 — Visão geral

## 1. Propósito

**Modern-Client.v2** recria o Counter-Strike 1.6 do zero, em código aberto, com **comportamento idêntico ao
original** do ponto de vista de quem joga e de quem administra servidores. Por baixo, a
engine é moderna, paralela e sem os tetos fixos da GoldSrc.

O projeto tem três pilares:

1. **Paridade:** movimento, armas, economia, rounds, mapas, HUD, console, cvars e servidores
   se comportam como no CS 1.6. Um jogador veterano não deve "sentir" diferença, a não ser
   por mais estabilidade e desempenho.
2. **Sem limites históricos:** entidades, precache, jogadores, mensagens, mods e threads
   deixam de ter tetos arbitrários embutidos no formato ou no protocolo.
3. **Plugins e mods de primeira classe:** o modelo que a comunidade já conhece é mantido
   (plugins carregados pelo servidor, agrupados em mods), mas com tecnologia nova. Os
   plugins são escritos em **Rook**, uma linguagem nova, fortemente tipada e de sintaxe
   familiar a quem vem do AMX Mod X/Pawn, compilada por um **compilador próprio e
   moderno** (`rookc`), com API estável, versionada e desacoplada da engine.

## 2. Limites do original e como o projeto os remove

| Limite no CS 1.6 / GoldSrc | Consequência | Abordagem no Modern-Client.v2 |
|----------------------------|--------------|-------------------|
| Código legado (Quake/GoldSrc, C/C++98) | Difícil de manter, inseguro, acoplado | C++26 do zero, módulos, erros tipados, contratos, testes ([02](02-padroes-de-codigo.md)) |
| Thread única para simulação, render e rede | CPU moderna ociosa, picos de frame | Job System com work-stealing e ECS paralelo por chunks ([04](04-job-system.md), [03](03-ecs.md)) |
| Tetos de entidades (edicts e entidades visíveis por pacote, como o famoso 512) | Mapas e mods grandes quebram com "no free edicts" ou entidades somem | IDs de entidade de 32 bits com geração, sem teto estrutural; rede com gerenciamento de interesse e orçamento por cliente ([03](03-ecs.md), [08](08-rede.md)) |
| Precache limitado (modelos, sons e genéricos em torno de 512 cada) | Mods precisam escolher quais recursos carregar | Registro de recursos sem teto, carregamento sob demanda e download incremental ([06](06-assets-e-recursos.md)) |
| Máximo de 32 jogadores | Modos grandes impossíveis | `maxplayers` de 1 a **1024** (padrão 32), teto de política e não de formato ([09](09-servidor-dedicado.md)) |
| Mensagens de usuário pequenas (≈192 bytes) | HUDs e menus de mods truncados | Mensagens tipadas de qualquer tamanho com fragmentação e orçamento ([08](08-rede.md)) |
| Limites de conteúdo: 4 attachments por entidade, `body` empacotado em poucos bits, sprites paletados de 256 cores com tamanho limitado, ossos, vértices, controllers e blends com tetos fixos | Armas e modelos de mods limitados, HUD e efeitos de baixa qualidade | Estruturas dinâmicas, attachments por nome, body groups por parte, sprites RGBA de alta resolução (`.nxspr`) ([06 §3.4](06-assets-e-recursos.md#34-limites-de-conteúdo-removidos)) |
| Campos de HUD e de rede em 1 byte (vida, munição, clip…) | Vida acima de 255 aparece errada no HUD (ex.: zumbis e chefes de mods) | Valores com os tipos de domínio do jogo (`Health`, `Armor`, `Money`), sem teto artificial, codificados como varint, e HUD sem limite de dígitos ([12](12-interface-hud-e-console.md#34-sem-truncamento-de-valores)) |
| AMXX/Pawn: célula de 32 bits sem tipo, tags fracas, sem structs, strings em buffers fixos | Bugs silenciosos, código verboso | Rook: tipos nominais, structs, enums com dados, `Result`, unidades de medida ([14](14-linguagem-rook.md)) |
| Mods acoplados a offsets de memória (pev, private data, Orpheu) | Quebram a cada atualização | API declarativa gerada por reflexão a partir da engine, versionada por semver ([15](15-api-de-mods.md)) |
| Mods só no servidor, sem predição | Armas customizadas "travadas" com lag | Código Rook `shared` roda no servidor e no cliente com sandbox e predição ([13](13-sistema-de-mods.md)) |
| Recarregar plugin exige troca de mapa | Desenvolvimento lento | Hot reload com migração de estado tipada ([13](13-sistema-de-mods.md)) |

## 3. Objetivos

- **G1 — Paridade de jogabilidade:** reproduzir fielmente o CS 1.6, versão Steam mais
  recente, incluindo movimento (bunny hop com penalidade, duck jump, strafe, surf, escadas),
  balística, spread, recoil e penetração.
- **G2 — Paridade de servidor:** quem administra servidores reconhece tudo:
  `server.cfg`, `mapcycle.txt`, `motd.txt`, cvars `mp_*`/`sv_*`, `rcon`, `status`, `kick`,
  `banid`, parâmetros de linha de comando, server browser e consultas A2S.
- **G3 — Escala:** de 1 a 1024 jogadores, dezenas de milhares de entidades e recursos
  ilimitados. A degradação é previsível e configurável, sem crash nem limite oculto.
- **G4 — Desempenho:** usar todos os núcleos disponíveis, com frame time estável e
  latência de entrada mínima.
- **G5 — Mods nativos:** linguagem Rook, API completa e tipada, ferramentas (LSP,
  depurador, formatador, testes) e hot reload.
- **G6 — Qualidade de engenharia:** módulos desacoplados, erros tipados, nada de números
  mágicos, testes automatizados, CI em Windows e Linux.

## 4. Não-objetivos (por enquanto)

- **Compatibilidade binária com a GoldSrc:** o protocolo 48 e os clientes/servidores
  originais não são compatíveis com o Modern-Client.v2. Os limites do original estão embutidos nesse
  protocolo (ver [ADR-0008](adr/0008-protocolo-de-rede-proprio.md)).
- **Carregar `.amxx`, `mp.dll`, `client.dll` ou plugins Metamod:** mods são migrados para
  Rook ([16](16-migracao-amxx.md)). Ferramentas de conversão assistida podem existir.
- **Distribuir assets da Valve:** o jogo funciona com conteúdo livre e, se o usuário tiver
  o CS 1.6, usa a instalação local dele, configurada quase automaticamente
  ([ADR-0015](adr/0015-conteudo-livre-e-assets-originais-opcionais.md)).
- **Remasterização visual obrigatória:** o visual padrão é o original. Melhorias são
  opcionais, por cvar (`r_fidelity`).
- **Anti-cheat em kernel.** A segurança se baseia em servidor autoritativo e validação.

## 5. Princípios

1. **Paridade primeiro, extensão depois.** Toda funcionalidade nova precisa preservar o
   comportamento original quando configurada com os valores padrão.
2. **Sem limites arbitrários.** Quando um limite existir (memória, banda, segurança), ele
   é nomeado, configurável e documentado, e ao ser atingido gera um erro tipado e
   observável.
3. **Explícito em vez de mágico.** Sem fallback silencioso, sem estado global escondido,
   sem números mágicos. Os valores de gameplay ficam em dados versionados.
4. **APIs desacopladas.** Módulos dependem de interfaces, nunca de detalhes internos. A API
   de mods é uma fachada estável sobre a engine.
5. **Erros são valores.** Falhas esperadas são tipos (`std::expected` no C++, `Result` no
   Rook). Violações de invariantes são contratos.
6. **Determinismo onde importa.** Mesma build e mesmas entradas geram a mesma simulação, o
   que viabiliza replays, testes de paridade e depuração.
7. **Paralelo por padrão, serial por decisão.** Trabalho em lote roda em jobs. Regras de
   jogo com ordem significativa rodam em ordem fixa e documentada.
8. **Evolução em passos pequenos e completos.** Cada marco entrega algo jogável e testado
   ([19](19-roadmap.md)).

## 6. Públicos e casos de uso

| Público | Caso de uso principal |
|---------|-----------------------|
| Jogador | Jogar CS 1.6 com a mesma sensação, em hardware e sistemas modernos |
| Administrador de servidor | Subir `nextds` com as mesmas configs e comandos de sempre |
| Desenvolvedor de mods | Escrever mods em Rook com tipagem forte, hot reload e depurador |
| Mapper | Usar os BSPs existentes e compiladores atuais (ZHLT/VHLT) sem mudar o fluxo |
| Contribuidor da engine | Trabalhar em módulos isolados e bem testados de C++26 |

## 7. Plataformas alvo

| Plataforma | Status |
|------------|--------|
| Windows 10/11 x64 | Alvo primário desde o M0 |
| Linux x64 (glibc) | Alvo primário desde o M0 (cliente e servidor dedicado) |
| Servidor dedicado Linux ARM64 | Desejável após o M4 |
| macOS (MoltenVK) | Futuro, não planejado |

| API gráfica | Status |
|-------------|--------|
| Vulkan 1.3 | Backend primário |
| DirectX 11 | Futuro (M8). A RHI é desenhada para suportá-lo desde o início |

## 8. Métricas de sucesso

- Um jogador veterano não distingue o movimento do original em teste cego (bhop, strafe,
  recoil, spread).
- `de_dust2` com 32 jogadores roda com tick de 100 Hz usando uma fração de um núcleo no
  servidor.
- Teste de estresse com 1024 jogadores simulados sem crash, com degradação controlada pelo
  orçamento de rede.
- O ReZombie (mod de referência) portado para Rook fica menor, mais legível e sem casts.
- Zero vazamentos e zero data races nos testes com sanitizers.
