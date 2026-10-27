# ADR-0024 — Agents: one system for bots, monsters, bosses and every character the game moves

- **Status:** Accepted, amended by [0028](0028-native-agent-test-mod.md) for the first native test mod
- **Date:** 2026-10-23

## Context

The maintainer wants bots, zombies, monsters, bosses and other characters to be one generic,
decoupled system, with a memory, that mods use through a simple API, the same on a server and
offline. Bots are to use the same intent API as players, with no gameplay shortcuts
([10 §11](../10-counter-strike-gameplay.md#11-bots-milestone-m7)).

Calling every one of them a non-player character with a flag (bot, boss, npc) mixes two
different things. A bot is a **player**: it takes a slot of `maxplayers`, shows on the scoreboard
and in `status`, has a team and money, buys, counts for the round's end and for `getPlayers`, and
is replicated as a player. A monster or a boss is an **entity** of the world: no slot, any size,
its own way of moving and its own `vars`. A flag would put `if (kind == bot)` in every rule that
cares.

The maintainer also wants each kind (the bots, a mod's zombies, its bosses) to be its own module,
independent of the others, over that one system, and the bots smarter than Condition Zero's, with a
memory of where the players were.

## Decision

A module **`nx.agents`** (layer 4, beside `nx.game`) makes agents of entities of the
ECS. An agent is an entity with an `Agent` component and the parts it uses: `Perception`,
`Memory`, `Behavior` and `NavigationAgent` (a profile of [ADR-0023](0023-navigation.md), its path
and its steering).

- **One brain, two bodies.** An agent drives its body only by **intent**: a direction to move,
  angles to look along, buttons, the command a player's client sends. It never sets a position or
  a health. A bot drives a player's body, and the game's rules treat it as a player; a monster,
  a boss or a hostage drives an entity's body, moved as its class moves (as a player does, by
  steps, flying). The agent's systems never ask which.
- **One module for each kind.** `nx.agents` is the brain every kind shares, and it knows no kind:
  perception, memory, behaviors, steering. Each kind of agent is its own module above it, which
  brings the kind's body, behaviors and rules and depends on no other kind's: removing one changes
  nothing in the others. Counter-Strike's bot is `cs.bots`; the kinds a mod makes, such as zombies
  and bosses, are modules of the mod, in Rook (M6, with the port of ReZombie,
  [16](../16-amxx-migration.md)), on the same API, since the repository plays Counter-Strike 1.6
  alone ([ADR-0017](0017-counter-strike-only.md)). Until then, the hostages, Counter-Strike's own
  characters, are the first kind with an entity's body, following the original's rules.
- **Roles are classes and tags, not a flag.** Boss, zombie, hostage, ally, escort are classes
  ([ADR-0022](0022-plugin-api-primitives.md)) and typed tags (`tag Boss;`) a mod declares; a boss's
  health bar or a round rule looks for the tag.
- **Perception.** Sight (a field of view, a range, a line through what is solid, the potentially
  visible set first), hearing (the sounds the game emits, by their loudness and place) and
  touch and damage. It runs within a budget per tick, each agent looking at a share of what it
  could see, and writes what it perceives into memory.
- **Memory**, the same for every agent:
  - **Perceived**: a record for each thing sensed — who or what, how (seen, heard, hurt by),
    where last and how fast, when — whose confidence falls with time while the area it may be in
    grows; below a threshold it is forgotten. The limit is the agent's class's setting, the
    weakest record going first: no fixed cap.
  - **Working**: a typed board for each agent (its target, its goal, its cover, its timers), its
    keys declared like a `vars` catalog and checked at compile time.
  - **Shared**: a board for each team or faction, fed by reports (the radio) after a delay, so
    that a squad or a horde knows what one of them told.
  - **Learned** (optional): for each map, the areas where enemies were seen and players died, kept
    in the navigation's cache between matches.
  - **Only from perception**: an agent knows what it sensed or was told, never what it could not
    (no wall hacks).
- **Behavior.** A hierarchical state machine whose transitions are chosen by utility scores:
  predictable, readable when it is debugged, and how Condition Zero's bot already decides. Built-in
  behaviors: wander, patrol, move to, chase, flee, attack (melee, ranged), take cover, follow,
  guard, investigate. A behavior is three methods (enter, update giving a status, exit), written
  in C++ or in Rook.
- **Moving.** An agent follows its navigation corridor, avoids other agents and players locally
  (velocity obstacles), and takes doors, ladders and jumps by the mesh's links.
- **Reaction is a setting, not a cost.** How long an agent takes to react and how well it aims
  are its class's and difficulty's values (`bot_difficulty`); the computation itself stays within
  its budget.
- **Server or offline.** Agents run where the simulation runs, on the server; offline is the
  local server ([08](../08-networking.md)); clients never run them. Ticks are fixed, each agent
  draws from its own random stream ([07 §2.3](../07-simulation-and-physics.md#23-shared-seed)),
  and pure decisions run in jobs: the same inputs give the same match, for replays and tests.
- **Counter-Strike's bot** is the module `cs.bots` (layer 5, beside `cs.server`, declared lateral):
  its bots join the server's game as players and hand it, each tick, the commands their agents
  make, as a client's arrive; offline, the game is the local server's. Buying, objectives, the radio
  and its difficulty follow ReGameDLL_CS's reconstruction of Condition Zero's bot (GPL, consulted),
  with the commands of [10 §11](../10-counter-strike-gameplay.md#11-bots-milestone-m7): `bot_add`,
  `bot_add_t`, `bot_add_ct`, `bot_kick`, `bot_quota`, `bot_difficulty`, `bot_chatter`. It is
  smarter by its memory, never by cheating: where each enemy was last seen or heard and where it
  may have gone since along the mesh, what its team reported, where players died on the map.
- **Rook API** (M6, in full words): a class with an agent part; `setGoal(agent, Goal.MoveTo(position))`,
  `chase(agent, target)`, `remember`, `recall`, `forget`, `knownThreats(agent)`,
  `getAgentMemory`/`setAgentMemory` (and `get_agent_memory`/`set_agent_memory`,
  [ADR-0021](0021-rook-amxx-names.md)); hooks on the agent's methods (`Agent.perceived`,
  `Agent.decide`); a behavior as a Rook class implementing `Behavior`.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| One kind of character with a flag (bot, boss, npc) | Players and world entities differ in every rule that counts players: the flag would spread through the rules |
| One module holding every kind | Each kind's body and rules would sit together: a mod's boss would change the module the bots live in, and no kind could be removed alone |
| A system for bots and another for monsters | Navigation, perception, memory and behaviors written twice, and mods choosing between them |
| Behavior trees | Fine for authored characters, but transitions by score fit a bot's shifting priorities better, and Condition Zero's bot already decides this way |
| Goal-oriented planning (GOAP) | A planner's choices are hard to predict and to debug, and cost more per decision |
| Learned behavior (machine learning) | Unpredictable, untestable by rules, and data the project does not have |
| Agents setting their bodies directly | Shortcuts players do not have, and two ways for every move |

## Consequences

- ✅ A mod's boss, horde or companion is a class with an agent part: navigation, perception,
  memory and behaviors come with it.
- ✅ Bots play by the players' rules: every intent goes through the same movement and weapons.
- ✅ Each kind is a module of its own: the bots, a mod's zombies and its bosses come and go
  independently, on one API.
- ✅ The same match on a server and offline, replayable.
- ⚠️ Perception and decisions for many agents cost time. Mitigation: budgets per tick, jobs, and
  benchmarks at 64, 256 and 1024 agents.
- ⚠️ Memory without a cap must still be bounded. Mitigation: each class's limit, the weakest
  record forgotten first, and the size reported by the agents' statistics.
- ⚠️ The game's bot depends on rounds, damage, buying and objectives, which come first.

The change that accepted it amended [01 §1.1](../01-architecture.md#11-module-catalog),
[10 §11](../10-counter-strike-gameplay.md#11-bots-milestone-m7), [15](../15-mod-api.md),
[19](../19-roadmap.md) and the M6 and M7 items of `TODO.md`.
