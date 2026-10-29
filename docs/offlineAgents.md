# Offline entity and agent tests

The native development mod `mod.zombies` exercises generic humanoid entities with pursuing,
attacking zombies before the Rook runtime exists ([ADR-0028](adr/0028-native-agent-test-mod.md)).
It runs in the local server, through the same simulation that a dedicated host will use.
It does not implement Counter-Strike's player bots or claim CSO gameplay parity.

## Run

1. Put a model you own in the user's `%APPDATA%/Modern-Client.v2/addons/cstrike/`, keeping its
   model, texture and sequence-group paths. No proprietary asset belongs in the repository.
2. Copy [`data/mods/zombies.toml`](../data/mods/zombies.toml) to that folder's
   `entities/zombies.toml`. Set its model path and the exact sequence labels that model has.
   Death must be non-looping; attacks may loop and are paced by the server's cooldown. Missing
   files, hitboxes, sequences and invalid numeric values are reported before a body is created.
   Copy it again when the example gains a table, as it gained `stuck`: a missing key is reported.
3. Build `next` as [17](17-build-and-tools.md) describes, then run:

   ```powershell
   .\build\dev-windows-msvc\bin\next.exe +mp_freezetime 0 +map de_dust2 +zombie_add 64
   ```

In the console, `zombie_add [count]` uses the map's terrorist spawn points as the base,
independently of the player's position. It selects clear, reachable base points and polygon
corners/centers, nearest the base first and within `spawnRadius` of a projected base point.
`spawnSpacing` separates them from live entity bodies horizontally. Bodies face the nearest
base point's map yaw. A map without terrorist spawn points reports an error.
Occupied or disconnected candidates are excluded. If there are too few valid positions,
the whole batch is rolled back. `spawnBatchLimit` limits one command, not the world population;
the example allows 64 per command within a radius of 512 units. Existing local configurations
must replace `spawnDistance` with `spawnRadius` and raise `spawnBatchLimit` to use 64.
`zombie_kick` removes the mod's bodies and corpses. Changing maps destroys them too.
`give ak47`, `give m4a1`, or the id of a local weapon gives a weapon to test against them.
For an isolated offline weapon test, `next --data <folder> --user-folder <folder>` selects
its data catalog and a separate settings/addons folder. Its loadouts may give the test weapons
on every spawn, with `weapon_<id>` binds in that folder's configuration. Proprietary models,
sounds and private study profiles stay outside the repository; unsupported special-mode data
is rejected, not silently ignored.

The example names the CSO model `models/player/zombi/zombi.mdl` and its `idle1`, `walk`,
`ref_shoot_knife` and `death1` sequences. It distributes none of its files. The configuration
and models are loaded once per process; restart after editing the definition.

## Boundaries and contracts

| Owner | Responsibility |
|---|---|
| `nx.navigation` | Recast/Detour meshes, profiles, queries and corridors; no BSP or game types |
| `nx.game` | Supplies map geometry and owns generic solid traces, placement and entity damage dispatch |
| `nx.agents` | An ECS `Agent` component: bounded last-seen memory, decisions and intent; no body mutation |
| `cs.shared` | Append-only `EntityCatalog`, typed definition/model indices, validated model sequences and snapshot data |
| `cs.server` | Generic entity bodies, movement, melee timing, animated hitboxes, damage and death; no zombie rules |
| `cs.client` | Entity snapshot presentation and GPU models, including the existing material-file path |
| `mod.zombies` | Zombie settings, sight budgets, target observations, brains and their commands |
| `app.client` | Composes the mod with the local server and console |

An entity has a generational ECS handle. Client interpolation keys positions by the full
handle, so a recycled slot never inherits a previous entity's motion. It consumes no player slot and changes no team's
player counts, money or round objectives. The first body adapter uses the standing player
hull and movement; its navigation profile excludes crouch-only passages. Different body
shapes need their own adapter and matching profile, rather than a kind flag.

The game owns tick duration: an entity command cannot speed up simulation. Movement goes
through collision and door touches. Bullets and the knife meet posed hitboxes, with the
definition's hit-group multipliers. Death removes solidity once, stops movement and plays
the death sequence; subsequent attacks take no more health. Melee uses a solid trace and
a server-clock cooldown, which also bounds looping attack animations. An NPC strike on a
player enters the existing harm/death pipeline.
Players attacking each other go through `cs::Game::attackPlayer` instead, with the players' hit
groups, armor and combat rules ([10 §7.4](10-counter-strike-gameplay.md#74-damage)).

Sight uses the configured range, field of view, per-agent candidate budget and the server's
solid traces, through the solids gathered once for the tick's sights (`cs::Game::sightSolids`); a
body's hitboxes are placed once for its pose and kept ([07 §4.2](07-simulation-and-physics.md#42-entities)). A brain forgets each
target that is no longer among the living players (`nx::agents::Brain::forgetAbsent`): a dead or
departed enemy's last position is neither investigated nor planned a way to. A brain pursues a visible target or its last observed position, never an unseen
current position. The server supplies the target's feet from its active standing/crouched hull,
separately from its eye position; the mod does not infer the target's stance. Memory expires
or is forgotten after reaching the corridor's walkable destination, including when the target
was last seen airborne. Targets are projected onto the final polygon, including its boundary,
so wall-clamped and airborne targets retain a valid floor height. Small observation changes
update the corridor's destination without a full search; retargeting keeps both the destination
and its last corner on the walkable floor. A failed
path yields `Blocked` with its navigation error; it does not enable direct movement. The mesh
is built once per map as it loads, in jobs, overlapping the client's level
(`nx::navigation::MeshBuild`), or read from the cache folder where an earlier load kept it, and the
horde and the bots borrow it: `zombie_add` and `bot_add`
build nothing. `nav_draw` draws the mesh over the scene and the way each living bot and zombie follows
on it while it moves along its corridor, as arrows in the colors of its team and of why it moves
(magenta heads while it is stuck), found
again once a tick in batches in jobs; a dead body's agent, which decides nothing, is not drawn
(`cs::Game::bodyStatusOf`). `nav_generate` builds the mesh
again, what the cache keeps left aside, and plays the map again on it, as Condition Zero restarts
the game once it analyzed a map. A brain's decision that moves is steered along its corridor among the
six nearest living bodies, players' and zombies', and the walls about it (`nx::agents::Brain::steer`,
`nx::navigation::Corridor::steer`: Detour's obstacle avoidance), its turns taken wide, waiting when it
must. A body stuck on its way all the same gets out as a bot does (`nx::agents::StuckWatch`, the
`stuck` table): it moves ahead, back or to a side, jumping now and then, and when it stays stuck it
searches another way, which keeps away from where it got stuck (`nx::navigation::PathRequest::avoided`)
while it chases. A body farther across from its corridor than its replan distance, teleported or
pushed off the mesh, searches its way anew from where it is. Each tick the bodies perceive one by one, then their brains decide together in
batches in jobs (`nx::agents::Deciders`), each batch with a query of its own and each decision at
its body's index, so that the commands are the same with any number of workers. Agent commands and
body updates have deterministic order. The session removes the
horde's owned bodies, including their borrowed corridors, before destroying the mesh. Native extensions attach typed components through `Game` without exposing its world.
The application owns each session as one unit: replacing a map destroys its horde before
its game, level and map, including when another map command replaces a pending session.

## Review and verification

The review for this change covers entity ownership, trace/damage routing, server/client
boundaries, the mod composition and the reused navigation implementation. It is not a
certification of every subsystem or of gameplay parity against the original.

Regression tests cover nonfinite and oversized navigation dimensions before integer
conversions, memory expiry, bounded memory, blocked paths, hit-group damage, death,
retired handles, player melee damage/cooldowns, local weapon hits and cleanup. Spawn tests
place 64 bodies in a base too narrow for a straight row, enforce their horizontal spacing
and roll back a partially placed batch when the reachable area cannot hold it. A corridor test
pushes an airborne target against a wall and checks its boundary position and floor height.
Base tests also reject missing points and prevent a full small base from spilling into the
rest of the map or removing previously created bodies.
Navigation determinism compares the same mesh, and the decisions of 100 brains over 8 ticks, with
0, 1, 3 and 15 workers. `nxBenchNavigation` measures navigation and pure agent memory/decisions
for 32, 64, 256 and 1024 agents, one by one and in batches in jobs; it does not represent the cost
of the full render/physics frame.

For a local lifetime/rendering smoke test, run `next --validation` with
`+map de_dust2 +zombie_add 3 +map de_dust2 +zombie_add 3`. This replaces a pending map that
already owns agents, then draws the replacement's agents. Append `++duck` to hold crouch
and exercise melee aiming at the lower eye position. Close normally so validation includes
resource destruction. Injected Vulkan overlays may need to be isolated for this check;
keep the explicitly requested Khronos validation layer enabled.

Dynamic navigation links/doors, PVS prefiltering, hearing, richer memory,
behavior extensions, local agent avoidance, other body adapters and the Rook bindings
remain explicit items in [TODO.md](TODO.md). None are simulated or reported as completed.
