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

/// The engine grows every box it links by a unit on every side, and a brush model's bounds by
/// another when it loads them.
constexpr float kLinkGrowth = 1.0F;
constexpr float kLoadedBoundsGrowth = 1.0F;
/// Something blocking a door again this soon after the last time is not felt (CBaseDoor::Blocked).
constexpr Seconds kBlockedInterval{0.25F};

/// A solid brush entity, where it is now.
struct SolidBrush {
    ecs::Entity entity{};
    std::uint32_t model = 0;
    Vec3 origin{};
};

/// The solid brush entities of `world`, where they are now.
[[nodiscard]] auto solidBrushesOf(ecs::World& world) -> std::vector<SolidBrush> {
    std::vector<SolidBrush> solids;
    ecs::Query<ecs::Entity, const BrushModel&, const Transform&, const BrushSolidity&>{world}.forEach(
        [&solids](ecs::Entity entity, const BrushModel& model, const Transform& transform,
                  const BrushSolidity& solidity) {
            if (solidity.solid == Solid::Bsp) {
                solids.push_back({.entity = entity, .model = model.index, .origin = transform.origin});
            }
        });
    return solids;
}

/// The server's way for `hull` from `start` to `end` through the world and the solid brush entities
/// of `solids` but `ignored` (SV_Move of a box among brush models): the world's trace, then each
/// entity's that stops it sooner or starts in solid; once all-solid, no other is looked at.
[[nodiscard]] auto serverTrace(const collision::BrushHulls& hulls, std::span<const SolidBrush> solids,
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
        const collision::Trace hit = hulls.trace(collision::HullTraceQuery{
            .model = solid.model, .hull = hull, .offset = solid.origin, .start = start, .end = end});
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

/// Whether the box of `body` at `origin` is in solid (SV_TestEntityPosition).
[[nodiscard]] auto isStuck(const collision::BrushHulls& hulls, std::span<const SolidBrush> solids,
                           const Body& body, Vec3 origin) -> bool {
    return serverTrace(hulls, solids, std::nullopt, body.hull, origin, origin).startsSolid;
}

/// A door reached where it was going (CBaseDoor::DoorHitTop and DoorHitBottom).
void arrive(ecs::World& world, ecs::Entity entity) {
    SlidingDoor& door = *world.get<SlidingDoor>(entity);
    Pusher& pusher = *world.get<Pusher>(entity);
    if (door.state == DoorState::Opening) {
        door.state = DoorState::Open;
        if (door.staysOpen) {
            door.isTouchable = !door.isUseOnly;
        } else if (door.wait >= Seconds{}) {
            pusher.nextThink = pusher.localTime + door.wait;
        }
        return;
    }
    door.state = DoorState::Closed;
    door.isTouchable = !door.isUseOnly;
}

/// Where the move under way ends (CBaseToggle::LinearMoveDone).
void finishMove(ecs::World& world, ecs::Entity entity) {
    Pusher& pusher = *world.get<Pusher>(entity);
    world.get<Transform>(entity)->origin = pusher.destination;
    pusher.velocity = {};
    pusher.nextThink = std::nullopt;
    arrive(world, entity);
}

/// Moves a pusher towards `destination` at `speed` (CBaseToggle::LinearMove): arrives at once when
/// it is there already, else moves at the velocity that brings it there by its clock.
void moveTo(ecs::World& world, ecs::Entity entity, Vec3 destination, UnitsPerSecond speed) {
    Pusher& pusher = *world.get<Pusher>(entity);
    const Vec3 origin = world.get<Transform>(entity)->origin;
    pusher.destination = destination;
    if (destination == origin) {
        finishMove(world, entity);
        return;
    }
    const Vec3 way = destination - origin;
    const double travelTime = std::sqrt(double{dot(way, way)}) / double{speed.value()};
    pusher.nextThink = Seconds{static_cast<float>(double{pusher.localTime.count()} + travelTime)};
    pusher.velocity = way * static_cast<float>(1.0 / travelTime);
}

/// A door starts opening, or closing (CBaseDoor::DoorGoUp and DoorGoDown).
void open(ecs::World& world, ecs::Entity entity) {
    SlidingDoor& door = *world.get<SlidingDoor>(entity);
    door.state = DoorState::Opening;
    moveTo(world, entity, door.open, door.speed);
}

void close(ecs::World& world, ecs::Entity entity) {
    SlidingDoor& door = *world.get<SlidingDoor>(entity);
    door.state = DoorState::Closing;
    moveTo(world, entity, door.closed, door.speed);
}

/// Something the door could not push stopped it at `time` (CBaseDoor::Blocked): it goes back the
/// way it came, unless it stays open for good or was stopped just before.
void block(ecs::World& world, ecs::Entity entity, Seconds time) {
    SlidingDoor& door = *world.get<SlidingDoor>(entity);
    if (time - door.lastBlocked < kBlockedInterval) {
        return;
    }
    door.lastBlocked = time;
    if (door.wait >= Seconds{}) {
        if (door.state == DoorState::Closing) {
            open(world, entity);
        } else {
            close(world, entity);
        }
    }
}

/// The pusher moves by `move` over `moveTime` (SV_PushMove), moving the bodies it runs into, or that
/// stand on it, along with it; when one cannot go, everything goes back and the door is blocked.
void push(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity, Seconds moveTime,
          Seconds time) {
    Pusher& pusher = *world.get<Pusher>(entity);
    if (pusher.velocity == Vec3{}) {
        pusher.localTime += moveTime;
        return;
    }
    const Vec3 move = pusher.velocity * moveTime.count();
    const std::uint32_t model = world.get<BrushModel>(entity)->index;
    Transform& transform = *world.get<Transform>(entity);
    const Aabb bounds = hulls.boundsOf(model);
    const Vec3 growth{kLinkGrowth + kLoadedBoundsGrowth, kLinkGrowth + kLoadedBoundsGrowth,
                      kLinkGrowth + kLoadedBoundsGrowth};
    const Vec3 sweptMin = transform.origin + bounds.min - growth + move;
    const Vec3 sweptMax = transform.origin + bounds.max + growth + move;
    const Vec3 pushedFrom = transform.origin;
    transform.origin = transform.origin + move;
    pusher.localTime += moveTime;
    if (world.get<BrushSolidity>(entity)->solid == Solid::Not) {
        return;
    }

    struct Moved {
        ecs::Entity entity{};
        Vec3 from{};
    };
    std::vector<Moved> moved;
    std::vector<ecs::Entity> bodies;
    ecs::Query<ecs::Entity, const Body&>{world}.forEach(
        [&bodies](ecs::Entity body, const Body&) { bodies.push_back(body); });
    const Vec3 linkGrowth{kLinkGrowth, kLinkGrowth, kLinkGrowth};
    for (const ecs::Entity bodyEntity : bodies) {
        const Body& body = *world.get<Body>(bodyEntity);
        Vec3& origin = world.get<Transform>(bodyEntity)->origin;
        if (body.groundModel != model) {
            const Vec3 boxMin = origin + body.mins - linkGrowth;
            const Vec3 boxMax = origin + body.maxs + linkGrowth;
            if (boxMin.x >= sweptMax.x || boxMin.y >= sweptMax.y || boxMin.z >= sweptMax.z ||
                boxMax.x <= sweptMin.x || boxMax.y <= sweptMin.y || boxMax.z <= sweptMin.z ||
                !isStuck(hulls, solidBrushesOf(world), body, origin)) {
                continue; // not in its way
            }
        }
        const Vec3 bodyFrom = origin;
        moved.push_back({.entity = bodyEntity, .from = bodyFrom});
        // The body goes the pusher's way, through all else (SV_PushEntity).
        const collision::Trace pushed =
            serverTrace(hulls, solidBrushesOf(world), entity, body.hull, origin, origin + move);
        if (pushed.fraction != 0.0F) {
            origin = pushed.end;
        }
        if (isStuck(hulls, solidBrushesOf(world), body, origin)) {
            // It cannot go: everything goes back, and the pusher is blocked.
            origin = bodyFrom;
            transform.origin = pushedFrom;
            pusher.localTime -= moveTime;
            for (const Moved& back : moved) {
                world.get<Transform>(back.entity)->origin = back.from;
            }
            if (world.has<SlidingDoor>(entity)) {
                block(world, entity, time);
            }
            return;
        }
    }
}

/// The original's SV_Physics_Pusher for one pusher.
void runPusher(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity, Seconds frameTime,
               Seconds time) {
    Pusher& pusher = *world.get<Pusher>(entity);
    const Seconds startTime = pusher.localTime;
    const std::optional<Seconds> thinkTime = pusher.nextThink;
    // Parity: a pusher with nothing to do by its clock does not move, nor does its clock.
    const Seconds moveTime = !thinkTime ? Seconds{}
                             : *thinkTime < startTime + frameTime
                                 ? std::max(*thinkTime - pusher.localTime, Seconds{})
                                 : frameTime;
    if (moveTime != Seconds{}) {
        push(world, hulls, entity, moveTime, time);
    }
    Pusher& after = *world.get<Pusher>(entity);
    if (thinkTime && *thinkTime > startTime && *thinkTime <= after.localTime) {
        after.nextThink = std::nullopt;
        if (const SlidingDoor* door = world.get<SlidingDoor>(entity)) {
            if (door->state == DoorState::Open) {
                close(world, entity);
            } else {
                finishMove(world, entity);
            }
        }
    }
}

} // namespace

void touchBrushEntities(ecs::World& world, std::span<const std::uint32_t> models) {
    std::vector<ecs::Entity> doors;
    for (const std::uint32_t model : models) {
        ecs::Query<ecs::Entity, const BrushModel&, const SlidingDoor&>{world}.forEach(
            [&doors, model](ecs::Entity entity, const BrushModel& brush, const SlidingDoor&) {
                if (brush.index == model) {
                    doors.push_back(entity);
                }
            });
    }
    for (const ecs::Entity entity : doors) {
        SlidingDoor& door = *world.get<SlidingDoor>(entity);
        if (!door.isTouchable || door.isNamed) {
            continue; // moving, used only, or opened by others
        }
        door.isTouchable = false;
        if (door.staysOpen && door.state == DoorState::Open) {
            close(world, entity);
        } else {
            open(world, entity);
        }
    }
}

void runPushers(ecs::World& world, const collision::BrushHulls& hulls, Seconds frameTime, Seconds time) {
    std::vector<ecs::Entity> pushers;
    ecs::Query<ecs::Entity, const Pusher&>{world}.forEach(
        [&pushers](ecs::Entity entity, const Pusher&) { pushers.push_back(entity); });
    for (const ecs::Entity entity : pushers) {
        runPusher(world, hulls, entity, frameTime, time);
    }
}

} // namespace nx::game
