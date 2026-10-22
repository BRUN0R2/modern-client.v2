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

auto serverTrace(const collision::BrushHulls& hulls, std::span<const SolidBrush> solids,
                 std::optional<ecs::Entity> ignored, collision::Hull hull, Vec3 start, Vec3 end)
    -> collision::Trace {
    collision::Trace best = hulls.trace(collision::HullTraceQuery{.hull = hull, .start = start, .end = end});
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
        if (hit.isAllSolid || hit.startsSolid || hit.fraction < best.fraction) {
            const bool startedSolid = best.startsSolid;
            best = hit;
            best.startsSolid = best.startsSolid || startedSolid;
        } else if (hit.startsSolid) {
            best.startsSolid = true;
        }
    }
    return best;
}

auto traceSolids(ecs::World& world, const collision::BrushHulls& hulls, collision::Hull hull, Vec3 start,
                 Vec3 end) -> collision::Trace {
    return serverTrace(hulls, solidBrushesOf(world), std::nullopt, hull, start, end);
}

} // namespace nx::game
