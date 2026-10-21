# 07 — Simulation, movement and physics

## 1. Time and tick

| Concept | Definition |
|---------|------------|
| `sv_tickrate` | Fixed frequency of the server simulation. Default **100 Hz** (parity with classic servers) |
| `GameTime` | Integer tick counter plus a fraction. No precision loss with long uptime (GoldSrc used `float` seconds, which degrades after hours) |
| `TickTime::delta` | Duration of one tick (`1 / sv_tickrate`) as `nx::Seconds` |
| Usercmd | Client input command with its own duration (`msec`), applied individually |

The server advances "world" entities (grenades, doors, triggers, timers) per tick.
**Players advance per usercmd**, as in the original, with the duration of each command. This
preserves the original movement behavior.

**Today** (`nx.core:gameTime`, `nx.game:clock`): `nx::TickRate` (`kDefaultTickRate`, 100) and
`nx::GameTime`, a signed 64-bit count of ticks since the map started and a `float` fraction of the
next, from 0 up to 1, ordered by tick then fraction. `after(seconds, rate)` moves it by a duration,
carrying the fraction's whole ticks into the count (a fraction that rounds up to 1 is the next tick's
start), and `since(earlier, rate)` measures from another moment, both in double precision: a moment
a month into a map is as precise as one at its start, where float seconds a month in are a quarter
of a second apart. Each game world holds a `TickTime` resource, its rate and its `now`, which
`registerComponents` gives it at the map's start and `advanceTick` moves on; the pushers' own clocks
and when they next act, and when a door was last blocked, are `GameTime`s on it.

## 2. User commands

### 2.1 Structure

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

Today `cs::UserCommand` (`cs.shared:movement`) has the fields movement reads: the duration, the
view angles, the move and the buttons (`Jump`, `Duck`, `Forward`, `Back`, `MoveLeft`,
`MoveRight`). The others arrive with what reads them.

### 2.2 Generation on the client

- **Fixed generation rate:** the client generates usercmds at `cl_movement_rate` Hz (default
  **100**, that is, `msec = 10`), **regardless of the render FPS**. This reproduces the
  classic competitive configuration (`fps_max 100`) on any machine and eliminates
  FPS-dependent physics differences.
- For compatibility with those who want the old behavior, `cl_movement_rate 0` generates one
  usercmd per render frame with the frame's `msec`. It is marked as a legacy mode and is not
  recommended.
- `cl_cmdrate` controls how many command packets are sent per second. Each packet
  carries the new commands plus `cl_cmdbackup` previous commands (redundancy against
  loss), as in the original.
- The server validates the sum of `duration` per time window, against *speedhack*
  (`sv_max_command_time_drift`). The excess is discarded, with a per-client metric.

### 2.3 Shared seed

Spread and other weapon randomness use a **seed derived from the command number**,
the same on the client and the server. This way the client's spread prediction matches the
server, as in the original.

## 3. Player movement (pmove)

Movement is the most sensitive part of parity. It lives in `cs.shared` and is **the same
code** on the server (authoritative) and on the client (prediction).

### 3.1 Purity requirements

```cpp
[[nodiscard]] auto simulateMovement(const MovementState& state,
                                    const UserCommand& command,
                                    const MovementSettings& settings,
                                    const nx::collision::BrushHulls& hulls,
                                    std::span<const BrushEntity> entities) -> MovementResult;
```

- Pure function: the result depends only on the arguments. No access to global state,
  the clock or server-only state. The state carries what the original keeps between commands
  (the duck under way and the eyes' height, the jump penalty, the jump out of water and its push,
  the held jump and duck buttons).
- `MovementSettings` contains the movement cvars (`sv_gravity`, `sv_friction`, `edgefriction`,
  `sv_stopspeed`, `sv_accelerate`, `sv_airaccelerate`, `sv_maxspeed`, `sv_maxvelocity`,
  `sv_stepsize`, `sv_rollangle`, `sv_rollspeed`), replicated to the client, with the defaults
  Counter-Strike plays with (its game sets `sv_accelerate` 5 and `sv_stopspeed` 75 over the
  engine's). `sv_wateraccelerate` and `sv_waterfriction` are not among them: Counter-Strike's
  water movement reads `sv_accelerate` and `sv_friction`. `sv_bounce` arrives with the friction of
  entities, the only thing that makes a player bounce off what it hits.
- It moves as the original moves, written anew from the GPL code that reconstructs it
  ([20 §3](20-legal-and-licensing.md#3-clean-room)): its order of steps, its thresholds and the
  precision of each sum (the ones the original makes in double precision are made in double here),
  which over many commands decide where a player ends up.
- What it moves among: the world (model 0 of the hulls) and the brush entities near the player,
  each a brush model of the same hulls at its origin, in the role the game gave it (the original's
  physents): **solid** (it stops the player), a **volume** whose contents a point inside it is in
  (`func_water`, and `func_illusionary` or a door given contents), or a **ladder**. Where two stop
  a trace at once, the first in the list does, the world before them all.
- Today (`cs.shared:movement`): walking, the air, jumping with its penalty and the bunny hop
  limit, ducking (0.4 s on the ground with the eyes easing down, at once in the air, the ducktap
  of a duck let go before it is down), stairs, sliding along walls and floors, water (its levels,
  swimming, jumping out) and ladders (climbed by the movement buttons along their face, left by
  a jump or by backing off them on the floor), among the world and the brush entities, where the
  game puts and turns them.
  `nxview` walks through the maps with it, among the brush entities `nx.game` makes as the game
  does when the map starts, listed for movement by `cs::brushEntitiesOf`. The result is the
  player and what it ran into or stood on (`MovementResult::touchedModels`, the original's touch
  list), which the game touches, a door opening; the player keeps the entity it stands on
  (`groundModel`), which a pusher carries. A solid entity turned by its angles is met turned, as
  the original turns a trace into its space and the plane it meets back out. Noclip, observers,
  unsticking, boxes (players, hostages, pushables), conveyors, pushes and currents come next
  ([TODO](../TODO.md)).

### 3.2 Behaviors that MUST be preserved

Each item below has a dedicated parity test ([18](18-testing-and-quality.md#parity-tests)):

| Behavior | Note |
|----------|------|
| Ground acceleration and friction | Including `edgefriction` near edges and `stopspeed` |
| Air acceleration with a cap on the desired speed | Basis of *air strafe*, *strafe jump* and *surf* |
| Jump and jump height | Initial velocity derived from the height, 45, and a gravity of 800, whatever `sv_gravity` is |
| Speed penalty after a jump (stamina) | A CS 1.6 trait that limits *bunny hop* |
| The original's *bunny hop* limiters | Reproduce the original exactly, as the test reveals |
| Ducking on the ground (transition) and in the air (instant) | *Duck jump*, *ducktap* |
| Step climbing (`sv_stepsize`) | Includes interaction with ramps |
| Steep ramps | Threshold normal for "ground". Below it the player slides (surf) |
| Ladders (`func_ladder`) | Speed, exit, jumping off the ladder |
| Water (levels 0–3) | Swimming, sinking, leaving the water |
| Fall damage | The original's damage thresholds and scale |
| Maximum speed per weapon | Weapon data (knife, pistols, zoomed AWP etc.) |
| Pushing and being pushed by movers | Doors and platforms (`MOVETYPE_PUSH`) |
| *Edgebug* and collision quirks | A natural consequence of reproducing traces and clipping faithfully |
| *Noclip*, *spectator* | For debugging and observers |

**Policy:** when in doubt between "fixing" an odd behavior and reproducing it,
**reproduce it** and document it with `// Parity:`. Fixes sit behind explicit cvars that
default to off.

### 3.3 Hulls

| Hull | Use | Dimensions |
|------|-----|------------|
| 0 — point | Bullets, line traces | Point |
| 1 — standing | Standing player | Standard GoldSrc player box |
| 2 — large | Large entities | GoldSrc large box |
| 3 — ducked | Ducked player | GoldSrc ducked player box |

The dimensions **MUST** match the hulls compiled into the BSP, because each hull's clipnode
trees come from the map compiler. They are not tunable: `cs.shared` holds them as constants
(`cs::kStandingBox`, 32 by 32 by 72 about the player's origin, its eyes 17 above it;
`cs::kDuckedBox`, 32 by 32 by 36, its eyes 12 above it), which movement and the player's body
among the game's pushers (`cs::bodyOf`) both read.

## 4. Collision

### 4.1 World

- Traces against the BSP's **clipnode trees** per hull, with the same `fraction`, `endpos`,
  plane, `startsolid`/`allsolid` and contents (water, lava, solid) results as the
  original.
- Brush entity submodels (doors, elevators) use their own hulls with the entity's
  transform.

**Today** (`nx.collision`, `BrushHulls`): the hulls of a map's brush models and the boxes of their
faces, from its BSP (hull 0 from the visible tree's nodes, a leaf as its contents; hulls 1 to 3
from the clip nodes they may share), checked when built: every index in range, no cycle, no tree deeper than 1024 nodes (the
compilers' deepest, Half-Life's hldemo3, is 146). The original traces by two sets of rules, and both
are here, written anew from how the GPL engines that reconstruct it describe them (the clean room,
[20 §3](20-legal-and-licensing.md#3-clean-room)):

| | `trace` (the server's: shots, entities) | `traceMovement` (player movement's) |
|---|---|---|
| The stop | 1/32 unit before the plane it would cross into solid | The same |
| A stop still in solid | Backed up a tenth of the way at a time, to the way's start at most | A twentieth, subtracted in double precision |
| Translucent contents | Neither open nor water | Water, as every content not empty nor solid |
| The crossing point | Its sum in double precision, rounded once | In single precision |
| A crossing that is not a number | Ends the trace | Is 0 |
| A hull rooted at its tree's last node | A tree | Empty (a quirk of the original, kept) |
| After the trace | The end recomputed from the fraction | All-solid counted as starting solid, which stops it at its start (fraction 0); the end recomputed |

A model is met where its entity is and turned as it is (`HullTraceQuery::offset` and `angles`), by
both rules: the original turns the trace into the turned model's space, and the plane it meets back
out to the world's. `nxcheck` builds the hulls of every map of the installation. Traces recorded
from the original, to test these rules against, are pending
([18](18-testing-and-quality.md#parity-tests)).

### 4.2 Entities

- Dynamic spatial partitioning (AABB tree) for moving entities, plus links to BSP leafs
  for PVS/PAS.
- Players collide with each other using the hull box.
- Bullet traces test the studio model's **hitboxes** (animated bones) to get the
  *hitgroup*.

### 4.3 API

The API below is planned, with entities and hitboxes. Today it is `BrushHulls::trace` and
`traceMovement` (`nx.collision:hulls`), which take a `HullTraceQuery` (`model`, `hull`, `offset`,
`angles`, `start`, `end`) and give a `Trace` (`fraction`, `end`, `plane`, `isAllSolid`,
`startsSolid`, `isInOpen`, `isInWater`), against brush models only ([§4.1](#41-world)).

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

- `TraceResult` always has `fraction`, `position`, `normal` and `contents`. The optional
  fields (`entity`, `hitGroup`, `surfaceMaterial`) are `std::optional`.
- Traces are *thread-safe* against an **immutable collision snapshot** of the tick. This
  allows traces in parallel jobs (e.g. lag compensation, bot AI).

## 5. Non-player entity physics

### 5.1 Movement types

| Type (GoldSrc) | Use in CS | Component |
|----------------|-----------|-----------|
| `NONE` | Static entities | — |
| `TOSS` | Dropped weapons, C4 on the ground | `Ballistic{gravity}` |
| `BOUNCE` | Grenades | `Ballistic{gravity, elasticity}` |
| `PUSH` | Doors, platforms, trains | `Mover{path, speed, blockedPolicy}` |
| `FOLLOW` | Weapons on the player model | `Parent` + attachment |
| `FLY`, `FLYMISSILE` | Mod projectiles | `Ballistic{gravity = 0}` |
| `STEP` | Hostages/NPCs | `Stepper` |
| `NOCLIP` | Spectator, debugging | `Noclip` |

Each type is its own ECS system, with its execution order documented in the schedule.

**Today** (`nx.game:movers`): `PUSH`, for doors. A `Door` slides between where it is closed and open
(`func_door`, `func_water`) or turns between its closed and open angles (`func_door_rotating`,
about its yaw axis, or roll or pitch as flagged, by its `distance`), moved by a `Pusher` (its
velocity and angular velocity, its own clock and when it next acts by it), as the original's
`CBaseDoor`, `CRotDoor` and `CBaseToggle::LinearMove` and `AngularMove`: a player's touch opens one
nobody else opens (no `targetname` or `master`, not use-only; others use it, §5.2), a turning one
away from the player by
the side of its box the player is on (`DoorGoUp`, its arms compared cut to whole units, as the
original; all of its open angles turned by the sign, its starting ones included, as the original
does); it stays open its `wait` and closes, or stays open for good (a negative `wait`), or until it
is touched again (toggle). A turning door flagged to start open starts open with both ends closed,
the original's mistake, kept. `runPushers` advances each pusher a tick as the original's
`SV_Physics_Pusher`, `SV_PushMove` and `SV_PushRotate` do: only while it has something to do by its
clock; carrying each `Body` it runs into, or that stands on it, along with it, or around its origin
as it turns, through the world and the other solid brush entities by the server's traces; when one
cannot go, everything goes back and the door goes back the way it came (`CBaseDoor::Blocked`, no
sooner than 0.25 s after the last time). A pusher moves a tick, or up to when it acts within it,
and its clock lands there exactly (`GameTime`), never a float tick short of it.

**Today** (`nx.game:buttons`): a `Button` (`func_button`, `CBaseButton`) moves as a sliding door
does, along its `angles`, its loaded size less 2 and its `lip` (4 when it gives none), at its `speed`
(40 when it gives none), or not at all when it would move less than a unit or is flagged "don't
move". Used (`ButtonUse`), one that is out and not moving goes in; one that is in comes back out when
it is a toggle (spawnflag 32), firing nothing then, as the original. One flagged "touch only"
(spawnflag 256) ignores use and is pressed by a player's touch instead (`ButtonTouch`), untouchable
until its move is done; touched in, a toggle fires its targets and comes back out. In, a button
fires its targets (`TriggerAndWait`) and comes back out after its `wait` (1 s when it gives none), or
stays in until used again (a toggle), or for good (a `wait` of -1); back out, a toggle fires its
targets again (`ButtonBackHome`). The moves of doors and buttons are those of the original's
`CBaseToggle` (`nx.game:toggles`), each arriving as its kind does. `nxview` ticks the game at 100 Hz:
the player's use (E), the player's command, the doors and buttons it touched, the triggers it is in,
what is due, then the pushers, the player's body among the bodies, then the clock.

**Today** (`nx.game:placement`): an entity the game drops to the floor as it makes it, as the
engine's `DROP_TO_FLOOR` drops it: its box traced 256 units down by the server's rules through the
hull its size chooses (`collision::boxHullOf`, `SV_HullForBsp`: a point for one 8 units wide or less,
the ducked hull for one 36 wide and tall at most, the standing one for one taller, the large one for
one wider; the hull's middle offset from the box's origin by the hull's low corner from the box's),
among the world and the solid brush entities; where it starts when it starts all in solid or meets
nothing. `nxview` drops the hostages so.

### 5.2 Think, touch and triggers

- **Think:** a `NextThink{time}` component. A system selects the entities that are due in
  the tick and runs them in deterministic order (time, then `NetId`).
- **Touch:** contact pairs generated by physics in the tick, dispatched in a stable order.
- **Triggers:** `trigger_multiple`, `trigger_once`, `trigger_hurt`, `trigger_push`,
  `trigger_teleport`, `trigger_gravity`, `trigger_camera` and the CS zones
  ([10](10-counter-strike-gameplay.md#5-map-entities)) are volume components
  with overlap systems.
- **Blocked movers:** a door blocked by a player deals damage or reverses, according to
  the map's *keyvalues*, as in the original.
- **Destructible entities:** `func_breakable` (glass, wood, metal) with the original's
  materials, gibs and sounds. `func_pushable`.

**Today** (`nx.game:thinking`, `nx.game:targets`, `nx.game:triggers`): an entity acts at its
`NextThink`, a `GameTime` of its world's clock; `runThinks` runs, each tick, every entity due before
the next tick starts (`SV_RunThink`), from the earliest, then by the entity, its `NextThink` taken off
first. An entity's name is its `Named` (`targetname`); what it fires, its `Targets` (`target`,
`killtarget`, `delay`, `CBaseDelay`). `useTargets` fires them as `SUB_UseTargets`: with a delay, through
a `DelayedUse` entity that fires them at its `NextThink` and goes; at once, removing the entities named
by the kill target, then using every entity named by the target, in the world's order, found by name
when they fire as `FireTargets` finds them; a chain of targets firing targets stops 128 deep, as
ReGameDLL stops one. Used, a door opens when closed, or closes when it stays open until used again
(`CBaseDoor::Use`), a turning one away from what used it; a door fires its targets when it is open and
when it is closed (`DoorHitTop`, `DoorHitBottom`). `trigger_multiple` and `trigger_once` are `Trigger`s:
a body touches one when their linked boxes meet and the trigger model's hull for the body's size holds
the body's origin (`SV_TouchLinks`, the model not turned); one not flagged "no clients" and not still
waiting (its `NextThink` after now) fires its targets for the body, then waits its `wait` (0.2 s when
it gives none) or, a `trigger_once`, is spent and goes 0.1 s later. `nxview` touches the triggers the
player is in after each command, then runs what is due before the pushers: cs_747's curtains open as
the player walks to them. A player's use (`playerUse`, `CBasePlayer::PlayerUse`) uses, of the brush
entities it reaches (a button that takes no damage, a door flagged "use only"), whose linked box
comes within 64 units of its origin (`FindEntityInSphere`), the one whose middle is most straight
ahead from its eye, more than 0.7 of the way; as the original, through walls. In cs_office, the
button by the garage door raises it.

## 6. Determinism

- `cs.shared` is compiled **once** into a static library linked by the client and the
  server, so that the prediction code is literally the same.
- Fixed floating-point flags: no `-ffast-math`, `-ffp-contract=off`, the same rounding
  mode on every thread.
- `nx.math` follows these rules: scalar `float` arithmetic without contraction into *fused
  multiply-add*, and sine, cosine and the arc functions evaluated in `double` and rounded
  once to `float`, which hides last-bit differences between C libraries. The
  `FloatingPointTest.MultiplyAddIsNeverFused` test fails if a build flag re-enables
  contraction.
- Determinism is guaranteed **per binary and platform**. Across different platforms
  (Windows × Linux), prediction corrects small divergences like any other prediction
  error.
- Gameplay randomness comes only from `nx::Random` with named, seeded streams
  (spread by command seed, gibs, spawn choice).

## 7. Typed errors

| Enum | Examples |
|------|----------|
| `CommandError` (planned, with the server's command validation) | `DurationOutOfRange`, `TimeDriftExceeded`, `StaleCommand`, `InvalidAngles` |
| `CollisionError` | `BadIndex`, `Cycle`, `TooDeep` (hull trees a compiler does not write) |
| `MoverError` (planned, with trains and platforms) | `InvalidPath` (missing `path_corner`), `TargetNotFound` |
