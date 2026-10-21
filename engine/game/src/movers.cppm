// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What moves brush entities (docs/07-simulation-and-physics.md, section 5): a player's touch
/// opening a door, and the pushers moving each frame, as the original's game and engine move them
/// (CBaseDoor, CBaseToggle::LinearMove, SV_Physics_Pusher), written anew from the GPL code that
/// reconstructs them (docs/20-legal-and-licensing.md, section 3).
export module nx.game:movers;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;
import :components;

export namespace nx::game {

/// A player touched the brush entities of `models`, in their order: a door a touch opens starts
/// opening, and one that stays open closes (CBaseDoor::DoorTouch).
void touchBrushEntities(ecs::World& world, std::span<const std::uint32_t> models);

/// Advances each pusher of `world`, in the world's order, by `frameTime`, at `time` since the map
/// started (SV_Physics_Pusher): it moves at its velocity while it has something to do by its clock,
/// moving each Body it runs into, or that stands on it, along with it through the world of `hulls`
/// and the other solid brush entities; one that cannot be moved stops it where it was and it gives
/// way (CBaseDoor::Blocked). Then it acts when its clock reaches its time: a door arrives, or closes.
void runPushers(ecs::World& world, const collision::BrushHulls& hulls, Seconds frameTime, Seconds time);

} // namespace nx::game
