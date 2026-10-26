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

/// The engine keeps a pusher's angles within ten turns.
constexpr float kAngleWrap = 3600.0F;

/// Whether the box of `body` at `origin` is in solid in `world` (SV_TestEntityPosition).
[[nodiscard]] auto isStuck(ecs::World& world, const collision::BrushHulls& hulls, const Body& body,
                           Vec3 origin) -> bool {
    return traceSolidsHit(world, hulls, hullTrace(body.hull, origin, origin)).trace.startsSolid;
}

/// Whether a body's linked box, at `origin`, overlaps `box`, more than touching it (SV_PushMove).
[[nodiscard]] auto overlaps(const Body& body, Vec3 origin, const Aabb& box) -> bool {
    const Aabb linked = linkedBoxOf(body, origin);
    return linked.min.x < box.max.x && linked.min.y < box.max.y && linked.min.z < box.max.z &&
           linked.max.x > box.min.x && linked.max.y > box.min.y && linked.max.z > box.min.z;
}

/// A pusher's step of a tick: how long it moves, and its clock from and to, exact (SV_Physics_Pusher's
/// movetime and ltime).
struct PushStep {
    Seconds time{};
    GameTime from{};
    GameTime until{};
};

/// A body the pusher moves, and where it was.
struct Moved {
    ecs::Entity entity{};
    Vec3 from{};
};

/// The bodies of `world` that pushers push.
[[nodiscard]] auto bodiesOf(ecs::World& world) -> std::vector<ecs::Entity> {
    std::vector<ecs::Entity> bodies;
    ecs::Query<ecs::Entity, const Body&>{world}.forEach([&bodies](ecs::Entity entity, const Body& body) {
        if (body.isPushed) {
            bodies.push_back(entity);
        }
    });
    return bodies;
}

/// Pushes `body` by `push` through all else (SV_PushEntity, the pusher not solid meanwhile); returns
/// whether it is still in solid after, the pusher solid again.
[[nodiscard]] auto pushBody(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity pusher,
                            ecs::Entity bodyEntity, Vec3 push) -> bool {
    const Body& body = *world.get<Body>(bodyEntity);
    Vec3& origin = world.get<Transform>(bodyEntity)->origin;
    ServerTrace way = hullTrace(body.hull, origin, origin + push);
    way.ignored = pusher;
    const collision::Trace pushed = traceSolidsHit(world, hulls, way).trace;
    if (pushed.fraction != 0.0F) {
        origin = pushed.end;
    }
    return isStuck(world, hulls, body, origin);
}

/// Puts back everything a blocked pusher moved, its clock too, and blocks it.
void giveWay(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
             std::span<const Moved> moved, const PushStep& step, const TickTime& clock) {
    world.get<Pusher>(entity)->localTime = step.from;
    for (const Moved& back : moved) {
        world.get<Transform>(back.entity)->origin = back.from;
    }
    if (world.has<Door>(entity)) {
        blockDoor(world, hulls, entity, clock);
    }
}

/// The pusher moves by its velocity over `step` (SV_PushMove), moving the bodies it runs into, or
/// that stand on it, along with it; when one cannot go, everything goes back and it gives way.
void pushMove(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity, const PushStep& step,
              const TickTime& clock) {
    Pusher& pusher = *world.get<Pusher>(entity);
    if (pusher.velocity == Vec3{}) {
        pusher.localTime = step.until;
        return;
    }
    const Vec3 move = pusher.velocity * step.time.count();
    const collision::ModelIndex model = world.get<BrushModel>(entity)->index;
    Transform& transform = *world.get<Transform>(entity);
    const Aabb linked = linkedBoxOf(hulls.boundsOf(model), transform.origin, transform.angles);
    const Aabb swept{.min = linked.min + move, .max = linked.max + move};
    const Vec3 pushedFrom = transform.origin;
    transform.origin = transform.origin + move;
    pusher.localTime = step.until;
    if (world.get<BrushSolidity>(entity)->solid == Solid::Not) {
        return;
    }
    std::vector<Moved> moved;
    for (const ecs::Entity bodyEntity : bodiesOf(world)) {
        const Body& body = *world.get<Body>(bodyEntity);
        const Vec3 origin = world.get<Transform>(bodyEntity)->origin;
        if (body.groundModel != model &&
            (!overlaps(body, origin, swept) || !isStuck(world, hulls, body, origin))) {
            continue; // not in its way
        }
        moved.push_back({.entity = bodyEntity, .from = origin});
        if (pushBody(world, hulls, entity, bodyEntity, move)) {
            world.get<Transform>(entity)->origin = pushedFrom;
            giveWay(world, hulls, entity, moved, step, clock);
            return;
        }
    }
}

/// The pusher turns by its angular velocity over `step` (SV_PushRotate), carrying the bodies it runs
/// into, or that stand on it, around its origin; when one cannot go, everything goes back and it gives
/// way. Returns whether it turned.
[[nodiscard]] auto pushRotate(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
                              const PushStep& step, const TickTime& clock) -> bool {
    Pusher& pusher = *world.get<Pusher>(entity);
    if (pusher.angularVelocity == Vec3{}) {
        pusher.localTime = step.until;
        return true;
    }
    const Vec3 turn = pusher.angularVelocity * step.time.count();
    Transform& transform = *world.get<Transform>(entity);
    const AngleVectors before = angleVectors(transform.angles);
    const EulerAngles turnedFrom = transform.angles;
    transform.angles = eulerOf(vectorOf(transform.angles) + turn);
    const AngleVectors after = angleVectors(transform.angles);
    pusher.localTime = step.until;
    if (world.get<BrushSolidity>(entity)->solid == Solid::Not) {
        return true;
    }
    const collision::ModelIndex model = world.get<BrushModel>(entity)->index;
    const Aabb linked = linkedBoxOf(hulls.boundsOf(model), transform.origin, transform.angles);
    std::vector<Moved> moved;
    for (const ecs::Entity bodyEntity : bodiesOf(world)) {
        const Body& body = *world.get<Body>(bodyEntity);
        const Vec3 origin = world.get<Transform>(bodyEntity)->origin;
        if (body.groundModel != model &&
            (!overlaps(body, origin, linked) || !isStuck(world, hulls, body, origin))) {
            continue; // not in its way
        }
        moved.push_back({.entity = bodyEntity, .from = origin});
        // Where the body's place about the pusher's origin goes as the pusher turns.
        const Vec3 start = origin - transform.origin;
        const Vec3 end = outOfAxes(after, intoAxes(before, start));
        if (pushBody(world, hulls, entity, bodyEntity, end - start)) {
            world.get<Transform>(entity)->angles = turnedFrom;
            giveWay(world, hulls, entity, moved, step, clock);
            return false;
        }
    }
    return true;
}

/// The original's SV_Physics_Pusher for one pusher, a tick of `clock`.
void runPusher(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
               const TickTime& clock) {
    const Pusher& pusher = *world.get<Pusher>(entity);
    const GameTime startTime = pusher.localTime;
    const std::optional<GameTime> thinkTime = pusher.nextThink;
    // Parity: a pusher with nothing to do by its clock does not move, nor does its clock. It moves a
    // tick, or up to when it acts within it; its clock lands there exactly, a tick being whole ticks.
    const GameTime tickEnd = startTime.ticksLater(1);
    PushStep step{.from = startTime, .until = startTime};
    if (thinkTime && *thinkTime < tickEnd) {
        step.time = std::max(thinkTime->since(startTime, clock.rate), Seconds{});
        step.until = std::max(*thinkTime, startTime);
    } else if (thinkTime) {
        step.time = clock.delta();
        step.until = tickEnd;
    }
    if (step.time != Seconds{}) {
        if (pusher.angularVelocity != Vec3{}) {
            if (pusher.velocity != Vec3{}) {
                if (pushRotate(world, hulls, entity, step, clock)) {
                    // Both: the move runs on the clock the turn started from, which keeps the later.
                    Pusher& turned = *world.get<Pusher>(entity);
                    const GameTime afterTurn = turned.localTime;
                    turned.localTime = startTime;
                    pushMove(world, hulls, entity, step, clock);
                    turned.localTime = std::max(turned.localTime, afterTurn);
                }
            } else {
                // Whether it turned matters only to a move that would follow the turn.
                std::ignore = pushRotate(world, hulls, entity, step, clock);
            }
        } else {
            pushMove(world, hulls, entity, step, clock);
        }
    }
    Transform& transform = *world.get<Transform>(entity);
    Vec3 angles = vectorOf(transform.angles);
    for (float* const angle : {&angles.x, &angles.y, &angles.z}) {
        if (*angle < -kAngleWrap || *angle > kAngleWrap) {
            *angle = std::fmod(*angle, kAngleWrap);
        }
    }
    transform.angles = eulerOf(angles);
    Pusher& after = *world.get<Pusher>(entity);
    if (thinkTime && *thinkTime > startTime && *thinkTime <= after.localTime) {
        after.nextThink = std::nullopt;
        thinkToggle(world, hulls, entity);
    }
}

} // namespace

void touchBrushEntities(ecs::World& world, const collision::BrushHulls& hulls,
                        std::span<const collision::ModelIndex> models, ecs::Entity toucher) {
    std::vector<ecs::Entity> touched;
    for (const collision::ModelIndex model : models) {
        ecs::Query<ecs::Entity, const BrushModel&>{world}.forEach(
            [&touched, model](ecs::Entity entity, const BrushModel& brush) {
                if (brush.index == model) {
                    touched.push_back(entity);
                }
            });
    }
    for (const ecs::Entity entity : touched) {
        if (!world.isAlive(entity)) {
            continue; // what another's targets removed is gone
        }
        if (world.has<Door>(entity)) {
            touchDoor(world, hulls, entity, toucher);
        } else if (world.has<Button>(entity)) {
            touchButton(world, hulls, entity, toucher);
        }
    }
}

void runPushers(ecs::World& world, const collision::BrushHulls& hulls) {
    const TickTime clock = tickTimeOf(world);
    std::vector<ecs::Entity> pushers;
    ecs::Query<ecs::Entity, const Pusher&>{world}.forEach(
        [&pushers](ecs::Entity entity, const Pusher&) { pushers.push_back(entity); });
    for (const ecs::Entity entity : pushers) {
        if (world.isAlive(entity)) { // what another's targets removed is gone
            runPusher(world, hulls, entity, clock);
        }
    }
}

} // namespace nx::game
