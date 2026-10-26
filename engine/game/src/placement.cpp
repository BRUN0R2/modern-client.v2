// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.game;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;

namespace nx::game {

namespace {

/// How far down the engine drops an entity to the floor.
constexpr float kDropReach = 256.0F;

} // namespace

auto droppedToFloor(ecs::World& world, const collision::BrushHulls& hulls, const Aabb& box, Vec3 origin,
                    std::optional<ecs::Entity> dropped) -> Vec3 {
    const collision::Trace trace = traceSolidsHit(world, hulls,
                                                  ServerTrace{.box = box,
                                                              .start = origin,
                                                              .end = origin - Vec3{.z = kDropReach},
                                                              .ignored = dropped})
                                       .trace;
    if (trace.isAllSolid || trace.fraction == 1.0F) {
        return origin;
    }
    return trace.end;
}

} // namespace nx::game
