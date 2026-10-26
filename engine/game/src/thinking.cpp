// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.game;

import std;
import nx.core;
import nx.ecs;
import nx.collision;

namespace nx::game {

void setNextThink(ecs::World& world, ecs::Entity entity, GameTime time) {
    if (NextThink* think = world.get<NextThink>(entity)) {
        think->time = time;
        return;
    }
    world.add(entity, NextThink{.time = time});
}

void runThinks(ecs::World& world, const collision::BrushHulls& hulls) {
    // SV_RunThink: what is due before the next tick starts.
    const GameTime end = tickTimeOf(world).now.ticksLater(1);
    struct Due {
        GameTime time{};
        ecs::Entity entity{};
    };
    std::vector<Due> due;
    ecs::Query<ecs::Entity, const NextThink&>{world}.forEach(
        [&due, end](ecs::Entity entity, const NextThink& think) {
            if (think.time <= end) {
                due.push_back(Due{.time = think.time, .entity = entity});
            }
        });
    std::ranges::sort(due, [](const Due& first, const Due& second) {
        return first.time != second.time ? first.time < second.time : first.entity < second.entity;
    });
    for (const Due& acting : due) {
        // One that went, or acts at another time, since this tick began does not act now.
        const NextThink* think = world.isAlive(acting.entity) ? world.get<NextThink>(acting.entity) : nullptr;
        if (think == nullptr || think->time != acting.time) {
            continue;
        }
        world.remove<NextThink>(acting.entity);
        if (const ThinkMethod* method = world.get<ThinkMethod>(acting.entity);
            method != nullptr && method->think != nullptr) {
            method->think(world, hulls, acting.entity, std::max(acting.time, tickTimeOf(world).now));
        } else if (const DelayedUse* delayed = world.get<DelayedUse>(acting.entity)) {
            useTargets(world, hulls, acting.entity, delayed->activator);
            if (world.isAlive(acting.entity)) {
                world.destroy(acting.entity);
            }
        } else if (const Trigger* trigger = world.get<Trigger>(acting.entity);
                   trigger != nullptr && trigger->isSpent) {
            world.destroy(acting.entity);
        }
    }
}

} // namespace nx::game
