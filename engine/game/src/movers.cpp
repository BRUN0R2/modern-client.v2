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

/// The boxes solid in a world to what pushers push: gathered once in a tick, as a pusher first runs into a
/// body, and kept where the pushes leave their bodies, however many bodies the world holds.
class PushedBoxes {
public:
    explicit PushedBoxes(ecs::World& pushedIn) : world{&pushedIn} {}

    [[nodiscard]] auto all() -> const SolidBoxes& {
        if (!isGathered) {
            boxes.gather(*world);
            isGathered = true;
        }
        return boxes;
    }

    /// The box of `entity`, if it has one, goes where its body is now.
    void follow(ecs::Entity entity) {
        const BoxSolidity* const solidity = world->get<BoxSolidity>(entity);
        if (isGathered && solidity != nullptr) {
            std::ignore = boxes.place(entity, world->get<Transform>(entity)->origin, solidity->box);
        }
    }

private:
    ecs::World* world;
    SolidBoxes boxes;
    bool isGathered = false;
};

/// Whether the box of `body`, of `entity`, at `origin` is in solid in `world` but itself
/// (SV_TestEntityPosition).
[[nodiscard]] auto isStuck(ecs::World& world, const collision::BrushHulls& hulls, PushedBoxes& boxes,
                           ecs::Entity entity, const Body& body, Vec3 origin) -> bool {
    ServerTrace stay = hullTrace(body.hull, origin, origin);
    stay.ignored = entity;
    return traceSolidsHit(world, hulls, solidBrushesOf(world, hulls), boxes.all(), stay).trace.startsSolid;
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

/// The bodies of `world` that pushers push that stand on `model` or overlap `box`, in the world's order, and
/// where they are.
[[nodiscard]] auto bodiesAbout(ecs::World& world, collision::ModelIndex model, const Aabb& box)
    -> std::vector<Moved> {
    std::vector<Moved> bodies;
    ecs::Query<ecs::Entity, const Body&, const Transform&>{world}.forEach(
        [&bodies, model, &box](ecs::Entity entity, const Body& body, const Transform& transform) {
            if (body.isPushed && (body.groundModel == model || overlaps(body, transform.origin, box))) {
                bodies.push_back({.entity = entity, .from = transform.origin});
            }
        });
    return bodies;
}

/// Pushes `body` by `push` through all else but itself (SV_PushEntity), the pusher not solid meanwhile
/// (SOLID_NOT, as SV_PushMove makes it); returns whether it is still in solid after, the pusher solid
/// again.
[[nodiscard]] auto pushBody(ecs::World& world, const collision::BrushHulls& hulls, PushedBoxes& boxes,
                            ecs::Entity pusher, ecs::Entity bodyEntity, Vec3 push) -> bool {
    const Body& body = *world.get<Body>(bodyEntity);
    Vec3& origin = world.get<Transform>(bodyEntity)->origin;
    ServerTrace way = hullTrace(body.hull, origin, origin + push);
    way.ignored = bodyEntity;
    Solid& pusherSolid = world.get<BrushSolidity>(pusher)->solid;
    const Solid solid = std::exchange(pusherSolid, Solid::Not);
    const collision::Trace pushed =
        traceSolidsHit(world, hulls, solidBrushesOf(world, hulls), boxes.all(), way).trace;
    pusherSolid = solid;
    if (pushed.fraction != 0.0F) {
        origin = pushed.end;
        boxes.follow(bodyEntity);
    }
    return isStuck(world, hulls, boxes, bodyEntity, body, origin);
}

/// Puts back everything a blocked pusher moved, its clock too, and blocks it.
void giveWay(ecs::World& world, const collision::BrushHulls& hulls, PushedBoxes& boxes, ecs::Entity entity,
             std::span<const Moved> moved, const PushStep& step, const TickTime& clock) {
    world.get<Pusher>(entity)->localTime = step.from;
    for (const Moved& back : moved) {
        world.get<Transform>(back.entity)->origin = back.from;
        boxes.follow(back.entity);
    }
    if (world.has<Door>(entity)) {
        blockDoor(world, hulls, entity, clock);
    }
}

/// The pusher moves by its velocity over `step` (SV_PushMove), moving the bodies it runs into, or
/// that stand on it, along with it; when one cannot go, everything goes back and it gives way.
void pushMove(ecs::World& world, const collision::BrushHulls& hulls, PushedBoxes& boxes, ecs::Entity entity,
              const PushStep& step, const TickTime& clock) {
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
    for (const Moved& about : bodiesAbout(world, model, swept)) {
        const Body& body = *world.get<Body>(about.entity);
        if (body.groundModel != model && !isStuck(world, hulls, boxes, about.entity, body, about.from)) {
            continue; // not in its way
        }
        moved.push_back(about);
        if (pushBody(world, hulls, boxes, entity, about.entity, move)) {
            world.get<Transform>(entity)->origin = pushedFrom;
            giveWay(world, hulls, boxes, entity, moved, step, clock);
            return;
        }
    }
}

/// The pusher turns by its angular velocity over `step` (SV_PushRotate), carrying the bodies it runs
/// into, or that stand on it, around its origin; when one cannot go, everything goes back and it gives
/// way. Returns whether it turned.
[[nodiscard]] auto pushRotate(ecs::World& world, const collision::BrushHulls& hulls, PushedBoxes& boxes,
                              ecs::Entity entity, const PushStep& step, const TickTime& clock) -> bool {
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
    for (const Moved& about : bodiesAbout(world, model, linked)) {
        const Body& body = *world.get<Body>(about.entity);
        if (body.groundModel != model && !isStuck(world, hulls, boxes, about.entity, body, about.from)) {
            continue; // not in its way
        }
        moved.push_back(about);
        // Where the body's place about the pusher's origin goes as the pusher turns.
        const Vec3 start = about.from - transform.origin;
        const Vec3 end = outOfAxes(after, intoAxes(before, start));
        if (pushBody(world, hulls, boxes, entity, about.entity, end - start)) {
            world.get<Transform>(entity)->angles = turnedFrom;
            giveWay(world, hulls, boxes, entity, moved, step, clock);
            return false;
        }
    }
    return true;
}

/// The original's SV_Physics_Pusher for one pusher, a tick of `clock`.
void runPusher(ecs::World& world, const collision::BrushHulls& hulls, PushedBoxes& boxes, ecs::Entity entity,
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
                if (pushRotate(world, hulls, boxes, entity, step, clock)) {
                    // Both: the move runs on the clock the turn started from, which keeps the later.
                    Pusher& turned = *world.get<Pusher>(entity);
                    const GameTime afterTurn = turned.localTime;
                    turned.localTime = startTime;
                    pushMove(world, hulls, boxes, entity, step, clock);
                    turned.localTime = std::max(turned.localTime, afterTurn);
                }
            } else {
                // Whether it turned matters only to a move that would follow the turn.
                std::ignore = pushRotate(world, hulls, boxes, entity, step, clock);
            }
        } else {
            pushMove(world, hulls, boxes, entity, step, clock);
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
    PushedBoxes boxes{world};
    for (const ecs::Entity entity : pushers) {
        if (world.isAlive(entity)) { // what another's targets removed is gone
            runPusher(world, hulls, boxes, entity, clock);
        }
    }
}

} // namespace nx::game
