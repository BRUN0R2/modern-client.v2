# ADR-0017 — Counter-Strike 1.6 only

- **Status:** Accepted
- **Date:** 2026-10-20

## Context

GoldSrc ran Half-Life and the games built on it, and Counter-Strike 1.6 is one of them. The
architecture left a door open to that family: [01 §7](../01-architecture.md#7-engine--game--mods-boundary)
described a `nx::game::GameModule` interface "so that, in the future, other GoldSrc games (e.g. HL
Deathmatch) can use the same engine". Nothing uses that interface, and keeping it in the design
shapes the engine around games the project does not make. The maintainer decided that this
version is Counter-Strike 1.6 alone: pure, clean and lean.

## Decision

- The project recreates **Counter-Strike 1.6 and nothing else**: no Half-Life single player, no
  Half-Life Deathmatch, and no other GoldSrc game or game mod (Team Fortress Classic, Day of
  Defeat, Condition Zero and the like).
- **No multi-game abstraction:** no game-module interface and no choice of game at start-up
  (`-game`, `liblist.gam` as a game selector). The engine (`nx.*`) and the game (`cs.*`) stay
  separate layers, because the engine not knowing the rules of Counter-Strike keeps both
  maintainable, but there is one game, linked statically.
- **Mods** means what the Counter-Strike community calls mods: Rook plugins grouped into mods
  that run on top of Counter-Strike 1.6 (a zombie mode, an admin system), not other games
  ([13](../13-mod-system.md)).
- **Content:** the `classic` profile reads Counter-Strike 1.6's own files from the user's
  installation, as the original game does: `cstrike/`, and behind it the base files of
  `valve/` that Counter-Strike 1.6 itself loads (WADs such as `halflife.wad` and
  `decals.wad`). Steam names that installation folder after Half-Life, its base game; nothing
  of Half-Life as a game is run, supported or redistributed
  ([ADR-0015](0015-free-content-and-optional-original-assets.md)).

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| Keep `GameModule` for future GoldSrc games | Infrastructure without a consumer ([RULES §5.3](../RULES.md#53-evolution)) that shapes the engine around games outside the project |
| Support Half-Life Deathmatch later as a second game | Doubles the parity work and the test matrix for a game the project does not aim at |
| Drop `valve/` from the `classic` profile | Counter-Strike 1.6 itself loads base files from there; its maps would miss textures and decals |

## Consequences

- ✅ One game: no abstraction to keep for hypothetical others, and every parity test aims at
  Counter-Strike 1.6.
- ✅ The scope is plain to contributors and players: a Counter-Strike 1.6 recreation.
- ⚠️ Another game on this engine would need a new ADR that supersedes this one, and the
  boundary between `nx.*` and `cs.*` to be reopened then, with a real consumer.
