// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.game;

import std;
import nx.core;
import nx.ecs;

namespace nx::game {

auto registerComponents(ecs::World& world) -> Result<void, ecs::ComponentError> {
    const std::array registered{world.registerComponent<Classified>("nx::game::Classified"),
                                world.registerComponent<BrushModel>("nx::game::BrushModel"),
                                world.registerComponent<Transform>("nx::game::Transform"),
                                world.registerComponent<BrushSolidity>("nx::game::BrushSolidity"),
                                world.registerComponent<BoxSolidity>("nx::game::BoxSolidity"),
                                world.registerComponent<ClipMethod>("nx::game::ClipMethod"),
                                world.registerComponent<DamageMethod>("nx::game::DamageMethod"),
                                world.registerComponent<Pusher>("nx::game::Pusher"),
                                world.registerComponent<Door>("nx::game::Door"),
                                world.registerComponent<Waves>("nx::game::Waves"),
                                world.registerComponent<AlternateTextures>("nx::game::AlternateTextures"),
                                world.registerComponent<Wall>("nx::game::Wall"),
                                world.registerComponent<RestartsEachRound>("nx::game::RestartsEachRound"),
                                world.registerComponent<Body>("nx::game::Body"),
                                world.registerComponent<NextThink>("nx::game::NextThink"),
                                world.registerComponent<ThinkMethod>("nx::game::ThinkMethod"),
                                world.registerComponent<Named>("nx::game::Named"),
                                world.registerComponent<Targets>("nx::game::Targets"),
                                world.registerComponent<DelayedUse>("nx::game::DelayedUse"),
                                world.registerComponent<Trigger>("nx::game::Trigger"),
                                world.registerComponent<Button>("nx::game::Button")};
    for (const auto& component : registered) {
        if (!component) {
            return std::unexpected{component.error()};
        }
    }
    world.insertResource(TickTime{});
    world.insertResource(TargetChain{});
    return {};
}

} // namespace nx::game
