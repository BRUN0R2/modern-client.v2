// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What moves brush entities (docs/07-simulation-and-physics.md, section 5): a player's touch
/// opening a door or pressing a button, and the pushers moving and turning each frame, as the
/// original's game and engine move them (CBaseDoor, CBaseButton, SV_Physics_Pusher), written anew from
/// the GPL code that reconstructs them (docs/20-legal-and-licensing.md, section 3).
export module nx.game:movers;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;
import :components;
import :clock;

export namespace nx::game {

/// `toucher` touched the brush entities of `models`, in their order: a door a touch opens starts
/// opening, a turning one away from the toucher, and one that stays open closes
/// (CBaseDoor::DoorTouch); a button pressed by touch goes in, or comes back out
/// (CBaseButton::ButtonTouch); their models are those of `hulls`.
void touchBrushEntities(ecs::World& world, const collision::BrushHulls& hulls,
                        std::span<const collision::ModelIndex> models, ecs::Entity toucher);

/// Advances each pusher of `world`, in the world's order, by a tick of the world's clock (TickTime),
/// at its time (SV_Physics_Pusher): it moves at its velocity, and turns at its angular one, while it
/// has something to do by its clock, carrying each Body it runs into, or that stands on it, along
/// with it through the world of `hulls` and the other solid brush entities; one that cannot be
/// carried stops it where it was and it gives way (CBaseDoor::Blocked). Then it acts when its clock
/// reaches its time: a door or a button arrives, a door that waited open closes, a button that waited
/// in comes back out.
void runPushers(ecs::World& world, const collision::BrushHulls& hulls);

} // namespace nx::game
