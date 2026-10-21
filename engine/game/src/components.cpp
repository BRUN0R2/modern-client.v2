// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.game;

import std;
import nx.core;
import nx.ecs;

namespace nx::game {

auto registerComponents(ecs::World& world) -> Result<void, ecs::ComponentError> {
    const std::array registered{world.registerComponent<BrushModel>("nx::game::BrushModel"),
                                world.registerComponent<Transform>("nx::game::Transform"),
                                world.registerComponent<BrushSolidity>("nx::game::BrushSolidity"),
                                world.registerComponent<Pusher>("nx::game::Pusher"),
                                world.registerComponent<Door>("nx::game::Door"),
                                world.registerComponent<Body>("nx::game::Body")};
    for (const auto& component : registered) {
        if (!component) {
            return std::unexpected{component.error()};
        }
    }
    return {};
}

} // namespace nx::game
