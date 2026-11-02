# ADR-0030 — The server's game is parts over one state, behind one class

- **Status:** Accepted
- **Date:** 2026-11-02

## Context

`cs::Game` is the game of a map as its server plays it. An audit against [RULES](../RULES.md) on
2026-11-02 found it the project's largest risk of the kind §5.1 and §13 forbid: about 72 member
functions, and 1355 lines in `game.cpp`, mixing how a player's model plays, where it spawns, what it
buys, how a hit hurts it, what a command does and what a client is told. Every gameplay part still to come
(grenades, the bomb, hostages following, dropping weapons) would land there. The maintainer asked for the
split.

Those concerns are not independent: they read and change the same players and world. What can be
separated is the code, and the order in which it may call itself.

## Decision

- **`cs.server:gameState`** holds what the game is played with and where it stands, as plain data:
  `GameState` (its content and map, cvars, models, rounds, accounts, wins, world, players, what the tick
  made, light styles), `Player` and `TickEvents`. Neither is exported.
- **Each concern is a partition of free functions over it**, the state their first parameter, none exported:
  `:playerPose`, `:playerSpawns`, `:gameRules`, `:playerHarms`, `:playerCommands`, `:purchases`,
  `:playerSnapshots`. Rules that need no state stay pure in `cs.shared`, as `cs.shared:buying` shows: a
  part weighs by them and acts.
- **They call each other in one direction:** pose, then spawns, then rules, then harms, then commands.
  Purchases and snapshots stand on the state and on what is below them. No cycle: an importing partition
  sits above what it imports, which the compiler checks.
- **`cs::Game` stays what importers see**, its public interface unchanged: it holds the state, owns the
  tick's order, and passes each of its calls on. Bots, mods, the client and the tests do not change.

## Alternatives considered

- **Splitting `game.cpp` into several files, the class whole.** Shorter files with the same 72 members and
  the same one scope: it moves the problem instead of solving it.
- **A class for each concern, each given the references it needs.** The narrowest interfaces, but the
  concerns need nearly all of the state and each other, so each constructor would list most of it and the
  classes would hold pointers to each other, a cycle `GameState` does not have.
- **Entities and ECS systems for the players.** Where the engine may go, and not a refactoring: `Player` keeps
  the original's `CBasePlayer` order of operations, which systems would have to state again. It needs its own
  decision, with a consumer.

## Consequences

- A new gameplay rule has one obvious home, and a part's interface tells what it does in a few declarations.
- A part can reach all of the state: its first parameter does not say which of it it uses. Where a function
  needs little it takes only that (`eyeOf`, `playersIn`), and rules that can be pure live in `cs.shared`.
- `Game::~Game` is defined in `game.cpp`, so that importers do not instantiate the state's destructor.
