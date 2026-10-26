// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Where the game puts an entity it makes (docs/07-simulation-and-physics.md, section 5): dropped to
/// the floor, as the original's engine drops it (DROP_TO_FLOOR, ReHLDS's PF_droptofloor_I, GPL,
/// consulted: docs/20-legal-and-licensing.md, section 3).
export module nx.game:placement;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;

export namespace nx::game {

/// Where the origin of a box `box`, about it, at `origin` rests once dropped to the floor, as the
/// engine drops entity `dropped` (DROP_TO_FLOOR): traced 256 units down through what is solid in
/// `world`, whose map's hulls are `hulls`, but itself (traceSolidsHit). Where it starts when it starts
/// all in solid or meets nothing.
[[nodiscard]] auto droppedToFloor(ecs::World& world, const collision::BrushHulls& hulls, const Aabb& box,
                                  Vec3 origin, std::optional<ecs::Entity> dropped) -> Vec3;

} // namespace nx::game
