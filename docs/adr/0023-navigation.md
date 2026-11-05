# ADR-0023 — Navigation meshes for every kind of agent, in `nx.navigation`

- **Status:** Accepted, amended by [0028](0028-native-agent-test-mod.md) for type metadata ownership
- **Date:** 2026-10-23

## Context

Bots are planned with a navigation mesh ([10 §11](../10-counter-strike-gameplay.md#11-bots-milestone-m7)),
and the maintainer wants the same navigation for every agent a mod creates: zombies, monsters,
bosses, hostages ([ADR-0024](0024-agents.md)). It has to load fast, answer in short time, work
the same on a server and offline, and stay simple for mods to create and read.

The original Counter-Strike 1.6 has no bots. Condition Zero's bot, which ReGameDLL_CS
reconstructs (GPL, consulted: [20 §3](../20-legal-and-licensing.md#3-clean-room)), walks a `.nav`
mesh of axis-aligned areas found by a flood fill every 25 units: minutes to generate on a large
map, one agent size, and paths that zigzag on ramps and diagonals. Its constants are the
player's: a step of 18 units, a jump of 41.8, half a width of 16, a height of 72.

## Decision

A module **`nx.navigation`** (layer 3) builds, keeps and answers navigation meshes of
convex polygons, through Recast and Detour.

- **Decoupled.** It depends on `nx.core`, `nx.math` and `nx.jobs`, and builds from the geometry it
  is given: walkable triangles with an area kind each, and links. It knows nothing of BSP files,
  entities or the game; `nx.game` gives it the map's geometry and the state of its doors. Recast
  and Detour (zlib, the vcpkg port `recastnavigation`) stay behind the module's own types, built
  without exceptions or RTTI as they are ([RULES §9](../RULES.md#9-dependencies)).
- **Profiles.** An agent's size and moves are a profile in data
  (`data/cstrike/navigation.toml`): radius, height standing and crouched, step, jump, steepest
  walkable floor, and the clearance it would rather keep from walls where there is room (the floor
  within it of the kind edge, weighed as the other kinds are). Each profile has its own mesh. The player's (16, 72 and 36, 18, 41.8, the
  movement's floor) is the first; a mod adds its own (a boss of 64 by 128 that no narrow door
  lets through).
- **Areas and costs.** Each polygon has a kind: floor, crouch, water, ladder, jump, door,
  danger… Each profile and each query weighs the kinds, so that a bot avoids danger and a zombie
  does not. A query also weighs places of its own, the polygons about them costing more (an agent
  keeping away from where it got stuck), through a query filter of ours: Detour is built with its
  filter virtual for it.
- **Links.** Ladders, jumps, drops and teleporters are links between polygons, found in the map
  (`func_ladder`, ledges, `trigger_teleport`) and added by plugins. Jumps and drops are found at the
  ledges as each tile is built: along each edge of a polygon that no other shares, one every two
  widths of the profile, where a body walking off lands on floor within its drop height below, its
  radius and a cell past the ledge; of the kind jump, both ways, within its jump height, else drop,
  down only, both heights the geometry's. A brain crosses one from its start, jumping once for a
  link up, having slowed down for what the link's way turns across its own, since in the air a body
  flies on, and heads for its end without steering, as DetourCrowd moves an agent over a link. It has
  crossed only on the ground, at the height of the end and as far from the start; back on the ground
  below that height it fell short, and searches its way anew from where it stands. The
  ledge its next link leaves from is no wall to its steering. A decision reuses its next-link lookup
  to anticipate the way over it when braking; after an attempted crossing fails and may have changed
  the corridor, it looks again.
- **What changes in a match.** Doors and breakables turn their areas and links on and off as the
  game reports them, without rebuilding; moving agents avoid each other locally (ADR-0024).
- **Tactical layer.** Hiding spots, sniper spots, encounter spots, choke points and places (named
  regions, for the radio's "BombsiteA") are computed once for each mesh and kept with it.
- **Cache.** A mesh is built once for a map's content hash, a profile and the build's settings,
  and kept as tiles in a cache file, read back without parsing; any change of the three rebuilds
  it. The first build runs in jobs while the map loads; `nxbake` can build them ahead for a
  server.
- **Ladders.** A `func_ladder` joins the floors it leads to by links of the kind `Ladder`, both ways:
  each floor past a side of it with no wall in the way, from one a body's top reaches its bottom from up
  to one a step above its top, a body over a wall on top of it as much as in front of it. An agent climbs
  by its keys and its view, as a player does (`PM_LadderMove`): up facing it level; down walking off toward
  it looking 60 degrees below level, which brings a body from behind it round to its front. What an
  agent asks of a body holds the keys of its speeds (`cs::holdKeysOf`), as a client's do: on a ladder a body
  moves by its keys alone, none held holding it there. A body off the mesh jumps, which lets go of one.
- **What no surface shows.** A map's clip brushes stop bodies and have no face: cs_assault's gates are
  `func_illusionary` fences, drawn and not solid, each with a clip brush of the same shape. The geometry carries
  the world's standing hull (`BodyHull`), a copy of the planes of the tree the map's compiler grew by the player's
  box, and a tile's floor keeps only where a standing body's middle is out of its solid, within a step, or what
  the steepest floor lifts its box by, of where it stands; a link's way, a ladder's landing the same. The floor
  is tested after its erosion from walls: the hull already keeps its radius from what it stops. A solid brush
  entity has its own hulls, about where it stands, unturned, which its faces alone tell nothing of: a beam
  a body ducks under is low to its head as much as a ceiling. Another profile's size will need its own hull.
- **Crouching.** Floor where a standing body does not fit and a ducked one does, by the world's ducked hull as
  much as by a low ceiling, is of the kind `Crouch`. An agent holds its duck key from six of its radii ahead of
  such floor along its way, as far as a body at full speed goes while it ducks and a decision holds, until it is
  past it (`Intent::crouches`); its keys ask a third of their speed then, so that it asks all of it however it
  steers. Off the ground a duck is at once: across a link onto such floor it ducks only as it gets there, as a
  body ducked at the top of a ladder walks to it at a third of its speed. It gains speed slowly ducked, 83 units
  a second each second, and a jump's cost for a second takes all of it: its stuck watch holds it to a third of
  the stuck speed, and draws no jump. A ledge higher than its jump and within its crouched one is linked both
  ways by the kind `CrouchedJump`, 58 units as a Condition Zero bot has it: the movement reaches 45 units
  jumping and 63 ducking in the air, its feet lifted by as much as its box is shorter, whenever from the jump to
  its top it presses the key, so that a brain holds the key from the jump until it lands.
- **Queries.** `findPath` (A* over the polygons, then the funnel that straightens the path),
  `nearestPoint`, `raycast` (whether a straight walk is possible), `randomPoint` within a radius,
  `isReachable`, `pathCost`, `placeAt`, `hidingSpots`, and a corridor that follows an agent as it
  moves instead of searching again. Queries run in batches in jobs within a budget per tick, and
  give the same path for the same inputs, for replays and tests. A corridor reuses its query's scratch
  for corners and bounds it by the corridor's polygons, even when more corners are requested. A way
  that is not there is known without a search, the most a search can cost: a mesh keeps which polygons
  lead to which, links one only drops down going one way, as it takes its tiles.
- **Build failures.** Mesh building propagates `MeshBuildError`; a tile refused by Recast carries
  the tile's coordinates, the failed step and Recast's diagnostic when available, including when
  it marks the floor along walls.
- **Tools.** Console commands in Condition Zero's names: `nav_generate`, `nav_draw` (the mesh, its
  links and the agents' paths drawn in `next`), `nav_edit` (marked areas and added links, kept as
  an overlay a rebuild applies again) and `nav_check` (islands, a spawn that cannot reach a
  bombsite).
- **Rook API** (M6, in full words):
  `findPath(from, to, profile) -> Result<NavigationPath, NavigationError>`, `nearestPoint`,
  `isReachable`, `randomPoint`, `hidingSpots(near, radius)`, `placeAt(position)`,
  `addLink(from, to, NavigationLink.Jump)`, `markArea(box, NavigationArea.Danger)`;
  `NavigationError` is `NoMesh`, `OutsideMesh{position}`, `Unreachable`, `UnknownProfile{name}`.
- A `.nav` file of Condition Zero's format may give its places and hiding spots to the tactical
  layer; the mesh itself is always built.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| Condition Zero's `.nav` mesh, as ReGameDLL generates it | Axis-aligned areas that zigzag on ramps and diagonals, minutes to generate a large map, one agent size |
| A mesh generator of our own | Voxelization, region partition and contour tracing are a large, subtle body of code that Recast has done for years for Unreal, Unity and Godot; the rules prefer a dependency when the complexity is high |
| Waypoints placed by hand (PODBot, YaPB) | Work for every map, breaks with every change, nothing for a new agent size |
| A* over a voxel grid | Memory and time grow with the map's volume, paths along cells |
| Surfaces made from a clip hull's tree, given to Recast with the faces | The tree is grown by the player's box: its floors stand 36 units above the map's and its walls 16 out, with no way back to the brushes |

## Consequences

- ✅ One navigation for bots and for every agent a mod creates, sized by a profile.
- ✅ Loads in the time it takes to read its tiles; queries in batches, within a budget.
- ✅ The module can be tested alone, with geometry built in the test.
- ⚠️ A new dependency, reviewed as [17 §5](../17-build-and-tools.md#5-dependencies) asks (zlib,
  maintained, small surface).
- ⚠️ A mesh for each profile costs memory and a first build. Mitigation: profiles are built when
  an agent of theirs first appears, and measured.
- ⚠️ A profile must match the movement it stands for. Mitigation: a test on every official map
  that each spawn reaches each objective with the player's profile.

The change that accepted it amended [01 §1.1](../01-architecture.md#11-module-catalog),
[06](../06-assets-and-resources.md), [10 §11](../10-counter-strike-gameplay.md#11-bots-milestone-m7),
[17 §5](../17-build-and-tools.md#5-dependencies) and the M7 items of `TODO.md`.
