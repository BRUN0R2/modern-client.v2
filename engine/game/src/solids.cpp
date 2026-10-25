// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.game;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;

namespace nx::game {

auto solidBrushesOf(ecs::World& world) -> std::vector<SolidBrush> {
    std::vector<SolidBrush> solids;
    ecs::Query<ecs::Entity, const BrushModel&, const Transform&, const BrushSolidity&>{world}.forEach(
        [&solids](ecs::Entity entity, const BrushModel& model, const Transform& transform,
                  const BrushSolidity& solidity) {
            if (solidity.solid == Solid::Bsp) {
                solids.push_back({.entity = entity,
                                  .model = model.index,
                                  .origin = transform.origin,
                                  .angles = transform.angles});
            }
        });
    return solids;
}

namespace {

/// serverTrace, and the entity of `solids` that stopped it, none for the world or nothing.
[[nodiscard]] auto serverHit(const collision::BrushHulls& hulls, std::span<const SolidBrush> solids,
                             std::optional<ecs::Entity> ignored, collision::Hull hull, Vec3 start, Vec3 end)
    -> SolidHit {
    SolidHit found{.trace = hulls.trace(collision::HullTraceQuery{.hull = hull, .start = start, .end = end})};
    collision::Trace& best = found.trace;
    for (const SolidBrush& solid : solids) {
        if (best.isAllSolid) {
            break;
        }
        if (solid.entity == ignored) {
            continue;
        }
        const collision::Trace hit = hulls.trace(collision::HullTraceQuery{.model = solid.model,
                                                                           .hull = hull,
                                                                           .offset = solid.origin,
                                                                           .angles = solid.angles,
                                                                           .start = start,
                                                                           .end = end});
        if (collision::keepFirstStop(best, hit)) {
            found.entity = solid.entity;
        }
    }
    return found;
}

} // namespace

auto serverTrace(const collision::BrushHulls& hulls, std::span<const SolidBrush> solids,
                 std::optional<ecs::Entity> ignored, collision::Hull hull, Vec3 start, Vec3 end)
    -> collision::Trace {
    return serverHit(hulls, solids, ignored, hull, start, end).trace;
}

auto traceSolids(ecs::World& world, const collision::BrushHulls& hulls, collision::Hull hull, Vec3 start,
                 Vec3 end) -> collision::Trace {
    return serverTrace(hulls, solidBrushesOf(world), std::nullopt, hull, start, end);
}

auto traceSolidsHit(ecs::World& world, const collision::BrushHulls& hulls, collision::Hull hull, Vec3 start,
                    Vec3 end) -> SolidHit {
    return serverHit(hulls, solidBrushesOf(world), std::nullopt, hull, start, end);
}

} // namespace nx::game
