// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What fires what among a map's entities (docs/03-ecs.md, section 10): an entity's name, the names
/// it fires, and what each entity does when it is used, as the original's game fires them
/// (FireTargets, CBaseDelay::SUB_UseTargets; ReGameDLL, GPL, consulted:
/// docs/20-legal-and-licensing.md, section 3).
export module nx.game:targets;

import std;
import nx.core;
import nx.ecs;
import nx.collision;

export namespace nx::game {

/// The name others fire an entity by ("targetname").
struct Named {
    std::string name{};
};

/// What an entity fires when it fires (CBaseDelay): after `delay`, the entities named `killTarget`
/// removed, then those named `target` used. Names are matched case and all.
struct Targets {
    std::string target{};
    std::string killTarget{};
    Seconds delay{};
};

/// An entity that fires its Targets when its NextThink comes, then goes (the original's DelayedUse):
/// what it fires them for.
struct DelayedUse {
    ecs::Entity activator{};
};

/// How deep the chain of targets that fire targets now is: a resource of the world, which
/// registerComponents gives it.
struct TargetChain {
    std::uint32_t depth = 0;
};

/// `caller` fires its Targets for `activator` (CBaseDelay::SUB_UseTargets): with a delay, through a
/// DelayedUse made to fire them then; else at once, the entities named by its killTarget removed,
/// then those named by its target used. Nothing without Targets.
void useTargets(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity caller,
                ecs::Entity activator);

/// Uses every entity named `name`, in the world's order, for `activator` (FireTargets): nothing for an
/// empty name. A chain of targets firing targets 128 deep stops there, as ReGameDLL stops a target
/// that fires itself.
void fireTargets(ecs::World& world, const collision::BrushHulls& hulls, std::string_view name,
                 ecs::Entity activator);

/// `entity` is used by `activator`: a door ready for it opens, or closes when it stays open
/// (CBaseDoor::Use); a button goes in, or a toggle comes back out (CBaseButton::ButtonUse), through
/// the world of `hulls`; a func_wall shows its other textures (CFuncWall::Use); anything else does
/// nothing yet. How it is used (USE_TYPE) comes with what
/// reads it (trigger_relay, multi_manager).
void use(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity, ecs::Entity activator);

} // namespace nx::game
