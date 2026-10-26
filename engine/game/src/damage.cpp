// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.game;

import std;
import nx.core;
import nx.ecs;

namespace nx::game {

auto attackEntity(ecs::World& world, ecs::Entity entity, const Attack& attack) -> Harm {
    const DamageMethod* const method = world.get<DamageMethod>(entity);
    if (method == nullptr || method->takeAttack == nullptr) {
        return Harm{};
    }
    return method->takeAttack(world, entity, attack);
}

} // namespace nx::game
