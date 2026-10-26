// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What an attack does to an entity it meets (docs/07-simulation-and-physics.md, section 5.3), as the
/// original's game deals it (TraceAttack, then ApplyMultiDamage and TakeDamage): each kind takes it its
/// own way, its DamageMethod (ADR-0022's `Entity.takeDamage`); an entity of none takes none.
export module nx.game:damage;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;

export namespace nx::game {

/// An attack on an entity a trace met: who made it, none for the world; how much damage it deals as
/// it is made; the way it goes (the original's vecDir); and the trace that met the entity, its hit
/// group among it.
struct Attack {
    std::optional<ecs::Entity> attacker{};
    Damage damage{};
    Vec3 direction{};
    collision::Trace trace{};
};

/// What an attack did to an entity: the damage it took, after what its kind makes of where it was met,
/// and whether it killed it.
struct Harm {
    Damage damage{};
    bool isKilled = false;
};

/// How an entity of a kind takes an attack (its TraceAttack and TakeDamage), given by what makes it.
struct DamageMethod {
    Harm (*takeAttack)(ecs::World& world, ecs::Entity entity, const Attack& attack) = nullptr;
};

/// Deals `attack` to `entity` of `world` as its kind takes it (DamageMethod): what it did; nothing to
/// an entity of a kind that takes none.
auto attackEntity(ecs::World& world, ecs::Entity entity, const Attack& attack) -> Harm;

} // namespace nx::game
