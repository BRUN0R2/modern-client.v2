// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.math;
import nx.assets;
import nx.render;

namespace cs {

namespace {

/// The most a channel of `rendercolor` and `renderamt` is.
constexpr float kChannelTop = 255.0F;

/// `value` held to a channel, cut to a whole number as the entity's state carries it.
[[nodiscard]] auto channelOf(float value) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0F, kChannelTop));
}

} // namespace

auto renderingOf(const nx::assets::BspEntity& entity) -> EntityRendering {
    using nx::render::RenderEffect;
    using nx::render::RenderMode;
    const int mode = entity.findInteger("rendermode").value_or(0);
    const int effect = entity.findInteger("renderfx").value_or(0);
    const nx::Vec3 color = entity.findVector("rendercolor").value_or(nx::Vec3{});
    return EntityRendering{
        .mode = mode >= 0 && mode < std::to_underlying(RenderMode::Count) ? static_cast<RenderMode>(mode)
                                                                          : RenderMode::Normal,
        .effect = effect >= 0 && effect < std::to_underlying(RenderEffect::Count)
                      ? static_cast<RenderEffect>(effect)
                      : RenderEffect::None,
        .amount = channelOf(entity.findNumber("renderamt").value_or(0.0F)),
        .color = {.red = channelOf(color.x), .green = channelOf(color.y), .blue = channelOf(color.z)}};
}

} // namespace cs
