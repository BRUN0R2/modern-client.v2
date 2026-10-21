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

auto droppedToFloor(ecs::World& world, const collision::BrushHulls& hulls, const Aabb& box, Vec3 origin)
    -> Vec3 {
    const collision::BoxHull boxHull = collision::boxHullOf(box);
    const Vec3 start = origin - boxHull.offset;
    const collision::Trace trace = serverTrace(hulls, solidBrushesOf(world), std::nullopt, boxHull.hull,
                                               start, start - Vec3{.z = kDropReach});
    if (trace.isAllSolid || trace.fraction == 1.0F) {
        return origin;
    }
    return trace.end + boxHull.offset;
}

} // namespace nx::game
