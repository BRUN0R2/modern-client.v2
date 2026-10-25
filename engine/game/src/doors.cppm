// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What doors do (docs/07-simulation-and-physics.md, section 5), as the original's game makes them do
/// it (CBaseDoor, CRotDoor; ReGameDLL, GPL, consulted: docs/20-legal-and-licensing.md, section 3); not
/// exported: a player's touch, a target's use and the pushers reach them through what is.
export module nx.game:doors;

import std;
import nx.core;
import nx.ecs;
import nx.collision;
import :clock;

namespace nx::game {

/// Door `entity` starts opening (CBaseDoor::DoorGoUp): a turning one away from what opened it, but
/// one-way.
void openDoor(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity);

/// Door `entity` starts closing (CBaseDoor::DoorGoDown).
void closeDoor(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity);

/// Door `entity` restarts as a round starts (CBaseDoor::Restart): taken as closed, it closes from where
/// it is, as DoorGoDown closes it, arriving at once where it is closed already; a touch opens it again
/// meanwhile, unless only use does.
void restartDoor(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity);

/// Door `entity` reached where it was going (CBaseDoor::DoorHitTop and DoorHitBottom): it waits or
/// rests, and fires its targets for what moved it.
void doorArrived(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity);

/// `toucher` touched door `entity` (CBaseDoor::DoorTouch): one a touch opens, not moving, opens, or
/// closes when it stays open.
void touchDoor(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
               ecs::Entity toucher);

/// `activator` uses door `entity` (CBaseDoor::Use): closed, it opens; open for good until used again
/// ("toggle"), it closes; else nothing.
void useDoor(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
             ecs::Entity activator);

/// Something door `entity` could not push stopped it at `clock`'s time (CBaseDoor::Blocked): it goes
/// back the way it came, unless it stays open for good or was stopped just before.
void blockDoor(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
               const TickTime& clock);

} // namespace nx::game
