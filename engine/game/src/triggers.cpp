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

/// Whether `body` at `origin` touches the brush entity of `model` at `placement` (SV_TouchLinks):
/// their linked boxes meet, and the model's hull for the body's size holds its origin, the model not
/// turned (SV_HullForBsp).
[[nodiscard]] auto touches(const collision::BrushHulls& hulls, const Body& body, Vec3 origin,
                           collision::ModelIndex model, const Transform& placement) -> bool {
    const Aabb modelBox = linkedBoxOf(hulls.boundsOf(model), placement.origin, placement.angles);
    if (!linkedBoxOf(body, origin).intersects(modelBox)) {
        return false;
    }
    const collision::BoxHull boxHull = collision::boxHullOf(body.box);
    return hulls.contentsAt(model, boxHull.hull, origin - boxHull.offset, placement.origin, EulerAngles{}) ==
           collision::Contents::Solid;
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

void touchTriggers(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity toucher) {
    const Body* body = world.get<Body>(toucher);
    const Transform* at = world.get<Transform>(toucher);
    expects(body != nullptr && at != nullptr, "touchTriggers: a toucher with a Body and a Transform");
    if (body == nullptr || at == nullptr) {
        return;
    }
    const Body touching = *body;
    const Vec3 origin = at->origin;
    std::vector<ecs::Entity> touched;
    ecs::Query<ecs::Entity, const BrushModel&, const Transform&, const Trigger&>{world}.forEach(
        [&](ecs::Entity entity, const BrushModel& model, const Transform& placement, const Trigger& trigger) {
            if (!trigger.isSpent && trigger.isTouchedByPlayers &&
                touches(hulls, touching, origin, model.index, placement)) {
                touched.push_back(entity);
            }
        });
    for (const ecs::Entity entity : touched) {
        if (world.isAlive(entity)) {
            activate(world, hulls, entity, toucher);
        }
    }
}

} // namespace nx::game
