# 07 — Simulação, movimento e física

## 1. Tempo e tick

| Conceito | Definição |
|----------|-----------|
| `sv_tickrate` | Frequência fixa da simulação do servidor. Padrão **100 Hz** (paridade com servidores clássicos) |
| `GameTime` | Contador inteiro de ticks mais fração. Sem perda de precisão com uptime longo (a GoldSrc usava `float` em segundos, que degrada após horas) |
| `TickTime::delta` | Duração de um tick (`1 / sv_tickrate`) como `nx::Seconds` |
| Usercmd | Comando de entrada do cliente com duração própria (`msec`), aplicado individualmente |

O servidor avança entidades "de mundo" (granadas, portas, triggers, timers) por tick.
**Jogadores avançam por usercmd**, como no original, com a duração de cada comando. Isso
preserva o comportamento do movimento original.

## 2. Comandos de usuário

### 2.1 Estrutura

```cpp
export namespace cs {

struct UserCommand {
    CommandNumber     number;        // monotonically increasing per client
    nx::Milliseconds  duration;      // "msec" in GoldSrc
    nx::EulerAngles   viewAngles;
    MoveIntent        move;          // forward, side, up (named, quantized)
    nx::Flags<Button> buttons;       // Attack, Jump, Duck, Use, Attack2, Reload, Score, ...
    Impulse           impulse;       // flashlight, spray, ...
    WeaponSelection   weaponSelect;
    nx::Milliseconds  interpolation; // client lerp, used by lag compensation
};

} // namespace cs
```

### 2.2 Geração no cliente

- **Taxa de geração fixa:** o cliente gera usercmds a `cl_movement_rate` Hz (padrão
  **100**, ou seja, `msec = 10`), **independentemente do FPS de render**. Isso reproduz a
  configuração competitiva clássica (`fps_max 100`) em qualquer máquina e elimina
  diferenças de física por FPS.
- Para compatibilidade com quem quer o comportamento antigo, `cl_movement_rate 0` gera um
  usercmd por frame de render com `msec` do frame. Fica marcado como modo legado e não é
  recomendado.
- `cl_cmdrate` controla quantos pacotes de comando por segundo são enviados. Cada pacote
  carrega os comandos novos mais `cl_cmdbackup` comandos anteriores (redundância contra
  perda), como no original.
- O servidor valida a soma de `duration` por janela de tempo, contra *speedhack*
  (`sv_max_command_time_drift`). O excesso é descartado com métrica por cliente.

### 2.3 Semente compartilhada

Spread e outros aleatórios de arma usam uma **semente derivada do número do comando**,
igual no cliente e no servidor. Assim a predição de spread no cliente bate com o servidor,
como no original.

## 3. Movimento do jogador (pmove)

O movimento é a parte mais sensível da paridade. Ele vive em `cs.shared` e é **o mesmo
código** no servidor (autoritativo) e no cliente (predição).

### 3.1 Requisitos de pureza

```cpp
[[nodiscard]] auto simulateMovement(const MovementState& state,
                                    const UserCommand& command,
                                    const MovementSettings& settings,
                                    const CollisionWorld& collision) -> MovementResult;
```

- Função pura: o resultado depende só dos argumentos. Nenhum acesso a estado global,
  relógio ou estado exclusivo do servidor.
- `MovementSettings` contém todas as cvars de movimento (`sv_gravity`, `sv_friction`,
  `sv_accelerate`, `sv_airaccelerate`, `sv_stopspeed`, `sv_maxspeed`, `sv_stepsize`,
  `edgefriction`, `sv_bounce`, `sv_maxvelocity`, `sv_wateraccelerate`, `sv_waterfriction`),
  replicadas para o cliente.

### 3.2 Comportamentos que DEVEM ser preservados

Cada item abaixo tem um teste de paridade dedicado ([18](18-testes-e-qualidade.md#testes-de-paridade)):

| Comportamento | Observação |
|---------------|------------|
| Aceleração no chão e atrito | Incluindo `edgefriction` perto de bordas e `stopspeed` |
| Aceleração aérea com teto de velocidade desejada | Base do *air strafe*, *strafe jump* e *surf* |
| Pulo e altura de pulo | Velocidade inicial derivada de gravidade e altura, não um literal |
| Penalidade de velocidade após pulo (stamina) | Característica do CS 1.6 que limita o *bunny hop* |
| Limitadores de *bunny hop* do original | Reproduzir exatamente o original, conforme o teste revelar |
| Agachar no chão (transição) e no ar (instantâneo) | *Duck jump*, *ducktap* |
| Subida de degraus (`sv_stepsize`) | Inclui interação com rampas |
| Rampas íngremes | Normal limite para "chão". Abaixo dela o jogador desliza (surf) |
| Escadas (`func_ladder`) | Velocidade, saída, pulo da escada |
| Água (níveis 0–3) | Natação, afundar, sair da água |
| Dano de queda | Limiares e escala de dano do original |
| Velocidade máxima por arma | Dado de arma (faca, pistolas, AWP com zoom etc.) |
| Empurrar e ser empurrado por movers | Portas e plataformas (`MOVETYPE_PUSH`) |
| *Edgebug* e quirks de colisão | Consequência natural de reproduzir traces e clipping com fidelidade |
| *Noclip*, *spectator* | Para depuração e observadores |

**Política:** quando houver dúvida entre "corrigir" um comportamento estranho e
reproduzi-lo, **reproduz-se** e documenta-se com `// Parity:`. Correções ficam atrás de
cvars explícitas com padrão desligado.

### 3.3 Hulls

| Hull | Uso | Dimensões |
|------|-----|-----------|
| 0 — ponto | Balas, traces de linha | Ponto |
| 1 — em pé | Jogador em pé | Caixa padrão de jogador GoldSrc |
| 2 — grande | Entidades grandes | Caixa grande GoldSrc |
| 3 — agachado | Jogador agachado | Caixa de jogador agachado GoldSrc |

As dimensões exatas ficam em `data/cstrike/hulls.toml` e **DEVEM** bater com os hulls
compilados no BSP, porque as árvores de clipnodes de cada hull vêm do compilador de mapas.

## 4. Colisão

### 4.1 Mundo

- Traces contra as **árvores de clipnodes** do BSP por hull, com os mesmos resultados de
  `fraction`, `endpos`, plano, `startsolid`/`allsolid` e conteúdo (água, lava, sólido) do
  original.
- Submodelos de entidades brush (portas, elevadores) usam os próprios hulls com
  transformação da entidade.

### 4.2 Entidades

- Partição espacial dinâmica (árvore AABB) para entidades móveis, mais vínculo com leafs
  do BSP para PVS/PAS.
- Jogadores colidem entre si com a caixa do hull.
- Traces de bala testam **hitboxes** do modelo de estúdio (ossos animados) para obter o
  *hitgroup*.

### 4.3 API

```cpp
auto hit = collision.trace(nx::collision::TraceQuery{
    .start  = muzzle,
    .end    = muzzle + direction * weapon.range,
    .hull   = nx::collision::Hull::Point,
    .mask   = nx::collision::ContentMask::shot(),
    .filter = nx::collision::TraceFilter::ignoring(shooter),
    .detail = nx::collision::TraceDetail::Hitboxes,
});

if (hit.entity) {
    applyBulletDamage(*hit.entity, hit.hitGroup, hit.position, hit.surfaceMaterial);
}
```

- `TraceResult` sempre tem `fraction`, `position`, `normal` e `contents`. Os campos
  opcionais (`entity`, `hitGroup`, `surfaceMaterial`) são `std::optional`.
- Traces são *thread-safe* contra um **snapshot de colisão imutável** do tick. Isso permite
  traces em jobs paralelos (ex.: compensação de lag, IA de bots).

## 5. Física de entidades não-jogador

### 5.1 Tipos de movimento

| Tipo (GoldSrc) | Uso no CS | Componente |
|----------------|-----------|------------|
| `NONE` | Entidades estáticas | — |
| `TOSS` | Armas caídas, C4 no chão | `Ballistic{gravity}` |
| `BOUNCE` | Granadas | `Ballistic{gravity, elasticity}` |
| `PUSH` | Portas, plataformas, trens | `Mover{path, speed, blockedPolicy}` |
| `FOLLOW` | Armas no modelo do jogador | `Parent` + anexo |
| `FLY`, `FLYMISSILE` | Projéteis de mods | `Ballistic{gravity = 0}` |
| `STEP` | Reféns/NPCs | `Stepper` |
| `NOCLIP` | Espectador, depuração | `Noclip` |

Cada tipo é um sistema ECS próprio, com ordem de execução documentada no schedule.

### 5.2 Think, touch e triggers

- **Think:** componente `NextThink{time}`. Um sistema seleciona as entidades vencidas no
  tick e as executa em ordem determinística (tempo e depois `NetId`).
- **Touch:** pares de contato gerados pela física no tick, despachados em ordem estável.
- **Triggers:** `trigger_multiple`, `trigger_once`, `trigger_hurt`, `trigger_push`,
  `trigger_teleport`, `trigger_gravity`, `trigger_camera` e as zonas do CS
  ([10](10-gameplay-counter-strike.md#5-entidades-de-mapa)) são componentes de volume
  com sistemas de sobreposição.
- **Movers bloqueados:** porta bloqueada por jogador aplica dano ou reverte, conforme
  as *keyvalues* do mapa, como no original.
- **Entidades destrutíveis:** `func_breakable` (vidro, madeira, metal) com materiais,
  gibs e sons do original. `func_pushable`.

## 6. Determinismo

- `cs.shared` é compilado **uma vez** em biblioteca estática linkada pelo cliente e pelo
  servidor, para que o código de predição seja literalmente o mesmo.
- Flags de ponto flutuante fixas: sem `-ffast-math`, `-ffp-contract=off`, mesmo modo de
  arredondamento em todas as threads.
- O `nx.math` segue essas regras: aritmética escalar em `float` sem contração em *fused
  multiply-add*, e seno, cosseno e arcos avaliados em `double` e arredondados uma vez para
  `float`, o que esconde diferenças de último bit entre bibliotecas C. O teste
  `FloatingPointTest.MultiplyAddIsNeverFused` falha se uma flag de build reativar a contração.
- Determinismo garantido **por binário e plataforma**. Entre plataformas diferentes
  (Windows × Linux), a predição corrige pequenas divergências como qualquer erro de
  predição.
- Aleatoriedade de gameplay vem apenas de `nx::Random` com fluxos nomeados e semeados
  (spread por semente de comando, gibs, escolha de spawn).

## 7. Erros tipados

| Enum | Exemplos |
|------|----------|
| `CommandError` | `DurationOutOfRange`, `TimeDriftExceeded`, `StaleCommand`, `InvalidAngles` |
| `CollisionError` | `InvalidHull`, `MissingClipnodes` (mapa compilado sem o hull exigido) |
| `MoverError` | `InvalidPath` (`path_corner` ausente), `TargetNotFound` |
