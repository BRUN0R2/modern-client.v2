# Documentação — Modern-Client.v2

> **Status:** rascunho v0.1 · **Idioma:** pt-BR (identificadores e comentários de código em inglês)
>
> **Modern-Client.v2** é o nome do projeto. Ele não usa marcas registradas da Valve.
> "Counter-Strike" aparece nesta documentação só para descrever o jogo que está sendo
> recriado (ver [20-legal-e-licenciamento.md](20-legal-e-licenciamento.md)).

Recriação de código aberto do Counter-Strike 1.6, escrita do zero em **C++26**, com
paridade de jogabilidade com o original e **sem os limites históricos** da GoldSrc:
thread única, teto de entidades, teto de precache, teto de 32 jogadores e um ecossistema de
mods preso ao AMX Mod X/Pawn.

## Regras do projeto

[RULES.md](RULES.md) é a constituição do projeto: prioridades, princípios e regras que valem
para todo contribuidor, humano ou IA. Os documentos abaixo detalham essas regras e prevalecem
apenas dentro delas.

## Ordem de leitura recomendada

| # | Documento | Conteúdo |
|---|-----------|----------|
| 00 | [Visão geral](00-visao-geral.md) | Objetivos, não-objetivos, limites removidos, princípios |
| 01 | [Arquitetura](01-arquitetura.md) | Camadas, módulos, threads, ciclo de frame, regras de dependência |
| 02 | [Padrões de código](02-padroes-de-codigo.md) | C++26, nomenclatura, erros tipados, contratos, proibição de números mágicos |
| 03 | [ECS](03-ecs.md) | Entidades, componentes por arquétipo, queries, sistemas, agendamento |
| 04 | [Job System](04-job-system.md) | Multithreading, work-stealing, grafos de dependência, `std::execution` |
| 05 | [Renderização](05-renderizacao.md) | RHI, Vulkan, DirectX 11 (futuro), render graph, GPU skinning |
| 06 | [Assets e recursos](06-assets-e-recursos.md) | VFS, formatos GoldSrc, fim do precache, download de recursos |
| 07 | [Simulação e física](07-simulacao-e-fisica.md) | Tick, usercmd, movimento (pmove), colisão por hulls, entidades de mapa |
| 08 | [Rede](08-rede.md) | Protocolo, snapshots delta, predição, compensação de lag, interesse |
| 09 | [Servidor dedicado](09-servidor-dedicado.md) | `nextds`, cvars, comandos, RCON, A2S, master server, identidade |
| 10 | [Gameplay Counter-Strike](10-gameplay-counter-strike.md) | Rounds, economia, armas, objetivos, dados de jogo |
| 11 | [Áudio](11-audio.md) | Mixer, espacialização, DSP de salas, voz |
| 12 | [Interface, HUD e console](12-interface-hud-e-console.md) | HUD, menus, console, cvars, binds, entrada |
| 13 | [Sistema de mods](13-sistema-de-mods.md) | Runtime, isolamento, pacotes, permissões, hot reload, mods no cliente |
| 14 | [Linguagem Rook](14-linguagem-rook.md) | Especificação da linguagem de mods |
| 15 | [API de mods](15-api-de-mods.md) | Módulos `std`, `nx` e `cs`, eventos, hooks, catálogo de erros |
| 16 | [Migração AMXX → Rook](16-migracao-amxx.md) | Equivalências Pawn/AMXX e exemplos convertidos |
| 17 | [Build e ferramentas](17-build-e-ferramentas.md) | Toolchain, CMake, dependências, layout do repositório, CI |
| 18 | [Testes e qualidade](18-testes-e-qualidade.md) | Estratégia de testes, paridade, determinismo, desempenho |
| 19 | [Roadmap](19-roadmap.md) | Marcos M0–M8 e critérios de saída |
| 20 | [Legal e licenciamento](20-legal-e-licenciamento.md) | Assets da Valve, clean room, marcas, licença |
| — | [Glossário](glossario.md) | Termos GoldSrc, engine e Rook |
| — | [ADRs](adr/README.md) | Registro de decisões de arquitetura |

O backlog executável do projeto fica em [`../TODO.md`](../TODO.md).

## Convenções desta documentação

- **DEVE / NÃO DEVE / PODE** indicam requisitos normativos (no espírito da RFC 2119).
- Exemplos de código C++ e Rook são **ilustrativos**: mostram a intenção da API, não um
  contrato congelado. O contrato oficial é o código e os ADRs aceitos.
- Valores numéricos de gameplay citados como "referência" precisam ser **validados contra o
  jogo original** antes de virarem dados oficiais (ver
  [18-testes-e-qualidade.md](18-testes-e-qualidade.md#testes-de-paridade)).
- Toda decisão estrutural relevante vira um ADR em [`adr/`](adr/README.md). Se um documento
  contradisser um ADR aceito, o ADR vale e o documento precisa ser corrigido.

## Nomes usados no projeto

| Nome | O que é |
|------|---------|
| **Modern-Client.v2** | O projeto como um todo (cliente, servidor, engine, linguagem e ferramentas) |
| **Next Engine** (`nx`) | A engine: núcleo, ECS, jobs, RHI, render, áudio, rede, assets, scripting host |
| **cstrike** (`cs`) | O jogo Counter-Strike implementado sobre a engine |
| **Rook** | Linguagem de plugins ("a peça que vem depois do Pawn"). Extensão `.rook` |
| **Plugin** | Menor unidade carregável, compilada pelo `rookc`. Equivale a um `.amxx` |
| **Mod** | Pacote distribuível com plugins, assets, traduções e configs (ex.: ReZombie) |
| `next` / `nextds` | Executáveis do cliente e do servidor dedicado |
| `rookc` / `rook` | Compilador próprio e CLI da linguagem Rook |
| `nxbake` | Ferramenta de importação e cozimento de assets |
