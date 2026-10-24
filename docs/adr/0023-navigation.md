# ADR-0023 — Navigation meshes for every kind of agent, in `nx.navigation`

- **Status:** Proposed
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

Proposed: a module **`nx.navigation`** (layer 3) builds, keeps and answers navigation meshes of
convex polygons, through Recast and Detour.

- **Decoupled.** It depends on `nx.core`, `nx.math` and `nx.jobs`, and builds from the geometry it
  is given: walkable triangles with an area kind each, and links. It knows nothing of BSP files,
  entities or the game; `nx.game` gives it the map's geometry and the state of its doors. Recast
  and Detour (zlib, the vcpkg port `recastnavigation`) stay behind the module's own types, built
  without exceptions or RTTI as they are ([RULES §9](../RULES.md#9-dependencies)).
- **Profiles.** An agent's size and moves are a profile in data
  (`data/cstrike/navigation.toml`): radius, height standing and crouched, step, jump, steepest
  walkable floor. Each profile has its own mesh. The player's (16, 72 and 36, 18, 41.8, the
  movement's floor) is the first; a mod adds its own (a boss of 64 by 128 that no narrow door
  lets through).
- **Areas and costs.** Each polygon has a kind: floor, crouch, water, ladder, jump, door,
  danger… Each profile and each query weighs the kinds, so that a bot avoids danger and a zombie
  does not.
- **Links.** Ladders, jumps, drops and teleporters are links between polygons, found in the map
  (`func_ladder`, ledges, `trigger_teleport`) and added by plugins.
- **What changes in a match.** Doors and breakables turn their areas and links on and off as the
  game reports them, without rebuilding; moving agents avoid each other locally (ADR-0024).
- **Tactical layer.** Hiding spots, sniper spots, encounter spots, choke points and places (named
  regions, for the radio's "BombsiteA") are computed once for each mesh and kept with it.
- **Cache.** A mesh is built once for a map's content hash, a profile and the build's settings,
  and kept as tiles in a cache file, read back without parsing; any change of the three rebuilds
  it. The first build runs in jobs while the map loads; `nxbake` can build them ahead for a
  server.
- **Queries.** `findPath` (A* over the polygons, then the funnel that straightens the path),
  `nearestPoint`, `raycast` (whether a straight walk is possible), `randomPoint` within a radius,
  `isReachable`, `pathCost`, `placeAt`, `hidingSpots`, and a corridor that follows an agent as it
  moves instead of searching again. Queries run in batches in jobs within a budget per tick, and
  give the same path for the same inputs, for replays and tests.
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

If accepted, the same change amends [01 §1.1](../01-architecture.md#11-module-catalog),
[06](../06-assets-and-resources.md), [10 §11](../10-counter-strike-gameplay.md#11-bots-milestone-m7),
[17 §5](../17-build-and-tools.md#5-dependencies) and the M7 items of `TODO.md`.
