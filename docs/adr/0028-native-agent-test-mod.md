# ADR-0028 — Exercise the entity and agent APIs with a native test mod

- **Status:** Accepted
- **Date:** 2026-10-27
- **Amends:** [0024](0024-agents.md)
- **Also amends:** [0023](0023-navigation.md), for ownership of navigation type metadata

## Context

The maintainer requests zombies that pursue and attack the local player, to test local
weapons offline before the Rook runtime exists. ADR-0024 places each mod's characters in
its own module and schedules their Rook implementation for M6. Putting zombies in
Counter-Strike's rules, or implementing movement and damage specifically for them, would
break that boundary.

## Decision

Bring the first consumer forward: `mod.zombies` is a native development mod, composed by
`app.client`, with its own commands and rules. It depends on `nx.agents` and `cs.server`,
never the reverse. This amends the timing and implementation language of ADR-0024's first
mod consumer; the Rook API and the Counter-Strike-only game remain the accepted direction.

The game provides generic humanoid entity bodies, driven by commands through its movement,
collision, animation and attack pipeline. They have generational entity handles and consume
no player slot. Their definitions and model sequences are validated before spawning.
The first body uses the existing standing player hull; other body shapes require their
own movement adapter and matching navigation profile.

`nx.agents` owns perception memory and decisions, and produces intent alone. The mod
supplies observations obtained through server traces and follows navigation corridors.
An unreachable target produces a visible blocked state; it never enables direct movement
through obstacles. NPC simulation runs in the server's tick, including offline play.

Navigation profiles and build settings own their `nx.meta` descriptions in `nx.navigation`.
The module therefore also depends on that lower-layer reflection facade. The game owns only
its map/configuration adapter; it does not specialize navigation's metadata in a different
module. This keeps type ownership and metadata together and avoids Clang 23.1.4's observed
crash mangling an imported type's description attached to another named module.

Models from the maintainer's local CSO installation stay in the user's addon folder.
Only synthetic fixtures and configuration examples enter the repository. This is a
development scenario, not a claim of CSO gameplay parity or the completed ReZombie port.

## Alternatives considered

- Waiting for M6 does not meet the requested offline weapon tests now.
- Making zombies players would change team counts, rounds and player slots.
- Adding zombie branches to `cs.server` would couple the game's rules to one mod.
- A second movement, damage or navigation implementation would duplicate existing owners.

## Consequences

The first real consumer validates the generic boundaries before Rook binds them. Bots can
later produce the same intent for player bodies. Native test-mod code remains GPL-covered;
the Rook Plugin Exception does not change its license. Advanced agent behaviors and other
movement adapters remain separate backlog items, without being reported as implemented.
