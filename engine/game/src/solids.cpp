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

/// The way's box of `trace` from its start to `end` (SV_MoveBounds): the box traced at both, a unit
/// more each way.
[[nodiscard]] auto wayOf(const ServerTrace& trace, Vec3 end) noexcept -> Aabb {
    const Vec3 margin{.x = kLinkGrowth, .y = kLinkGrowth, .z = kLinkGrowth};
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
[[nodiscard]] auto boxTrace(ecs::World& world, const ServerTrace& trace, const SolidBoxEntity& solid,
                            Vec3 start, Vec3 end) -> collision::Trace {
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

void SolidBoxes::clear() noexcept {
    std::ranges::fill(placeOfEntity, 0);
    boxes.clear();
    nextInCell.clear();
    if (!cells.empty()) {
        std::ranges::fill(cells, kNoPlace);
    }
    widest = 0.0F;
}

void SolidBoxes::add(const SolidBoxEntity& solid) {
    const auto entity = std::size_t{std::to_underlying(solid.entity.index())};
    if (entity >= placeOfEntity.size()) {
        placeOfEntity.resize(std::max(entity + 1, placeOfEntity.size() * 2));
    }
    const auto place = narrow<std::uint32_t>(boxes.size());
    boxes.push_back(solid);
    placeOfEntity[entity] = place + 1;
    nextInCell.push_back(kNoPlace);
    widest = std::max({widest, std::abs(solid.box.min.x), std::abs(solid.box.min.y),
                       std::abs(solid.box.max.x), std::abs(solid.box.max.y)});
    if (!cells.empty()) {
        link(place, cellOf(solid));
    } else if (boxes.size() >= kLeastIndexed) {
        buildCells();
    }
}

void SolidBoxes::gather(ecs::World& world, jobs::JobSystem* jobs) {
    // The rows of a chunk of them, and where they go among all of them.
    struct Part {
        std::span<const ecs::Entity> entities;
        std::span<const Transform> transforms;
        std::span<const BoxSolidity> solidities;
        std::span<const ClipMethod> clips;
        std::size_t first = 0;
    };
    std::vector<Part> parts;
    std::size_t count = 0;
    ecs::Query<ecs::Entity, const Transform&, const BoxSolidity&, ecs::Optional<const ClipMethod>>{world}
        .forEachChunk(
            [&parts, &count](std::span<const ecs::Entity> entities, std::span<const Transform> transforms,
                             std::span<const BoxSolidity> solidities, std::span<const ClipMethod> clips) {
                parts.push_back({.entities = entities,
                                 .transforms = transforms,
                                 .solidities = solidities,
                                 .clips = clips,
                                 .first = count});
                count += entities.size();
            });
    std::ranges::fill(placeOfEntity, 0);
    placeOfEntity.resize(std::max(placeOfEntity.size(), world.entitySlotCount()));
    boxes.resize(count);
    nextInCell.resize(count);
    const bool isIndexed = !cells.empty() || count >= kLeastIndexed;
    cellOfBox.resize(isIndexed ? count : 0);
    constexpr std::size_t kSmallestGatheredBatch = 8; // chunks
    std::vector<float> widestOfBatch(
        jobs != nullptr ? jobs->batchCountOf(parts.size(), kSmallestGatheredBatch) : 1);
    const auto gatherBatch = [&](std::size_t batch, std::size_t first, std::size_t last) {
        float widestHere = 0.0F;
        for (const Part& part : std::span{parts}.subspan(first, last - first)) {
            for (std::size_t row = 0; row < part.entities.size(); ++row) {
                const Aabb& box = part.solidities[row].box;
                const std::size_t place = part.first + row;
                boxes[place] = {.entity = part.entities[row],
                                .origin = part.transforms[row].origin,
                                .box = box,
                                .clip = part.clips.empty() ? ClipMethod{} : part.clips[row]};
                placeOfEntity[std::to_underlying(part.entities[row].index())] =
                    narrow<std::uint32_t>(place + 1);
                if (isIndexed) {
                    cellOfBox[place] = narrow<std::uint32_t>(cellOf(boxes[place]));
                }
                widestHere = std::max({widestHere, std::abs(box.min.x), std::abs(box.min.y),
                                       std::abs(box.max.x), std::abs(box.max.y)});
            }
        }
        widestOfBatch[batch] = widestHere;
    };
    if (jobs != nullptr) {
        jobs->forBatches({.label = "solids.gather", .priority = jobs::Priority::Critical}, parts.size(),
                         kSmallestGatheredBatch, gatherBatch);
    } else {
        gatherBatch(0, 0, parts.size());
    }
    widest = std::ranges::max(widestOfBatch);
    if (isIndexed) {
        cells.assign(kCellsAcross * kCellsAcross, kNoPlace);
        const auto boxCount = narrow<std::uint32_t>(count);
        for (std::uint32_t place = 0; place < boxCount; ++place) {
            link(place, cellOfBox[place]);
        }
    }
}

auto SolidBoxes::place(ecs::Entity entity, Vec3 origin, const Aabb& box) -> bool {
    const auto index = std::size_t{std::to_underlying(entity.index())};
    if (index >= placeOfEntity.size() || placeOfEntity[index] == 0) {
        return false;
    }
    const std::uint32_t place = placeOfEntity[index] - 1;
    SolidBoxEntity& solid = boxes[place];
    const std::size_t from = cells.empty() ? 0 : cellOf(solid);
    solid.origin = origin;
    solid.box = box;
    widest = std::max(
        {widest, std::abs(box.min.x), std::abs(box.min.y), std::abs(box.max.x), std::abs(box.max.y)});
    if (!cells.empty()) {
        const std::size_t to = cellOf(solid);
        if (to != from) {
            unlink(place, from);
            link(place, to);
        }
    }
    return true;
}

void SolidBoxes::forget(ecs::Entity entity) {
    const auto index = std::size_t{std::to_underlying(entity.index())};
    if (index >= placeOfEntity.size() || placeOfEntity[index] == 0) {
        return;
    }
    const std::uint32_t place = placeOfEntity[index] - 1;
    const auto last = narrow<std::uint32_t>(boxes.size() - 1);
    if (!cells.empty()) {
        unlink(place, cellOf(boxes[place]));
        if (place != last) {
            unlink(last, cellOf(boxes[last]));
        }
    }
    placeOfEntity[index] = 0;
    if (place != last) {
        boxes[place] = boxes[last];
        placeOfEntity[std::to_underlying(boxes[place].entity.index())] = place + 1;
        if (!cells.empty()) {
            link(place, cellOf(boxes[place]));
        }
    }
    boxes.pop_back();
    nextInCell.pop_back();
}

auto SolidBoxes::indexedSpanOf(const Aabb& area) const noexcept -> std::optional<CellSpan> {
    if (cells.empty()) {
        return std::nullopt;
    }
    constexpr float kMiddleOfTheWorld = static_cast<float>(kCellsAcross) / 2.0F;
    const auto cellAlong = [](float at) {
        const float cell = (at / kCellSize) + kMiddleOfTheWorld;
        return cell > 0.0F ? static_cast<std::size_t>(std::min(cell, static_cast<float>(kCellsAcross - 1)))
                           : 0;
    };
    const float reach = widest + kCellSize;
    const CellSpan span{.firstColumn = cellAlong(area.min.x - reach),
                        .lastColumn = cellAlong(area.max.x + reach),
                        .firstRow = cellAlong(area.min.y - reach),
                        .lastRow = cellAlong(area.max.y + reach)};
    const std::size_t covered = (span.lastColumn - span.firstColumn + 1) * (span.lastRow - span.firstRow + 1);
    return covered < boxes.size() ? std::optional{span} : std::nullopt;
}

auto SolidBoxes::cellOf(const SolidBoxEntity& solid) noexcept -> std::size_t {
    constexpr float kMiddleOfTheWorld = static_cast<float>(kCellsAcross) / 2.0F;
    const auto cellAlong = [](float at) {
        const float cell = (at / kCellSize) + kMiddleOfTheWorld;
        return cell > 0.0F ? static_cast<std::size_t>(std::min(cell, static_cast<float>(kCellsAcross - 1)))
                           : 0;
    };
    return (cellAlong(solid.origin.y) * kCellsAcross) + cellAlong(solid.origin.x);
}

void SolidBoxes::link(std::uint32_t place, std::size_t cell) noexcept {
    nextInCell[place] = cells[cell];
    cells[cell] = place;
}

void SolidBoxes::unlink(std::uint32_t place, std::size_t cell) noexcept {
    std::uint32_t* at = &cells[cell];
    while (*at != place) {
        at = &nextInCell[*at];
    }
    *at = nextInCell[place];
}

void SolidBoxes::buildCells() {
    cells.assign(kCellsAcross * kCellsAcross, kNoPlace);
    const auto count = narrow<std::uint32_t>(boxes.size());
    for (std::uint32_t place = 0; place < count; ++place) {
        link(place, cellOf(boxes[place]));
    }
}

auto solidBrushesOf(ecs::World& world, const collision::BrushHulls& hulls) -> std::vector<SolidBrushEntity> {
    std::vector<SolidBrushEntity> solids;
    ecs::Query<ecs::Entity, const BrushModel&, const Transform&, const BrushSolidity&>{world}.forEach(
        [&solids, &hulls](ecs::Entity entity, const BrushModel& model, const Transform& transform,
                          const BrushSolidity& solidity) {
            if (solidity.solid == Solid::Bsp) {
                solids.push_back(
                    {.entity = entity,
                     .model = model.index,
                     .origin = transform.origin,
                     .angles = transform.angles,
                     .linked = linkedBoxOf(hulls.boundsOf(model.index), transform.origin, transform.angles)});
            }
        });
    return solids;
}

auto solidsOf(ecs::World& world, const collision::BrushHulls& hulls) -> Solids {
    Solids solids{.brushes = solidBrushesOf(world, hulls)};
    solids.boxes.gather(world);
    return solids;
}

auto traceSolidsHit(ecs::World& world, const collision::BrushHulls& hulls, const ServerTrace& trace)
    -> SolidHit {
    return traceSolidsHit(world, hulls, solidsOf(world, hulls), trace);
}

auto traceSolidsHit(ecs::World& world, const collision::BrushHulls& hulls, const Solids& solids,
                    const ServerTrace& trace) -> SolidHit {
    return traceSolidsHit(world, hulls, solids.brushes, solids.boxes, trace);
}

auto traceSolidsHit(ecs::World& world, const collision::BrushHulls& hulls,
                    std::span<const SolidBrushEntity> brushes, const SolidBoxes& boxes,
                    const ServerTrace& trace) -> SolidHit {
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
    for (const SolidBrushEntity& solid : brushes) {
        if (kept.isAllSolid) {
            break;
        }
        if (isLeftOut(solid.entity) || !way.intersects(solid.linked)) {
            continue;
        }
        if (collision::keepFirstStop(
                kept, brushTrace(hulls, trace, solid.model, solid.origin, solid.angles, trace.start, end))) {
            found.entity = solid.entity;
        }
    }
    const Vec3 margin{.x = kLinkGrowth, .y = kLinkGrowth, .z = kLinkGrowth};
    if (trace.type != TraceType::NoMonsters) {
        boxes.forEachAbout(way, [&](const SolidBoxEntity& solid) {
            if (kept.isAllSolid || isLeftOut(solid.entity) ||
                !way.intersects(solid.box.translated(solid.origin).expanded(margin))) {
                return;
            }
            if (collision::keepFirstStop(kept, boxTrace(world, trace, solid, trace.start, end))) {
                found.entity = solid.entity;
            }
        });
    }
    kept.fraction *= worldFraction;
    return found;
}

auto lineReaches(const collision::BrushHulls& hulls, std::span<const SolidBrushEntity> brushes, Vec3 start,
                 Vec3 end) noexcept -> bool {
    if (hulls.trace(collision::HullTraceQuery{.start = start, .end = end}).fraction < 1.0F) {
        return false;
    }
    const Aabb way = wayOf(ServerTrace{.start = start}, end);
    for (const SolidBrushEntity& solid : brushes) {
        if (!way.intersects(solid.linked)) {
            continue;
        }
        const collision::Trace met = hulls.trace(collision::HullTraceQuery{.model = solid.model,
                                                                           .offset = solid.origin,
                                                                           .angles = solid.angles,
                                                                           .start = start,
                                                                           .end = end});
        if (met.fraction < 1.0F) {
            return false;
        }
        if (met.isAllSolid) {
            return true; // no later one is looked at: all in solid, the trace goes the whole way
        }
    }
    return true;
}

auto traceSolids(ecs::World& world, const collision::BrushHulls& hulls, collision::Hull hull, Vec3 start,
                 Vec3 end) -> collision::Trace {
    return traceSolidsHit(world, hulls, hullTrace(hull, start, end)).trace;
}

} // namespace nx::game
