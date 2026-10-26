// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The entities that act at a time of the game's clock (docs/07-simulation-and-physics.md, section
/// 5.2), as the original's engine runs their think (SV_RunThink, written anew from ReHLDS, GPL,
/// consulted: docs/20-legal-and-licensing.md, section 3). Pushers act by clocks of their own
/// (runPushers).
export module nx.game:thinking;

import std;
import nx.core;
import nx.ecs;
import nx.collision;

export namespace nx::game {

/// When an entity acts next, by its world's clock (the original's pev->nextthink).
struct NextThink {
    GameTime time{};
};

/// What an entity of its kind does as it acts, its kind's think (ADR-0022's `Entity.think`), given by
/// what makes it: called with `at`, when it was due, or the tick's start for one due before, as the
/// original's engine sets the time of a think (SV_RunThink).
struct ThinkMethod {
    void (*think)(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
                  GameTime at) = nullptr;
};

/// `entity` acts next at `time`, instead of when it would have.
void setNextThink(ecs::World& world, ecs::Entity entity, GameTime time);

/// Runs what acts during this tick of `world`'s clock (TickTime): each entity whose NextThink comes
/// before the next tick, from the earliest, then by the entity, its NextThink taken off first: one of a
/// kind with a ThinkMethod does what it says; a DelayedUse fires its targets and goes; a Trigger that
/// waited may fire again, and a spent one goes. A target fired opens a door through the world of
/// `hulls`.
void runThinks(ecs::World& world, const collision::BrushHulls& hulls);

} // namespace nx::game
