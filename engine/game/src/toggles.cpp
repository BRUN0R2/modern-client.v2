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

/// The move under way ends where it was going, and the toggle arrives.
void finishMove(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity) {
    Pusher& pusher = *world.get<Pusher>(entity);
    Transform& transform = *world.get<Transform>(entity);
    if (pusher.isTurning) {
        transform.angles = pusher.destinationAngles;
        pusher.angularVelocity = {};
    } else {
        transform.origin = pusher.destination;
        pusher.velocity = {};
    }
    pusher.nextThink = std::nullopt;
    if (world.has<Door>(entity)) {
        doorArrived(world, hulls, entity);
    } else if (world.has<Button>(entity)) {
        buttonArrived(world, hulls, entity);
    }
}

} // namespace

auto vectorOf(EulerAngles angles) -> Vec3 {
    return Vec3{.x = angles.pitch.value(), .y = angles.yaw.value(), .z = angles.roll.value()};
}

auto eulerOf(Vec3 angles) -> EulerAngles {
    return EulerAngles{.pitch = Degrees{angles.x}, .yaw = Degrees{angles.y}, .roll = Degrees{angles.z}};
}

void moveTo(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity, Vec3 destination,
            UnitsPerSecond speed) {
    Pusher& pusher = *world.get<Pusher>(entity);
    const Vec3 origin = world.get<Transform>(entity)->origin;
    pusher.destination = destination;
    pusher.isTurning = false;
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

void turnTo(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
            EulerAngles destination, DegreesPerSecond speed) {
    Pusher& pusher = *world.get<Pusher>(entity);
    const EulerAngles angles = world.get<Transform>(entity)->angles;
    pusher.destinationAngles = destination;
    pusher.isTurning = true;
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

void thinkToggle(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity) {
    if (const Door* door = world.get<Door>(entity); door != nullptr && door->state == DoorState::Open) {
        closeDoor(world, hulls, entity);
        return;
    }
    if (const Button* button = world.get<Button>(entity);
        button != nullptr && button->state == ButtonState::In) {
        returnButton(world, hulls, entity);
        return;
    }
    finishMove(world, hulls, entity);
}

} // namespace nx::game
