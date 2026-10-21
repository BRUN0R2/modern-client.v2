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

} // namespace

void openDoor(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity) {
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

void closeDoor(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity) {
    Door& door = *world.get<Door>(entity);
    door.state = DoorState::Closing;
    if (const auto* slide = std::get_if<DoorSlide>(&door.motion)) {
        moveTo(world, hulls, entity, slide->closed, slide->speed);
        return;
    }
    const DoorTurn& turn = std::get<DoorTurn>(door.motion);
    turnTo(world, hulls, entity, turn.closed, turn.speed);
}

void doorArrived(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity) {
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

void touchDoor(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
               ecs::Entity toucher) {
    Door& door = *world.get<Door>(entity);
    if (!door.isTouchable || door.isNamed) {
        return; // moving, used only, or opened by others
    }
    door.isTouchable = false;
    door.activator = toucher;
    if (door.staysOpen && door.state == DoorState::Open) {
        closeDoor(world, hulls, entity);
    } else {
        openDoor(world, hulls, entity);
    }
}

void useDoor(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
             ecs::Entity activator) {
    Door& door = *world.get<Door>(entity);
    door.activator = activator;
    if (door.state == DoorState::Closed) {
        openDoor(world, hulls, entity);
    } else if (door.staysOpen && door.state == DoorState::Open) {
        closeDoor(world, hulls, entity);
    }
}

void blockDoor(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
               const TickTime& clock) {
    Door& door = *world.get<Door>(entity);
    if (clock.now.since(door.lastBlocked, clock.rate) < kBlockedInterval) {
        return;
    }
    door.lastBlocked = clock.now;
    if (door.wait >= Seconds{}) {
        if (door.state == DoorState::Closing) {
            openDoor(world, hulls, entity);
        } else {
            closeDoor(world, hulls, entity);
        }
    }
}

} // namespace nx::game
