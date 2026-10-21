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

/// Something blocking a door again this soon after the last time is not felt (CBaseDoor::Blocked).
constexpr Seconds kBlockedInterval{0.25F};
/// The engine keeps a pusher's angles within ten turns.
constexpr float kAngleWrap = 3600.0F;

[[nodiscard]] auto vectorOf(EulerAngles angles) -> Vec3 {
    return Vec3{.x = angles.pitch.value(), .y = angles.yaw.value(), .z = angles.roll.value()};
}

[[nodiscard]] auto eulerOf(Vec3 angles) -> EulerAngles {
    return EulerAngles{.pitch = Degrees{angles.x}, .yaw = Degrees{angles.y}, .roll = Degrees{angles.z}};
}

/// Whether the box of `body` at `origin` is in solid (SV_TestEntityPosition).
[[nodiscard]] auto isStuck(const collision::BrushHulls& hulls, std::span<const SolidBrush> solids,
                           const Body& body, Vec3 origin) -> bool {
    return serverTrace(hulls, solids, std::nullopt, body.hull, origin, origin).startsSolid;
}

/// Whether a body's linked box, at `origin`, overlaps `box`, more than touching it (SV_PushMove).
[[nodiscard]] auto overlaps(const Body& body, Vec3 origin, const Aabb& box) -> bool {
    const Aabb linked = linkedBoxOf(body, origin);
    return linked.min.x < box.max.x && linked.min.y < box.max.y && linked.min.z < box.max.z &&
           linked.max.x > box.min.x && linked.max.y > box.min.y && linked.max.z > box.min.z;
}

/// A door reached where it was going (CBaseDoor::DoorHitTop and DoorHitBottom): it waits or rests,
/// and fires its targets for what moved it.
void arrive(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity) {
    Door& door = *world.get<Door>(entity);
    Pusher& pusher = *world.get<Pusher>(entity);
    if (door.state == DoorState::Opening) {
        door.state = DoorState::Open;
        if (door.staysOpen) {
            door.isTouchable = !door.isUseOnly;
        } else if (door.wait >= Seconds{}) {
            pusher.nextThink = pusher.localTime.after(door.wait, tickTimeOf(world).rate);
        }
    } else {
        door.state = DoorState::Closed;
        door.isTouchable = !door.isUseOnly;
    }
    useTargets(world, hulls, entity, door.activator);
}

/// Where the move or the turn under way ends (CBaseToggle::LinearMoveDone and AngularMoveDone).
void finishMove(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity) {
    Pusher& pusher = *world.get<Pusher>(entity);
    Transform& transform = *world.get<Transform>(entity);
    if (std::holds_alternative<DoorTurn>(world.get<Door>(entity)->motion)) {
        transform.angles = pusher.destinationAngles;
        pusher.angularVelocity = {};
    } else {
        transform.origin = pusher.destination;
        pusher.velocity = {};
    }
    pusher.nextThink = std::nullopt;
    arrive(world, hulls, entity);
}

/// Moves a pusher towards `destination` at `speed` (CBaseToggle::LinearMove): arrives at once when
/// it is there already, else moves at the velocity that brings it there by its clock.
void moveTo(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity, Vec3 destination,
            UnitsPerSecond speed) {
    Pusher& pusher = *world.get<Pusher>(entity);
    const Vec3 origin = world.get<Transform>(entity)->origin;
    pusher.destination = destination;
    if (destination == origin) {
        finishMove(world, hulls, entity);
        return;
    }
    const Vec3 way = destination - origin;
    const double travelTime = std::sqrt(double{dot(way, way)}) / double{speed.value()};
    pusher.nextThink =
        pusher.localTime.after(Seconds{static_cast<float>(travelTime)}, tickTimeOf(world).rate);
    pusher.velocity = way * static_cast<float>(1.0 / travelTime);
}

/// Turns a pusher towards `destination` at `speed` (CBaseToggle::AngularMove), as moveTo moves it.
void turnTo(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
            EulerAngles destination, DegreesPerSecond speed) {
    Pusher& pusher = *world.get<Pusher>(entity);
    const EulerAngles angles = world.get<Transform>(entity)->angles;
    pusher.destinationAngles = destination;
    if (destination == angles) {
        finishMove(world, hulls, entity);
        return;
    }
    const Vec3 turn = vectorOf(destination) - vectorOf(angles);
    const double travelTime = std::sqrt(double{dot(turn, turn)}) / double{speed.value()};
    pusher.nextThink =
        pusher.localTime.after(Seconds{static_cast<float>(travelTime)}, tickTimeOf(world).rate);
    pusher.angularVelocity = turn / static_cast<float>(travelTime);
}

/// Which way a door that opens away turns from its activator, at `toActivator` (CBaseDoor::DoorGoUp):
/// by the side of its box the activator is on and the arm from its origin, 1 or -1.
[[nodiscard]] auto awaySign(const Aabb& box, Vec3 origin, Vec3 toActivator) -> float {
    const float armX = toActivator.x - origin.x;
    const float armY = toActivator.y - origin.y;
    // Parity: the original compares the arms' lengths cut to whole units.
    const bool isArmYLonger = std::abs(std::trunc(armY)) > std::abs(std::trunc(armX));
    const auto sign = [](bool isPositive) {
        return isPositive ? 1.0F : -1.0F;
    };
    if (box.min.x > toActivator.x) {
        if (toActivator.y < box.min.y) {
            return isArmYLonger ? sign(armY < 0.0F) : sign(armX > 0.0F);
        }
        if (toActivator.y > box.max.y) {
            return isArmYLonger ? sign(armY < 0.0F) : sign(armX < 0.0F);
        }
        return sign(armY < 0.0F);
    }
    if (toActivator.x <= box.max.x) {
        if (toActivator.y < box.min.y) {
            return sign(armX > 0.0F);
        }
        if (toActivator.y > box.max.y) {
            return sign(armX < 0.0F);
        }
        return 1.0F;
    }
    if (toActivator.y < box.min.y) {
        return isArmYLonger ? sign(armY > 0.0F) : sign(armX > 0.0F);
    }
    if (toActivator.y > box.max.y) {
        return isArmYLonger ? sign(armY > 0.0F) : sign(armX < 0.0F);
    }
    return sign(armY > 0.0F);
}

/// A door starts opening (CBaseDoor::DoorGoUp): a turning one away from what opened it, but one-way.
void open(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity) {
    Door& door = *world.get<Door>(entity);
    const bool isReversing = door.state == DoorState::Closing;
    door.state = DoorState::Opening;
    if (const auto* slide = std::get_if<DoorSlide>(&door.motion)) {
        moveTo(world, hulls, entity, slide->open, slide->speed);
        return;
    }
    const DoorTurn& turn = std::get<DoorTurn>(door.motion);
    float sign = 1.0F;
    if (turn.opensAway && world.isAlive(door.activator)) {
        const Vec3 origin = world.get<Transform>(entity)->origin;
        const Aabb bounds = hulls.boundsOf(world.get<BrushModel>(entity)->index);
        const Vec3 growth{kLoadedBoundsGrowth, kLoadedBoundsGrowth, kLoadedBoundsGrowth};
        const Aabb box{.min = bounds.min - growth + origin, .max = bounds.max + growth + origin};
        sign = awaySign(box, origin, world.get<Transform>(door.activator)->origin);
        if (isReversing) {
            sign = -sign;
        }
    }
    // Parity: the original turns all of its open angles by the sign, its starting ones included.
    turnTo(world, hulls, entity, eulerOf(vectorOf(turn.open) * sign), turn.speed);
}

/// A door starts closing (CBaseDoor::DoorGoDown).
void close(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity) {
    Door& door = *world.get<Door>(entity);
    door.state = DoorState::Closing;
    if (const auto* slide = std::get_if<DoorSlide>(&door.motion)) {
        moveTo(world, hulls, entity, slide->closed, slide->speed);
        return;
    }
    const DoorTurn& turn = std::get<DoorTurn>(door.motion);
    turnTo(world, hulls, entity, turn.closed, turn.speed);
}

/// Something the door could not push stopped it at `clock`'s time (CBaseDoor::Blocked): it goes back
/// the way it came, unless it stays open for good or was stopped just before.
void block(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity, const TickTime& clock) {
    Door& door = *world.get<Door>(entity);
    if (clock.now.since(door.lastBlocked, clock.rate) < kBlockedInterval) {
        return;
    }
    door.lastBlocked = clock.now;
    if (door.wait >= Seconds{}) {
        if (door.state == DoorState::Closing) {
            open(world, hulls, entity);
        } else {
            close(world, hulls, entity);
        }
    }
}

/// A body the pusher moves, and where it was.
struct Moved {
    ecs::Entity entity{};
    Vec3 from{};
};

/// The bodies of `world`.
[[nodiscard]] auto bodiesOf(ecs::World& world) -> std::vector<ecs::Entity> {
    std::vector<ecs::Entity> bodies;
    ecs::Query<ecs::Entity, const Body&>{world}.forEach(
        [&bodies](ecs::Entity body, const Body&) { bodies.push_back(body); });
    return bodies;
}

/// Pushes `body` by `push` through all else (SV_PushEntity, the pusher not solid meanwhile); returns
/// whether it is still in solid after, the pusher solid again.
[[nodiscard]] auto pushBody(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity pusher,
                            ecs::Entity bodyEntity, Vec3 push) -> bool {
    const Body& body = *world.get<Body>(bodyEntity);
    Vec3& origin = world.get<Transform>(bodyEntity)->origin;
    const collision::Trace pushed =
        serverTrace(hulls, solidBrushesOf(world), pusher, body.hull, origin, origin + push);
    if (pushed.fraction != 0.0F) {
        origin = pushed.end;
    }
    return isStuck(hulls, solidBrushesOf(world), body, origin);
}

/// Puts back everything a blocked pusher moved, and blocks it.
void giveWay(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
             std::span<const Moved> moved, Seconds moveTime, const TickTime& clock) {
    Pusher& pusher = *world.get<Pusher>(entity);
    pusher.localTime = pusher.localTime.after(-moveTime, clock.rate);
    for (const Moved& back : moved) {
        world.get<Transform>(back.entity)->origin = back.from;
    }
    if (world.has<Door>(entity)) {
        block(world, hulls, entity, clock);
    }
}

/// The pusher moves by its velocity over `moveTime` (SV_PushMove), moving the bodies it runs into,
/// or that stand on it, along with it; when one cannot go, everything goes back and it gives way.
void pushMove(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity, Seconds moveTime,
              const TickTime& clock) {
    Pusher& pusher = *world.get<Pusher>(entity);
    if (pusher.velocity == Vec3{}) {
        pusher.localTime = pusher.localTime.after(moveTime, clock.rate);
        return;
    }
    const Vec3 move = pusher.velocity * moveTime.count();
    const collision::ModelIndex model = world.get<BrushModel>(entity)->index;
    Transform& transform = *world.get<Transform>(entity);
    const Aabb linked = linkedBoxOf(hulls.boundsOf(model), transform.origin, transform.angles);
    const Aabb swept{.min = linked.min + move, .max = linked.max + move};
    const Vec3 pushedFrom = transform.origin;
    transform.origin = transform.origin + move;
    pusher.localTime = pusher.localTime.after(moveTime, clock.rate);
    if (world.get<BrushSolidity>(entity)->solid == Solid::Not) {
        return;
    }
    std::vector<Moved> moved;
    for (const ecs::Entity bodyEntity : bodiesOf(world)) {
        const Body& body = *world.get<Body>(bodyEntity);
        const Vec3 origin = world.get<Transform>(bodyEntity)->origin;
        if (body.groundModel != model &&
            (!overlaps(body, origin, swept) || !isStuck(hulls, solidBrushesOf(world), body, origin))) {
            continue; // not in its way
        }
        moved.push_back({.entity = bodyEntity, .from = origin});
        if (pushBody(world, hulls, entity, bodyEntity, move)) {
            world.get<Transform>(entity)->origin = pushedFrom;
            giveWay(world, hulls, entity, moved, moveTime, clock);
            return;
        }
    }
}

/// The pusher turns by its angular velocity over `moveTime` (SV_PushRotate), carrying the bodies it
/// runs into, or that stand on it, around its origin; when one cannot go, everything goes back and
/// it gives way. Returns whether it turned.
[[nodiscard]] auto pushRotate(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
                              Seconds moveTime, const TickTime& clock) -> bool {
    Pusher& pusher = *world.get<Pusher>(entity);
    if (pusher.angularVelocity == Vec3{}) {
        pusher.localTime = pusher.localTime.after(moveTime, clock.rate);
        return true;
    }
    const Vec3 turn = pusher.angularVelocity * moveTime.count();
    Transform& transform = *world.get<Transform>(entity);
    const AngleVectors before = angleVectors(transform.angles);
    const EulerAngles turnedFrom = transform.angles;
    transform.angles = eulerOf(vectorOf(transform.angles) + turn);
    const AngleVectors after = angleVectors(transform.angles);
    pusher.localTime = pusher.localTime.after(moveTime, clock.rate);
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
            (!overlaps(body, origin, linked) || !isStuck(hulls, solidBrushesOf(world), body, origin))) {
            continue; // not in its way
        }
        moved.push_back({.entity = bodyEntity, .from = origin});
        // Where the body's place about the pusher's origin goes as the pusher turns.
        const Vec3 start = origin - transform.origin;
        const Vec3 end = outOfAxes(after, intoAxes(before, start));
        if (pushBody(world, hulls, entity, bodyEntity, end - start)) {
            world.get<Transform>(entity)->angles = turnedFrom;
            giveWay(world, hulls, entity, moved, moveTime, clock);
            return false;
        }
    }
    return true;
}

/// The original's SV_Physics_Pusher for one pusher, a tick of `clock`.
void runPusher(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
               const TickTime& clock) {
    Pusher& pusher = *world.get<Pusher>(entity);
    const Seconds frameTime = clock.delta();
    const GameTime startTime = pusher.localTime;
    const std::optional<GameTime> thinkTime = pusher.nextThink;
    // Parity: a pusher with nothing to do by its clock does not move, nor does its clock.
    const Seconds moveTime = !thinkTime ? Seconds{}
                             : *thinkTime < startTime.after(frameTime, clock.rate)
                                 ? std::max(thinkTime->since(pusher.localTime, clock.rate), Seconds{})
                                 : frameTime;
    if (moveTime != Seconds{}) {
        if (pusher.angularVelocity != Vec3{}) {
            if (pusher.velocity != Vec3{}) {
                if (pushRotate(world, hulls, entity, moveTime, clock)) {
                    // Both: the move runs on the clock the turn started from, which keeps the later.
                    Pusher& turned = *world.get<Pusher>(entity);
                    const GameTime afterTurn = turned.localTime;
                    turned.localTime = startTime;
                    pushMove(world, hulls, entity, moveTime, clock);
                    turned.localTime = std::max(turned.localTime, afterTurn);
                }
            } else {
                // Whether it turned matters only to a move that would follow the turn.
                std::ignore = pushRotate(world, hulls, entity, moveTime, clock);
            }
        } else {
            pushMove(world, hulls, entity, moveTime, clock);
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
        if (const Door* door = world.get<Door>(entity)) {
            if (door->state == DoorState::Open) {
                close(world, hulls, entity);
            } else {
                finishMove(world, hulls, entity);
            }
        }
    }
}

} // namespace

void touchBrushEntities(ecs::World& world, const collision::BrushHulls& hulls,
                        std::span<const collision::ModelIndex> models, ecs::Entity toucher) {
    std::vector<ecs::Entity> doors;
    for (const collision::ModelIndex model : models) {
        ecs::Query<ecs::Entity, const BrushModel&, const Door&>{world}.forEach(
            [&doors, model](ecs::Entity entity, const BrushModel& brush, const Door&) {
                if (brush.index == model) {
                    doors.push_back(entity);
                }
            });
    }
    for (const ecs::Entity entity : doors) {
        Door* door = world.isAlive(entity) ? world.get<Door>(entity) : nullptr;
        if (door == nullptr || !door->isTouchable || door->isNamed) {
            continue; // gone, moving, used only, or opened by others
        }
        door->isTouchable = false;
        door->activator = toucher;
        if (door->staysOpen && door->state == DoorState::Open) {
            close(world, hulls, entity);
        } else {
            open(world, hulls, entity);
        }
    }
}

void useDoor(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
             ecs::Entity activator) {
    Door& door = *world.get<Door>(entity);
    door.activator = activator;
    if (door.state == DoorState::Closed) {
        open(world, hulls, entity);
    } else if (door.staysOpen && door.state == DoorState::Open) {
        close(world, hulls, entity);
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
