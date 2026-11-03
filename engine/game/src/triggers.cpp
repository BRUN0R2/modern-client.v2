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

/// A trigger that fired for good goes this long after (CBaseTrigger::ActivateMultiTrigger).
constexpr Seconds kSpentTriggerLife{0.1F};
/// How long a hurt volume's clock runs, whose damage is that share of its damage a second (HurtTouch).
constexpr Seconds kHurtInterval{0.5F};
constexpr float kHurtShareOfASecond = 0.5F;

/// Whether `volume` deals its harm to `toucher` at `now`, as CBaseTrigger::HurtTouch's multiplayer rule
/// weighs it, keeping count of what it touched; in the tick it last hurt something, it hurts what touches
/// it without counting it, so that what it did not count is hurt once more in that half second.
[[nodiscard]] auto hurtsNow(HurtVolume& volume, ecs::Entity toucher, GameTime now) -> bool {
    if (volume.clockEndsAt && *volume.clockEndsAt > now) {
        if (volume.lastHurtAt != now) {
            if (std::ranges::contains(volume.touched, toucher)) {
                return false;
            }
            volume.touched.push_back(toucher);
        }
        return true;
    }
    volume.touched.assign(1, toucher);
    return true;
}

/// `activator` fires `entity`, a trigger, unless it waits still: its targets fire, then it waits its
/// `wait`, or is spent and goes a tenth of a second later.
void activate(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
              ecs::Entity activator) {
    const TickTime clock = tickTimeOf(world);
    if (const NextThink* think = world.get<NextThink>(entity); think != nullptr && think->time > clock.now) {
        return;
    }
    useTargets(world, hulls, entity, activator);
    if (!world.isAlive(entity)) {
        return; // a killtarget of its own
    }
    Trigger& trigger = *world.get<Trigger>(entity);
    if (trigger.wait > Seconds{}) {
        setNextThink(world, entity, clock.now.after(trigger.wait, clock.rate));
        return;
    }
    trigger.isSpent = true;
    setNextThink(world, entity, clock.now.after(kSpentTriggerLife, clock.rate));
}

} // namespace

auto touchesVolume(const collision::BrushHulls& hulls, const Body& body, Vec3 origin,
                   collision::ModelIndex model, const Transform& placement) -> bool {
    const Aabb modelBox = linkedBoxOf(hulls.boundsOf(model), placement.origin, placement.angles);
    if (!linkedBoxOf(body, origin).intersects(modelBox)) {
        return false;
    }
    const collision::BoxHull boxHull = collision::boxHullOf(body.box);
    return hulls.contentsAt(model, boxHull.hull, origin - boxHull.offset, placement.origin, EulerAngles{}) ==
           collision::Contents::Solid;
}

void touchTriggers(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity toucher) {
    TouchedVolumes volumes{world};
    touchTriggers(volumes, world, hulls, toucher);
}

auto touchHurtVolumes(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity toucher)
    -> std::vector<VolumeHarm> {
    TouchedVolumes volumes{world};
    return touchHurtVolumes(volumes, world, hulls, toucher);
}

void touchTriggers(TouchedVolumes& volumes, ecs::World& world, const collision::BrushHulls& hulls,
                   ecs::Entity toucher) {
    const Body* body = world.get<Body>(toucher);
    const Transform* at = world.get<Transform>(toucher);
    expects(body != nullptr && at != nullptr, "touchTriggers: a toucher with a Body and a Transform");
    if (body == nullptr || at == nullptr) {
        return;
    }
    const Body touching = *body;
    const Vec3 origin = at->origin;
    std::vector<ecs::Entity> touched;
    volumes.triggers.forEach(
        [&](ecs::Entity entity, const BrushModel& model, const Transform& placement, const Trigger& trigger) {
            if (!trigger.isSpent && trigger.isTouchedByPlayers &&
                touchesVolume(hulls, touching, origin, model.index, placement)) {
                touched.push_back(entity);
            }
        });
    for (const ecs::Entity entity : touched) {
        if (world.isAlive(entity)) {
            activate(world, hulls, entity, toucher);
        }
    }
}

auto touchHurtVolumes(TouchedVolumes& volumes, ecs::World& world, const collision::BrushHulls& hulls,
                      ecs::Entity toucher) -> std::vector<VolumeHarm> {
    const Body* body = world.get<Body>(toucher);
    const Transform* at = world.get<Transform>(toucher);
    expects(body != nullptr && at != nullptr, "touchHurtVolumes: a toucher with a Body and a Transform");
    std::vector<VolumeHarm> harms;
    if (body == nullptr || at == nullptr) {
        return harms;
    }
    const TickTime clock = tickTimeOf(world);
    volumes.hurtVolumes.forEach([&](const BrushModel& model, const Transform& placement, HurtVolume& volume) {
        if (!volume.isTouchedByPlayers || !touchesVolume(hulls, *body, at->origin, model.index, placement) ||
            !hurtsNow(volume, toucher, clock.now)) {
            return;
        }
        harms.push_back(
            {.damage = Damage{volume.damagePerSecond.value() * kHurtShareOfASecond}, .kind = volume.kind});
        volume.lastHurtAt = clock.now;
        volume.clockEndsAt = clock.now.after(kHurtInterval, clock.rate);
    });
    return harms;
}

} // namespace nx::game
