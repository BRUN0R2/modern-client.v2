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

/// How much an entity's box grows each way as the engine links it (SV_LinkEdict), and the way's box
/// as it bounds the move (SV_MoveBounds).
constexpr float kLinkMargin = 1.0F;

/// A solid brush entity, where it is now.
struct SolidBrush {
    ecs::Entity entity{};
    collision::ModelIndex model{};
    Vec3 origin{};
    EulerAngles angles{};
};

/// An entity solid as a box, where it is now, and how a trace meets it.
struct SolidBox {
    ecs::Entity entity{};
    Vec3 origin{};
    Aabb box{};
    ClipMethod clip{};
};

/// The solid brush entities of `world`, where they are now.
[[nodiscard]] auto solidBrushesOf(ecs::World& world) -> std::vector<SolidBrush> {
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

/// The entities of `world` solid as boxes, where they are now.
[[nodiscard]] auto solidBoxesOf(ecs::World& world) -> std::vector<SolidBox> {
    std::vector<SolidBox> solids;
    ecs::Query<ecs::Entity, const Transform&, const BoxSolidity&, ecs::Optional<const ClipMethod>>{world}
        .forEach([&solids](ecs::Entity entity, const Transform& transform, const BoxSolidity& solidity,
                           const ClipMethod* clip) {
            solids.push_back({.entity = entity,
                              .origin = transform.origin,
                              .box = solidity.box,
                              .clip = clip != nullptr ? *clip : ClipMethod{}});
        });
    return solids;
}

/// The way's box of `trace` from its start to `end` (SV_MoveBounds): the box traced at both, a unit
/// more each way.
[[nodiscard]] auto wayOf(const ServerTrace& trace, Vec3 end) noexcept -> Aabb {
    const Vec3 margin{.x = kLinkMargin, .y = kLinkMargin, .z = kLinkMargin};
    return Aabb{.min = componentMin(trace.start, end) + trace.box.min - margin,
                .max = componentMax(trace.start, end) + trace.box.max + margin};
}

/// The trace of `trace`'s box from `start` to `end` through brush model `model` placed at `origin` and
/// turned by `angles`, through the hull the box's size chooses (SV_HullForBsp), in the box's origin.
[[nodiscard]] auto brushTrace(const collision::BrushHulls& hulls, const ServerTrace& trace,
                              collision::ModelIndex model, Vec3 origin, EulerAngles angles, Vec3 start,
                              Vec3 end) -> collision::Trace {
    const collision::BoxHull boxHull = collision::boxHullOf(trace.box);
    collision::Trace traced = hulls.trace(collision::HullTraceQuery{.model = model,
                                                                    .hull = boxHull.hull,
                                                                    .offset = origin,
                                                                    .angles = angles,
                                                                    .start = start - boxHull.offset,
                                                                    .end = end - boxHull.offset});
    traced.end = traced.end + boxHull.offset;
    return traced;
}

/// The trace of `trace`'s box from `start` to `end` through `solid` (SV_ClipMoveToEntity): as its kind
/// says, else through its box grown by the one traced (SV_HullForBox).
[[nodiscard]] auto boxTrace(ecs::World& world, const ServerTrace& trace, const SolidBox& solid, Vec3 start,
                            Vec3 end) -> collision::Trace {
    if (solid.clip.clip != nullptr) {
        return solid.clip.clip(world, solid.entity, trace.box, start, end);
    }
    return collision::traceBox(
        Aabb{.min = solid.box.min - trace.box.max, .max = solid.box.max - trace.box.min}, solid.origin, start,
        end);
}

} // namespace

auto hullTrace(collision::Hull hull, Vec3 start, Vec3 end) noexcept -> ServerTrace {
    return ServerTrace{.box = collision::hullBoxOf(hull), .start = start, .end = end};
}

auto traceSolidsHit(ecs::World& world, const collision::BrushHulls& hulls, const ServerTrace& trace)
    -> SolidHit {
    SolidHit found{.trace = brushTrace(hulls, trace, collision::kWorldModel, Vec3{}, EulerAngles{},
                                       trace.start, trace.end)};
    if (found.trace.fraction == 0.0F) {
        return found;
    }
    // The entities are met on the way to where the world stops it, then that way is the whole one's.
    const Vec3 end = found.trace.end;
    const float worldFraction = found.trace.fraction;
    collision::Trace& kept = found.trace;
    kept.fraction = 1.0F;
    const Aabb way = wayOf(trace, end);
    const auto isLeftOut = [&trace](ecs::Entity entity) {
        return trace.ignored == entity;
    };
    for (const SolidBrush& solid : solidBrushesOf(world)) {
        if (kept.isAllSolid) {
            break;
        }
        if (isLeftOut(solid.entity)) {
            continue;
        }
        if (collision::keepFirstStop(
                kept, brushTrace(hulls, trace, solid.model, solid.origin, solid.angles, trace.start, end))) {
            found.entity = solid.entity;
        }
    }
    const Vec3 margin{.x = kLinkMargin, .y = kLinkMargin, .z = kLinkMargin};
    for (const SolidBox& solid : solidBoxesOf(world)) {
        if (kept.isAllSolid) {
            break;
        }
        if (trace.type == TraceType::NoMonsters || isLeftOut(solid.entity) ||
            !way.intersects(solid.box.translated(solid.origin).expanded(margin))) {
            continue;
        }
        if (collision::keepFirstStop(kept, boxTrace(world, trace, solid, trace.start, end))) {
            found.entity = solid.entity;
        }
    }
    kept.fraction *= worldFraction;
    return found;
}

auto traceSolids(ecs::World& world, const collision::BrushHulls& hulls, collision::Hull hull, Vec3 start,
                 Vec3 end) -> collision::Trace {
    return traceSolidsHit(world, hulls, hullTrace(hull, start, end)).trace;
}

} // namespace nx::game
