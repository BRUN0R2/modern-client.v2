// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The moves of the game's toggles, doors and buttons (not exported): a pusher sliding or turning to
/// where it goes, as the original's CBaseToggle moves it (LinearMove, AngularMove, LinearMoveDone,
/// AngularMoveDone), and what a toggle does when it gets there or when its wait is over.
export module nx.game:toggles;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;

namespace nx::game {

/// `angles` as a vector: pitch, yaw and roll.
[[nodiscard]] auto vectorOf(EulerAngles angles) -> Vec3;
[[nodiscard]] auto eulerOf(Vec3 angles) -> EulerAngles;

/// Slides pusher `entity` towards `destination` at `speed` (LinearMove): it arrives at once when it is
/// there already, else moves at the velocity that brings it there by its clock.
void moveTo(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity, Vec3 destination,
            UnitsPerSecond speed);

/// Turns pusher `entity` towards `destination` at `speed` (AngularMove), as moveTo slides it.
void turnTo(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
            EulerAngles destination, DegreesPerSecond speed);

/// Pusher `entity`'s clock reached when it acts (its think): a door that waited open closes, a button
/// that waited in comes out; else the move under way ends where it was going (LinearMoveDone,
/// AngularMoveDone) and the toggle arrives, a door opened or closed, a button in or out.
void thinkToggle(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity);

} // namespace nx::game
