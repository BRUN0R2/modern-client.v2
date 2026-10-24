# 03 — Entity Component System (ECS)

> Decision recorded in [ADR-0002](adr/0002-archetype-ecs.md).

## 1. Goals

| Goal | How it is met |
|------|---------------|
| No entity ceiling | 64-bit handle (32-bit index + 32-bit generation). The practical limit is memory |
| Cache-friendly | Storage by **archetype** in SoA **chunks** |
| Parallel | Per-chunk iteration in jobs. The scheduler parallelizes systems that do not conflict |
| Deterministic | Fixed system order, command buffers applied in a stable order, IDs allocated in a stable order |
| Network-ready | Per-chunk and per-column change versions feed delta replication |
| Extensible by mods | Rook components registered at runtime with a known layout |

## 2. Concepts

| Concept | Definition |
|---------|------------|
| **Entity** | Opaque handle `{index: u32, generation: u32}`. Carries no data |
| **Component** | Data struct without behavior (`Transform`, `Health`, `WeaponState`) |
| **Tag** | Zero-size component (`Player`, `Dead`, `OnGround`). Only affects the archetype |
| **Archetype** | Ordered set of component types. Every entity with exactly that set lives in it |
| **Chunk** | 16 KiB block of an archetype, with SoA columns (one array per component) |
| **Query** | Typed archetype filter (`with`, `without`, `changed`) plus access (read/write) |
| **System** | Function that receives queries and resources. The access it declares defines the parallelism |
| **Resource** | Unique world data (`GameRules`, `TickTime`, `MapInfo`) |
| **Event** | Typed message that lives for two ticks: written in one, readable until the end of the next (`PlayerKilled`, [§9](#9-events)) |
| **Command buffer** | Queue of structural changes (create/destroy/add/remove) applied at *sync points* |

## 3. Entities

```cpp
export namespace nx::ecs {

enum class EntityIndex : std::uint32_t {};
enum class Generation : std::uint32_t {};

class Entity {
public:
    constexpr Entity() noexcept = default;  // the null entity, never alive
    Entity(std::uint32_t rawIndex) = delete; // raw indices are not handles

    [[nodiscard]] constexpr auto index() const noexcept -> EntityIndex;
    [[nodiscard]] constexpr auto generation() const noexcept -> Generation;
    [[nodiscard]] constexpr auto isNull() const noexcept -> bool;
    // A member, not a hidden friend: GCC 16 crashes on defaulted friend comparisons (17 §2.2).
    [[nodiscard]] constexpr auto operator<=>(const Entity&) const noexcept = default;

private:
    friend class EntityTable;
    constexpr Entity(EntityIndex index, Generation generation) noexcept;
    EntityIndex m_index = kNullIndex;
    Generation  m_generation{};
};

} // namespace nx::ecs
```

- **Validation:** `world.isAlive(entity)` compares the handle's generation with the slot's. An
  old handle **never** identifies another entity. Destroying advances the slot's generation
  immediately, so a free slot already holds a generation that no issued handle carries.
- **Null entity:** `Entity{}` uses the index `2³² − 1`, which no slot receives, and is never
  alive. It represents "no entity" in components (`Owner{}`).
- **Slot reuse:** a FIFO queue of free slots, so that an index takes as long as possible to
  be reused.
- **Generation exhaustion:** when a slot's generation reaches the maximum, the slot is
  **retired** (never reused again) instead of wrapping around. This is recorded in
  metrics.
- **Entity record:** `{archetype, chunk, row}` in a dense array indexed by
  `EntityIndex`. The handle table is the `EntityTable`; the `World` keeps the records.
- **No structural limit:** up to 2³² − 1 slots. Operational limits (e.g. entities a plugin
  can create) are planned as configurable quotas, with the typed error
  `EntityError::QuotaExceeded`.

## 4. Components

### 4.1 Declaration

Components are aggregate structs. Metadata comes from annotations read through reflection:

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

### 4.2 Registration and type identity

- Local `ComponentTypeId`: a dense index assigned at registration, used in masks and
  archetypes.
- Until registration goes through the reflection facade (`nx.meta`, an item of the
  `TODO.md`), it takes the name explicitly, as the fallback path of
  [02 §1.2](02-coding-standards.md#12-toolchain-and-fallback-policy) does:
  `world.registerComponent<cs::Transform>("cs::Transform")`. Components defined at runtime
  (Rook) come in through the same registry, with a `ComponentInfo` (name, size, alignment,
  and relocation and destruction functions). Using an unregistered type is a contract
  violation.
- `StableTypeId`: `nx::StableId::fromName("cs::Transform")`, the XXH64 of a name **declared**
  at registration (never the compiler-generated name, which changes between compilers). It
  is used in networking, saves, replays and mods, and does not change between builds as long
  as the name does not change.
- Registration collects through reflection: size, alignment, whether the type is trivially
  copyable/relocatable, default constructor, destructor, name, fields and annotations.
- A `StableTypeId` collision produces a registration error
  (`ComponentError::StableIdCollision`) right at initialization. The other registration
  errors are `TypeAlreadyRegistered`, `NameAlreadyRegistered`, `InvalidAlignment` (above the
  chunk alignment, 64 bytes) and `InvalidName`.
- Empty components (`std::is_empty_v`) are **tags**: they enter the archetype, without a
  column.

### 4.3 Rules for components

- No raw pointers to other entities: use `Entity`.
- No `std::string` in hot components: use `nx::Name` (interned string) or a handle.
- Trivially copyable components are moved between chunks with `memcpy`. The others use
  reflected operations.
- A component represents **one** concept. "God" components (all of the player's state in
  one struct) are forbidden.

## 5. Storage

```
Archetype {Transform, Velocity, Player, OnGround}
├── Chunk 0 (16 KiB)
│   ├── Entity[]     ███████████
│   ├── Transform[]  ███████████
│   ├── Velocity[]   ███████████
│   └── versions:    {Transform: 412, Velocity: 418}
├── Chunk 1 ...
```

- Chunk capacity: the maximum number of rows that fit in 16 KiB with each array aligned,
  computed per archetype. The header (row count, versions) stays outside the block. A row
  larger than 16 KiB gets chunks made of several contiguous blocks, instead of a component
  size limit.
- **Change versions:** each column of each chunk keeps the version of its last write
  (mutable access). Queries with `Changed<T>` skip unchanged chunks. Replication and the
  render extract use the same mechanism. The marking is conservative: mutable access counts
  as a write, and so does a structural change in the chunk (a row that enters it or is moved
  into it).
- **Archetype graph:** each archetype keeps the "add T"/"remove T" edges for amortized O(1)
  transitions.
- Removal uses *swap-and-pop* with the **archetype's last row**, updating the record of the
  moved entity. This way every chunk stays full except the last one.
- Empty chunks of a single 16 KiB block return to a world pool, for any archetype to reuse;
  the storage of a chunk made of several blocks is freed.

## 6. Queries

```cpp
using MovingBodies = nx::ecs::Query<
    const cs::Transform&,
    cs::Velocity&,
    nx::ecs::With<cs::PhysicsBody>,
    nx::ecs::Without<cs::Frozen>>;
```

- The access parameters define read (`const T&`), write (`T&`) and optional presence
  (`Optional<const T>` and `Optional<T>`, which yield `const T*`/`T*`, null if the entity
  does not have the component). The `Entity` term yields the handle. `std::optional<T&>`
  (C++26) replaces the pointers once the supported compilers offer it.
- The filters `With<T>`, `Without<T>` and `Changed<T>` give no access. `Added<T>` needs a
  per-row version (the chunk version does not say which rows are new) and comes with its
  first consumer.
- A query receives two versions: `lastRun`, which `Changed<T>` compares against, and
  `thisRun`, which writes mark on the chunks. The scheduler gives each system run its own.
- The cache of matching archetypes is updated incrementally when a new archetype
  appears.
- Iteration forms:
  - `query.forEach(fn)`: sequential.
  - `query.forEachChunk(fn)`: receives `std::span`s of the columns, ideal for SIMD.
  - `query.parallelForEachChunk(jobs, fn)`: one job per batch of chunks, with automatic
    granularity. Returns when every chunk has finished.
- Random access: `query.get(entity)` returns `std::optional<Tuple>`, `nullopt` if the
  entity does not match.
- A direct structural change on the `World` (create, destroy, add, remove) during an
  iteration is a contract violation: inside systems, it goes through the command buffers (§8).

## 7. Systems and scheduling

### 7.1 Declaration

```cpp
void integrateProjectiles(MovingBodies& bodies,
                          nx::ecs::Res<const cs::MovementSettings> settings,
                          nx::ecs::Res<const nx::TickTime> time,
                          nx::jobs::JobSystem& jobs)
{
    bodies.parallelForEachChunk(jobs, [&](std::span<const cs::Transform> transforms,
                                          std::span<cs::Velocity> velocities) {
        for (auto& velocity : velocities) {
            velocity.linear.z -= settings->gravity.value() * time->delta.count();
        }
    });
}

const nx::ecs::SystemId gravity =
    schedule.add({.name = "applyGravity", .phase = nx::ecs::Phase::Simulation}, applyGravity);
const std::array afterGravity{gravity};
schedule.add({.name = "integrateProjectiles", .phase = nx::ecs::Phase::Simulation, .after = afterGravity},
             integrateProjectiles);
```

- A system is a (non-generic) function or lambda that returns `void` and only receives
  system parameters. Each parameter type declares its own access:

| Parameter | Declared access |
|-----------|-----------------|
| `Query<...>&` | Read/write per term; `Changed<T>` counts as a read of `T`. By value also works, but copies the query on every run |
| `Res<const T>` / `Res<T>` | Read / write of the resource `T` |
| `EventReader<E>` / `EventWriter<E>` | Read / write of the `Events<E>` queue |
| `Commands&` | None: each system has its own buffer |
| `World&` | Exclusive: conflicts with all |
| `nx::jobs::JobSystem&` | None |

- Registration uses a `SystemDesc` with designated initialization, like the other engine
  options ([02 §9](02-coding-standards.md#9-functions-and-classes)): `name` (a `JobLabel`,
  also used as the job label), `phase`, `after` and `before` (with the `SystemId`s that
  `add` returns).
- Resources are unique values per world: `world.insertResource(GameRules{...})`,
  `world.resource<GameRules>()`. A system whose resource does not exist violates the
  contract and does not run.

### 7.2 Phases

| Phase | Frequency | Typical content |
|-------|-----------|-----------------|
| `Input` | per tick | Apply received usercmds |
| `PreSimulation` | per tick | Timers, entity think, triggers |
| `Simulation` | per tick | Movement, physics, weapons, damage |
| `Rules` | per tick | Serial game rules (round, economy, objectives) and synchronous Rook hooks |
| `Script` | per tick | Rook event dispatch in deterministic order |
| `PostSimulation` | per tick | Cleanup, final application of command buffers |
| `Replication` | per snapshot | Per-client snapshot assembly (server) |
| `Presentation` | per frame | Interpolation, visual animation, extract (client) |

### 7.3 Parallelism and determinism

- For each phase, the scheduler builds a **DAG** in which two systems conflict if one writes
  a component or resource that the other reads or writes.
- Systems that do not conflict run in parallel. Those that conflict run in **declaration
  order**, adjusted by `.before()`/`.after()`.
- Ordering cycles are a schedule build error (`ScheduleError::Cycle`), with the cycle
  printed (`alpha -> beta -> alpha`). A constraint that names a nonexistent system is
  `ScheduleError::UnknownSystem`, and a constraint that contradicts the phase order is
  `ScheduleError::OrderAcrossPhases`.
- `schedule.build()` builds the graphs; `schedule.run(phase, jobs)` runs a phase, with
  each system as a job that depends on its predecessors in the graph.
  `schedule.dependenciesOf(id)` exposes the graph for diagnostics.
- The **change versions** of each run are handed out before any system runs, in schedule
  order. So what `Changed<T>` sees depends neither on timing nor on the number of
  workers.
- **Exclusive systems** (mutable `nx::ecs::World&`) run alone and serve operations that
  touch the whole world (map load, round reset).
- **Gameplay rules** that depend on order (damage, death, purchase, round) live in the
  `Rules` phase and are serial by design. The parallelism gain comes from batch systems, not
  from parallelizing rules with cross effects.

## 8. Structural changes

- While systems run, creating/destroying entities and adding/removing components can only
  be done through `nx::ecs::Commands` (`spawn`, `destroy`, `add`, `remove`). Each system
  that receives `Commands&` has its own buffer. Exclusive systems (`World&`) change the
  world directly.
- Command buffers are applied at the *sync point* at the end of each phase.
- **Deterministic application order:** by the system's declaration order in the schedule
  and, within a buffer, by recording order. It does not depend on the thread that ran it.
  Per-chunk buffers inside `parallelForEachChunk` (applied by source chunk index) come with
  the first system that needs them.
- **Entities created in a command buffer** receive a local `PendingEntity`, which other
  commands in the same buffer can use as a target. The real `Entity` is allocated at
  application, in deterministic order, and `commands.resolve(pending)` returns it after
  `apply`. `spawn(components...)` records the components with the spawn, so `apply` creates
  the entity directly in its final archetype instead of moving it through one archetype per
  component.
- **Runs:** spawns recorded one after another with the same component types form one command,
  which `apply` places as one batch of rows in their archetype (`World::spawnRelocated`: one
  archetype lookup per batch, not per entity); destroys recorded one after another form one
  command too. A run changes nothing observable: entities and ids come out exactly as from the
  same commands applied one by one.
- `CommandStats` counts recorded commands: a spawn counts once, with its components. Commands
  whose target died before application (a grenade's owner died before the grenade spawned)
  are ignored and counted in `CommandStats::skipped`; the recorded component is destroyed.

## 9. Events

```cpp
void announceKills(nx::ecs::EventReader<cs::PlayerKilled> kills, nx::ecs::Res<cs::KillFeed> feed);
void applyDamage(..., nx::ecs::EventWriter<cs::PlayerKilled> kills);
```

- Events are *double-buffered*: written in tick N, readable until the end of tick N+1.
  `world.addEvents<E>()` creates the queue (`Events<E>`, a resource) and the tick loop calls
  `world.advanceEvents()` once per tick.
- Each event receives a sequence number, and each `EventReader` keeps track of how far it
  has read: a system never reads the same event twice (`reader.forEach(fn)`).
- Writers in parallel jobs will use per-job buffers, concatenated in deterministic order,
  together with the per-chunk command buffers.
- The `Script` phase consumes the events marked `[[=nx::script::Expose{}]]` and dispatches
  them to the subscribed mods ([13](13-mod-system.md)).

## 10. Relations

| Relation | Representation | Use |
|----------|----------------|-----|
| Parent/child | `Parent{Entity}` + `Children{inplace_vector/handle}` | Attachments: weapon on the player model (`MOVETYPE_FOLLOW`), attached sprites |
| Owner | `Owner{Entity}` | Grenades, weapons on the ground, projectiles (do not collide with the owner) |
| Target | `Named{name}` + `Targets{target, killTarget, delay}` | Map entities with `target`/`targetname`, found by name when they fire, as the original's `FireTargets` finds them |

> **Status:** the target relation is in `nx.game:targets` ([07 §5.2](07-simulation-and-physics.md#52-think-touch-and-triggers));
> a name is looked up when it fires, so a name that changes or an entity made later is found.
> The others come with the gameplay systems that use them. `Entity{}` (the null entity) already
> serves as "no owner".

Destroying an entity removes the relations that point to it at the same sync point
(no dangling handles in `Children`).

## 11. Prefabs and map entities

- Each BSP `classname` (`info_player_start`, `func_buyzone`, `armoury_entity`…) is
  mapped to a **prefab**: a list of components with default values, defined in data.
- The *keyvalues* of the entities lump are converted through a reflected schema. An unknown
  key produces a warning with the map name and the entity index, and a missing required key
  produces a typed error.
- Mods register new prefabs and `classname`s through the API ([15](15-mod-api.md)).

**Today** (`nx.game`): the brush entities. `data/cstrike/entities.toml` describes, for each class
of them, how solid the game makes it (`nx::game::BrushClass`: solid or not, the spawnflags that
make one not solid, whether a `skin` makes a door a volume, contents the class always has).
`spawnBrushEntities` makes, in the map's order, an entity for each one of a listed class, with its
`BrushModel` (its `nx::assets::ModelIndex`), its `Transform` and its `BrushSolidity` (the original's
`solid`, and its `skin` as the contents that number names, `nx::assets::contentsNumbered`: a volume
of water, slime or lava, a ladder, or nothing), and what
its class's `behavior` makes of it (`slidingDoor`, `rotatingDoor`: a `Door` and its `Pusher`,
[07 §5.1](07-simulation-and-physics.md#51-movement-types), and its `Waves`, an eighth of its
`WaveHeight`, as `CBaseDoor::KeyValue` sets a door's `scale`); an entity of a brush model the map
lacks is `GameError::UnknownModel`. `nx.assets` reads the values as
the original does (`BspEntity::findInteger`, `findVector`, `findBrushModel`). Of the point entities,
the `infodecal`s give the decals the map stamps as its game starts (`nx::game::staticDecalsOf`,
[05 §8](05-rendering.md#8-bsp-world)), without an entity of their own.

## 12. Networking integration

- `[[=nx::net::Replicated{}]]` components take part in snapshots. The `StableTypeId` and the
  reflected field layout generate the delta encoder.
- Each replicated entity receives a `NetId` (varint) independent of the local `Entity`.
- The client keeps a `NetId → Entity` mapping of its own world.
- Details in [08-networking.md](08-networking.md).

## 13. Rook plugin integration

- Components declared in Rook (`component Frozen { until: GameTime }`) are registered at
  runtime with a layout computed by the `rookc` compiler. Their `StableTypeId` is derived
  from `"<mod-id>.<plugin-id>::<ComponentName>"`.
- Plugins declare systems with typed queries. The scheduler treats a plugin system like any
  other: the declared access defines the parallelism. Systems of the **same** plugin are
  serialized among themselves, because each plugin has a single heap (isolate). Different
  plugins without access conflicts run in parallel.
- Plugins never receive pointers to chunks: the VM accesses columns through checked
  views.

## 14. Worlds

| World | Where | Content |
|-------|-------|---------|
| Server world | `nextds` / listen server | Authoritative state |
| Client world | `next` | Replicated entities (interpolated) and the local player (predicted) |
| Render world | `next` | Flat (non-ECS) structure filled by extract every frame |

## 15. Debugging tools

- `ent_list [filter]`, `ent_dump <netid>` and `ecs_stats` (archetypes, chunks, occupancy,
  memory per component). The numbers already come from `World::stats()`; the commands are
  planned in `nx.console` ([TODO](../TODO.md)).
- ImGui inspector in development builds: archetype tree, component editing and a view of
  the schedule DAG with per-system timings.
- Tracy zones per system and per batch of chunks.

## 16. Performance targets (reference hardware to be defined in M1)

| Operation | Target |
|-----------|--------|
| Iterate 100,000 entities with 2 components (1 thread) | < 0.2 ms |
| Create/destroy 10,000 entities through a command buffer | < 1 ms |
| Scheduler overhead per empty system | < 2 µs |

`nxBenchEcs` measures the three (`iterateTwoComponents`, `spawnAndDestroyThroughCommands`,
`emptySystems`); run it from a release build. On a 14-core development machine (2026-10-20), the
three meet their targets: about 0.16 ms for the iteration, 0.86 ms to create and destroy 10,000
entities through reused command buffers, and 0.18 µs per empty system on one thread.
