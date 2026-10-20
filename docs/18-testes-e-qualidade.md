# 18 — Testes e qualidade

## Pirâmide de testes

| Nível | Ferramenta | Escopo | Quando roda |
|-------|-----------|--------|-------------|
| Unitário | GoogleTest | Funções e tipos isolados (`nx.core`, parsers, ECS, matemática) | Todo PR |
| Integração | GoogleTest + servidor *headless* | Subsistemas juntos (carregar mapa, conectar cliente, round completo com bots de teste) | Todo PR |
| Plugins Rook | `rook test` | Plugins embutidos e exemplos | Todo PR |
| Conformidade Rook | Suíte em `rook/spec/` | Cada regra da linguagem e cada diagnóstico (`R0xxx`) | Todo PR |
| Determinismo | Replays | Hash de estado por tick | Todo PR |
| Render | Imagens de referência | Passes do renderer com tolerância | Todo PR |
| Paridade | Fixtures do original | Movimento, armas, economia, regras | Noturno |
| Desempenho | Benchmarks | Caminhos quentes e frame completo | Noturno |
| Fuzzing | libFuzzer | Parsers, decodificador de rede, `rookc` | Noturno |
| Estresse | Clientes simulados | 1024 jogadores, entidades em massa | Semanal |
| *Soak* | Servidor longo | 24 h+ com bots: memória estável, sem degradação de tempo | Por release |

## Testes de paridade

O objetivo é provar, com números, que o comportamento é igual ao do CS 1.6.

### Captura de referência

1. A ferramenta `tools/parityRecorder` roda cenários **contra o jogo original** (cópia
   legítima), com um plugin de servidor que registra a saída do jogo para entradas
   roteirizadas.
2. Cenários são sequências de usercmds determinísticas (ex.: "pulo com strafe para a
   esquerda por 1 s em `de_dust2` a partir do spawn X").
3. A saída gravada é **só numérica**: posição, velocidade, flags, vida, ângulos de punch e
   pontos de impacto por tick. Isso vira um *fixture* em `tests/parity/`. Nenhum asset ou
   código da Valve entra no repositório ([20](20-legal-e-licenciamento.md)).

### Execução

1. O mesmo cenário roda no Modern-Client.v2 com as mesmas entradas e cvars.
2. A comparação é campo a campo, tick a tick, com tolerância declarada por campo
   (ex.: posição ≤ 0,01 unidade).
3. Divergência gera relatório com o primeiro tick divergente, valores esperados e obtidos,
   e um gráfico da trajetória.

### Cobertura mínima

| Área | Cenários |
|------|----------|
| Movimento | Todos os itens de [07 §3.2](07-simulacao-e-fisica.md#32-comportamentos-que-devem-ser-preservados) |
| Armas | Spread por arma (amostras com sementes fixas), recoil por rajada, tempos de deploy/reload, dano por distância e hitgroup, penetração por material |
| Granadas | Trajetória, quiques, cegueira da flash por ângulo e distância, duração da fumaça |
| Economia | Recompensas por motivo de fim de round, bônus de derrota, penalidades |
| Round | Transições de estado e motivos de fim por tipo de mapa |
| Entrada | Sensibilidade em graus por contagem de mouse |

## Convenções dos testes C++

- Um executável por módulo (`nxCoreTests`). Cada caso vira um teste no CTest.
- Testes não usam exceções: as asserções fatais do GoogleTest (`ASSERT_*`) retornam da
  função de teste.
- **Contratos:** para verificar que uma checagem disparou sem abortar, o teste instala um
  handler que registra e retorna (`ContractRecorder`, modo *observe*). O handler padrão
  (*enforce*) é coberto por testes de morte (`EXPECT_DEATH`).
- Literais são permitidos em testes como dados de entrada e valores esperados. O
  `.clang-tidy` dos diretórios de teste desliga só as regras de números mágicos e as que
  conflitam com as macros do GoogleTest.

## Determinismo

- Toda partida pode ser gravada como **replay de entrada** (usercmds + sementes + config).
- Teste obrigatório: o mesmo replay com `jobs_workers` 0, 1, 3 e 15 produz **hash de
  estado idêntico a cada tick** ([04 §8](04-job-system.md#8-determinismo)).
- Bugs de gameplay são reportados com replay. O replay vira teste de regressão.

## Rede

- Testes de integração com `net_fakelag`, `net_fakeloss` e `net_fakejitter` verificam:
  predição sem erros visíveis em perda moderada, compensação de lag acertando alvos
  rebobinados, reconexão e troca de mapa.
- Testes de propriedade do codificador delta: `decode(encode(estado, base), base) == estado`
  para estados aleatórios.
- Fuzzing contínuo do decodificador com corpus de pacotes reais.

## Render

- Renderer de software (lavapipe) no CI gera imagens de cenas fixas (mapa, câmera,
  entidades), comparadas com referências por métrica perceptual com tolerância.
- Testes de conformidade da RHI rodam o mesmo conjunto em cada backend (Vulkan e, no
  futuro, D3D11).
- Testes de limites de conteúdo: modelos com dezenas de attachments, centenas de body
  parts, sprites `.nxspr` de alta resolução, vida acima de 255 no HUD
  ([06 §3.4](06-assets-e-recursos.md#34-limites-de-conteúdo-removidos),
  [12 §3.4](12-interface-hud-e-console.md#34-sem-truncamento-de-valores)).

## Desempenho

| Métrica | Onde | Limiar de regressão |
|---------|------|---------------------|
| Tempo de tick do servidor (p50/p99) com 32, 128 e 1024 jogadores | `benchServer` | 5% |
| Tempo de frame do cliente em `timedemo` | `benchClient` | 5% |
| Iteração ECS, agendador e command buffers | `nxBenchEcs` | 5% |
| Throughput do Job System | `nxBenchJobs` | 5% |
| Carregamento de mapa (cache quente) | `benchAssets` | 10% |
| Tempo de compilação Rook por 1000 linhas | `rookBench` | 10% |

Benchmarks rodam em máquina dedicada e estável. Os resultados são publicados como série
histórica.

## Sanitizers e análise

- ASan + UBSan e TSan em todo PR. MSan noturno.
- `clang-tidy` como erro ([02 §12](02-padroes-de-codigo.md#12-formatação-e-análise-estática)).
- Fuzzers com corpus versionado. Todo crash encontrado vira teste.

## Metas de cobertura

| Área | Meta de linhas |
|------|----------------|
| `nx.core`, `nx.ecs`, `nx.jobs`, `nx.net` (codificação), parsers de formato | ≥ 85% |
| `cs.shared` | ≥ 80%, mais 100% dos itens de paridade |
| `rookc` (checker) | 100% dos diagnósticos com teste de conformidade |
| Demais | ≥ 70% |

Cobertura é indicador, não objetivo: um PR não pode **reduzir** a cobertura do módulo sem
justificativa.

## Definição de pronto

Uma tarefa do [TODO](../TODO.md) só é marcada `[x]` quando:

- [ ] O código segue [02-padroes-de-codigo.md](02-padroes-de-codigo.md) e passou na revisão.
- [ ] Há testes (unitários, integração ou paridade, conforme o caso) e o CI está verde.
- [ ] Comportamento de paridade alterado tem teste de paridade.
- [ ] Caminho quente novo tem benchmark.
- [ ] Documentação atualizada (este diretório, `///` e changelog).
- [ ] Nenhum `TODO` sem referência no código.

## Política de bugs

- Todo bug corrigido ganha um **teste de regressão** que falhava antes da correção.
- Bugs de paridade levam a etiqueta `parity` e precisam de fixture.
- Crashes em produção (crash reports) são triados semanalmente.
