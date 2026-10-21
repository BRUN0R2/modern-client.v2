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

/// Where the origin of a box `box`, about it, at `origin` rests once dropped to the floor: traced 256
/// units down by the server's rules, through the hull its size chooses (collision::boxHullOf), among the
/// world of `hulls` and the solid brush entities of `world`. Where it starts when it starts all in solid
/// or meets nothing.
[[nodiscard]] auto droppedToFloor(ecs::World& world, const collision::BrushHulls& hulls, const Aabb& box,
                                  Vec3 origin) -> Vec3;

} // namespace nx::game
