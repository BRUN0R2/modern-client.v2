# 03 — Entity Component System (ECS)

> Decisão registrada em [ADR-0002](adr/0002-ecs-por-arquetipos.md).

## 1. Objetivos

| Objetivo | Como é atendido |
|----------|-----------------|
| Sem teto de entidades | Handle de 64 bits (índice de 32 + geração de 32). O limite prático é a memória |
| Cache-friendly | Armazenamento por **arquétipo** em **chunks** SoA |
| Paralelo | Iteração por chunk em jobs. O agendador paraleliza sistemas sem conflito |
| Determinístico | Ordem de sistemas fixa, command buffers aplicados em ordem estável, IDs alocados em ordem estável |
| Pronto para rede | Versões de mudança por chunk e coluna alimentam a replicação delta |
| Extensível por mods | Componentes Rook registrados em tempo de execução com layout conhecido |

## 2. Conceitos

| Conceito | Definição |
|----------|-----------|
| **Entity** | Handle opaco `{index: u32, generation: u32}`. Não carrega dados |
| **Component** | Struct de dados sem comportamento (`Transform`, `Health`, `WeaponState`) |
| **Tag** | Componente de tamanho zero (`Player`, `Dead`, `OnGround`). Só afeta o arquétipo |
| **Archetype** | Conjunto ordenado de tipos de componente. Todas as entidades com exatamente esse conjunto vivem nele |
| **Chunk** | Bloco de 16 KiB de um arquétipo, com colunas SoA (um array por componente) |
| **Query** | Filtro tipado de arquétipos (`with`, `without`, `changed`) mais acesso (leitura/escrita) |
| **System** | Função que recebe queries e recursos. O acesso que ela declara define o paralelismo |
| **Resource** | Dado único do mundo (`GameRules`, `TickTime`, `MapInfo`) |
| **Event** | Mensagem tipada com vida de um tick (`PlayerKilled`) |
| **Command buffer** | Fila de mudanças estruturais (criar/destruir/adicionar/remover) aplicada em *sync points* |

## 3. Entidades

```cpp
export namespace nx::ecs {

class Entity {
public:
    [[nodiscard]] constexpr auto index() const noexcept -> EntityIndex;
    [[nodiscard]] constexpr auto generation() const noexcept -> Generation;
    constexpr auto operator<=>(const Entity&) const = default;

    Entity(std::uint32_t) = delete("entities are issued by World; raw indices are not handles");

private:
    friend class World;
    constexpr Entity(EntityIndex index, Generation generation) noexcept;
    EntityIndex m_index;
    Generation  m_generation;
};

} // namespace nx::ecs
```

- **Validação:** `world.isAlive(entity)` compara a geração do handle com a do slot. Um
  handle antigo **nunca** identifica outra entidade.
- **Reuso de slots:** fila FIFO de slots livres, para que um índice demore o máximo
  possível para ser reutilizado.
- **Esgotamento de geração:** quando a geração de um slot chega ao máximo, o slot é
  **aposentado** (nunca mais reutilizado), em vez de dar a volta. Isso é registrado em
  métricas.
- **Registro de entidade:** `{archetype, chunk, row}` em um array denso indexado por
  `EntityIndex`.
- **Sem limite estrutural:** até 2³² − 1 slots. Limites operacionais (ex.: entidades
  criáveis por plugin) são cotas configuráveis, com erro tipado
  `EntityError::QuotaExceeded`.

## 4. Componentes

### 4.1 Declaração

Componentes são structs agregadas. Metadados vêm de anotações lidas por reflexão:

```cpp
export module cs.shared:components;

import nx.core;
import nx.math;
import nx.net;
import nx.script;

export namespace cs {

inline constexpr auto kWorldHalfExtent = nx::HammerUnits{8192.0f};
inline constexpr auto kOriginBits      = nx::net::BitCount{24};

struct [[=nx::net::Replicated{}, =nx::script::Expose{"Transform"}]] Transform {
    [[=nx::net::QuantizeRange{.bits = kOriginBits, .extent = kWorldHalfExtent}]]
    nx::Vec3 origin;
    nx::EulerAngles angles;
};

struct [[=nx::net::Replicated{.audience = nx::net::Audience::Owner}]] Ammo {
    std::int16_t clip;
    std::int16_t reserve;
};

struct [[=nx::ecs::TagComponent{}]] OnGround {};

} // namespace cs
```

### 4.2 Registro e identidade de tipo

- `ComponentTypeId` local: índice denso atribuído no registro, usado em máscaras e arquétipos.
- `StableTypeId`: `nx::StableId::fromName("cs::Transform")`, o XXH64 de um nome **declarado**
  no registro (nunca o nome gerado pelo compilador, que muda entre compiladores). É
  usado em rede, saves, replays e mods, e não muda entre builds enquanto o nome não mudar.
- O registro coleta por reflexão: tamanho, alinhamento, se é trivialmente
  copiável/realocável, construtor padrão, destrutor, nome, campos e anotações.
- Colisão de `StableTypeId` gera erro de registro (`ComponentError::StableIdCollision`)
  já na inicialização.

### 4.3 Regras para componentes

- Sem ponteiros crus para outras entidades: use `Entity`.
- Sem `std::string` em componentes quentes: use `nx::Name` (string interna) ou um handle.
- Componentes trivialmente copiáveis são movidos com `memcpy` entre chunks. Os demais
  usam operações refletidas.
- Um componente representa **um** conceito. Componentes "deus" (todo o estado do
  jogador em uma struct) são proibidos.

## 5. Armazenamento

```
Archetype {Transform, Velocity, Player, OnGround}
├── Chunk 0 (16 KiB)
│   ├── Entity[]     ███████████
│   ├── Transform[]  ███████████
│   ├── Velocity[]   ███████████
│   └── versions:    {Transform: 412, Velocity: 418}
├── Chunk 1 ...
```

- Capacidade do chunk = `(16 KiB − cabeçalho) / Σ tamanhos`, calculada por arquétipo.
- **Versões de mudança:** cada coluna de cada chunk guarda a versão do mundo na última
  escrita (acesso mutável). Queries com `Changed<T>` pulam chunks não alterados.
  Replicação e extract de render usam o mesmo mecanismo.
- **Grafo de arquétipos:** cada arquétipo guarda as arestas "adicionar T"/"remover T"
  para transições O(1) amortizadas.
- Remoção usa *swap-and-pop* dentro do chunk, atualizando o registro da entidade movida.
- Chunks vazios voltam para um pool do mundo.

## 6. Queries

```cpp
using MovingBodies = nx::ecs::Query<
    const cs::Transform&,
    cs::Velocity&,
    nx::ecs::With<cs::PhysicsBody>,
    nx::ecs::Without<cs::Frozen>>;
```

- Os parâmetros de acesso definem leitura (`const T&`), escrita (`T&`) e presença
  opcional (`std::optional<const T&>`).
- Os filtros `With<T>`, `Without<T>`, `Changed<T>` e `Added<T>` não dão acesso.
- O cache de arquétipos compatíveis é atualizado incrementalmente quando um arquétipo novo
  aparece.
- Formas de iteração:
  - `query.forEach(fn)`: sequencial.
  - `query.forEachChunk(fn)`: recebe `std::span`s das colunas, ideal para SIMD.
  - `query.parallelForEachChunk(jobs, fn)`: um job por lote de chunks, com granularidade
    automática.
- Acesso aleatório: `query.get(entity)` retorna `std::optional<Tuple>`, `nullopt` se a
  entidade não casa.

## 7. Sistemas e agendamento

### 7.1 Declaração

```cpp
void integrateProjectiles(MovingBodies bodies,
                          nx::ecs::Res<const cs::MovementSettings> settings,
                          nx::ecs::Res<const nx::TickTime> time)
{
    bodies.parallelForEachChunk([&](std::span<const cs::Transform> transforms,
                                    std::span<cs::Velocity> velocities) {
        for (auto& velocity : velocities) {
            velocity.linear.z -= settings->gravity.value() * time->delta.count();
        }
    });
}

schedule.add(integrateProjectiles)
        .in(nx::ecs::Phase::Simulation)
        .after(applyUserCommands);
```

### 7.2 Fases

| Fase | Frequência | Conteúdo típico |
|------|------------|-----------------|
| `Input` | por tick | Aplicar usercmds recebidos |
| `PreSimulation` | por tick | Timers, think de entidades, triggers |
| `Simulation` | por tick | Movimento, física, armas, dano |
| `Rules` | por tick | Regras de jogo seriais (round, economia, objetivos) e hooks Rook síncronos |
| `Script` | por tick | Despacho de eventos Rook em ordem determinística |
| `PostSimulation` | por tick | Limpeza, aplicação final de command buffers |
| `Replication` | por snapshot | Montagem de snapshots por cliente (servidor) |
| `Presentation` | por frame | Interpolação, animação visual, extract (cliente) |

### 7.3 Paralelismo e determinismo

- O agendador monta, por fase, um **DAG** em que dois sistemas conflitam se um escreve um
  componente ou recurso que o outro lê ou escreve.
- Sistemas sem conflito executam em paralelo. Os que conflitam executam na **ordem de
  declaração**, ajustada por `.before()`/`.after()`.
- Ciclos de ordenação são erro de construção do schedule (`ScheduleError::Cycle`), com o
  ciclo impresso.
- **Sistemas exclusivos** (`nx::ecs::World&` mutável) rodam sozinhos e servem para
  operações que tocam o mundo inteiro (carregar mapa, reset de round).
- **Regras de gameplay** que dependem de ordem (dano, morte, compra, round) vivem na fase
  `Rules` e são seriais por desenho. O ganho de paralelismo vem dos sistemas em lote, não
  de paralelizar regras com efeitos cruzados.

## 8. Mudanças estruturais

- Durante a execução de sistemas, criar/destruir entidades e adicionar/remover componentes
  só pode ser feito via `nx::ecs::Commands` (command buffer por job).
- Os command buffers são aplicados em *sync points* definidos pelo schedule (fim de cada
  fase, por padrão).
- **Ordem de aplicação determinística:** por ordem do sistema no schedule e, dentro de um
  sistema paralelo, por índice do chunk de origem. Não depende da thread que executou.
- **Entidades criadas em command buffer** recebem um `PendingEntity` local. O `Entity` real
  é alocado na aplicação, em ordem determinística, e o mapeamento fica disponível para os
  sistemas seguintes.

## 9. Eventos

```cpp
void announceKills(nx::ecs::EventReader<cs::PlayerKilled> kills, nx::ecs::Res<cs::KillFeed> feed);
void applyDamage(..., nx::ecs::EventWriter<cs::PlayerKilled> kills);
```

- Os eventos são *double-buffered*: escritos no tick N, legíveis até o fim do tick N+1.
- Escritores em jobs paralelos usam buffers por job, concatenados em ordem determinística.
- A fase `Script` consome os eventos marcados `[[=nx::script::Expose{}]]` e os despacha
  para os mods inscritos ([13](13-sistema-de-mods.md)).

## 10. Relações

| Relação | Representação | Uso |
|---------|---------------|-----|
| Pai/filho | `Parent{Entity}` + `Children{inplace_vector/handle}` | Anexos: arma no modelo do jogador (`MOVETYPE_FOLLOW`), sprites anexados |
| Dono | `Owner{Entity}` | Granadas, armas no chão, projéteis (não colidir com o dono) |
| Alvo | `Target{Entity}` | Entidades de mapa com `target`/`targetname` resolvidos na carga |

Destruir uma entidade remove as relações que apontam para ela no mesmo sync point
(sem handles pendurados em `Children`).

## 11. Prefabs e entidades de mapa

- Cada `classname` do BSP (`info_player_start`, `func_buyzone`, `armoury_entity`…) é
  mapeado para um **prefab**: lista de componentes com valores padrão, definida em dados.
- As *keyvalues* do lump de entidades são convertidas por esquema refletido. Chave
  desconhecida gera aviso com nome do mapa e índice da entidade, e chave obrigatória
  ausente gera erro tipado.
- Mods registram prefabs e `classname`s novos pela API ([15](15-api-de-mods.md)).

## 12. Integração com rede

- Componentes `[[=nx::net::Replicated{}]]` participam de snapshots. O `StableTypeId` e o
  layout de campos refletido geram o codificador delta.
- Cada entidade replicada recebe um `NetId` (varint) independente do `Entity` local.
- O cliente mantém um mapeamento `NetId → Entity` do seu próprio mundo.
- Detalhes em [08-rede.md](08-rede.md).

## 13. Integração com plugins Rook

- Componentes declarados em Rook (`component Frozen { until: GameTime }`) são registrados
  em tempo de execução com layout calculado pelo compilador `rookc`. Eles têm
  `StableTypeId` derivado de `"<mod-id>.<plugin-id>::<NomeDoComponente>"`.
- Plugins declaram sistemas com queries tipadas. O agendador trata um sistema de plugin
  como qualquer outro: o acesso declarado define o paralelismo. Sistemas do **mesmo**
  plugin são serializados entre si, porque cada plugin tem um único heap (isolate).
  Plugins diferentes sem conflito de acesso rodam em paralelo.
- Plugins nunca recebem ponteiros para chunks: a VM acessa colunas por meio de views
  verificadas.

## 14. Mundos

| Mundo | Onde | Conteúdo |
|-------|------|----------|
| Mundo do servidor | `nextds` / listen server | Estado autoritativo |
| Mundo do cliente | `next` | Entidades replicadas (interpoladas) e o jogador local (predito) |
| Mundo de render | `next` | Estrutura plana (não-ECS) preenchida por extract a cada frame |

## 15. Ferramentas de depuração

- `ent_list [filtro]`, `ent_dump <netid>` e `ecs_stats` (arquétipos, chunks, ocupação,
  memória por componente).
- Inspetor ImGui em builds de desenvolvimento: árvore de arquétipos, edição de
  componentes e visualização do DAG do schedule com tempos por sistema.
- Zonas Tracy por sistema e por lote de chunks.

## 16. Metas de desempenho (hardware de referência a definir no M1)

| Operação | Meta |
|----------|------|
| Iterar 100 mil entidades com 2 componentes (1 thread) | < 0,2 ms |
| Criar/destruir 10 mil entidades via command buffer | < 1 ms |
| Overhead do agendador por sistema vazio | < 2 µs |
