// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How a map entity asks to be drawn, as the engine reads its keys before its game class does (not
/// exported); its angles are nx.game's (nx::game::anglesOf).
export module cs.client:entityKeys;

import std;
import nx.math;
import nx.assets;
import nx.render;

namespace cs {

/// How an entity asks to be drawn.
struct EntityRendering {
    nx::render::RenderMode mode = nx::render::RenderMode::Normal;
    nx::render::RenderEffect effect = nx::render::RenderEffect::None;
    std::uint8_t amount = 0;
    nx::render::RenderColor color{};
};

/// The `rendermode`, `renderfx`, `renderamt` and `rendercolor` of `entity`: amounts and channels held to
/// a byte, as its state carries them; a mode or an effect the original does not number Normal or None.
[[nodiscard]] auto renderingOf(const nx::assets::BspEntity& entity) -> EntityRendering;

} // namespace cs
