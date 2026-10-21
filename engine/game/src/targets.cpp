// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.game;

import std;
import nx.core;
import nx.ecs;
import nx.collision;

namespace nx::game {

namespace {

constexpr log::Category kLog{"game"};

/// How deep a chain of targets that fire targets may go (ReGameDLL's MAX_TARGET_RECURSION_LEVEL).
constexpr std::uint32_t kDeepestChain = 128;

/// The entities of `world` named `name`, in its order.
[[nodiscard]] auto namedEntitiesOf(ecs::World& world, std::string_view name) -> std::vector<ecs::Entity> {
    std::vector<ecs::Entity> named;
    ecs::Query<ecs::Entity, const Named&>{world}.forEach(
        [&named, name](ecs::Entity entity, const Named& its) {
            if (its.name == name) {
                named.push_back(entity);
            }
        });
    return named;
}

} // namespace

void useTargets(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity caller,
                ecs::Entity activator) {
    const Targets* targets = world.get<Targets>(caller);
    if (targets == nullptr || (targets->target.empty() && targets->killTarget.empty())) {
        return;
    }
    // A copy: removing and using entities may move the caller's.
    const Targets fired = *targets;
    if (fired.delay != Seconds{}) {
        const TickTime& clock = tickTimeOf(world);
        world.spawn(Targets{.target = fired.target, .killTarget = fired.killTarget},
                    DelayedUse{.activator = activator},
                    NextThink{.time = clock.now.after(fired.delay, clock.rate)});
        return;
    }
    if (!fired.killTarget.empty()) {
        for (const ecs::Entity killed : namedEntitiesOf(world, fired.killTarget)) {
            world.destroy(killed);
        }
    }
    fireTargets(world, hulls, fired.target, activator);
}

void fireTargets(ecs::World& world, const collision::BrushHulls& hulls, std::string_view name,
                 ecs::Entity activator) {
    if (name.empty()) {
        return;
    }
    TargetChain* chain = world.resource<TargetChain>();
    expects(chain != nullptr, "fireTargets: a world registerComponents made ready");
    if (chain == nullptr) {
        return;
    }
    if (chain->depth >= kDeepestChain) {
        log::warning(kLog, "targets named {:?} fire targets {} deep: the chain stops", name, kDeepestChain);
        return;
    }
    ++chain->depth;
    for (const ecs::Entity target : namedEntitiesOf(world, name)) {
        if (world.isAlive(target)) {
            use(world, hulls, target, activator);
        }
    }
    --chain->depth; // a resource stays where it is
}

void use(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity, ecs::Entity activator) {
    if (world.has<Door>(entity)) {
        useDoor(world, hulls, entity, activator);
    } else if (world.has<Button>(entity)) {
        useButton(world, hulls, entity, activator);
    } else if (AlternateTextures* textures = world.get<AlternateTextures>(entity);
               textures != nullptr && world.has<Wall>(entity)) {
        // CFuncWall::Use: every use is a toggle (USE_TOGGLE) today.
        textures->isShown = !textures->isShown;
    }
}

} // namespace nx::game
