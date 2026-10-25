// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.render;

import std;
import nx.core;
import nx.math;
import nx.assets;

namespace nx::render {

namespace {

/// The textures compilers leave on brush entities for the game, never drawn: the triggers' and the
/// tool textures that survive compiling.
constexpr auto kInvisibleTextures =
    std::to_array<std::string_view>({"aaatrigger", "clip", "origin", "null", "skip", "hint", "bevel"});

[[nodiscard]] auto startsWithIgnoringCase(std::string_view text, std::string_view prefix) noexcept -> bool {
    return text.size() >= prefix.size() && equalsIgnoringCase(text.substr(0, prefix.size()), prefix);
}

} // namespace

auto surfaceKindOf(std::string_view name) noexcept -> SurfaceKind {
    if (equalsIgnoringCase(name, "sky")) {
        return SurfaceKind::Sky;
    }
    if (std::ranges::any_of(kInvisibleTextures, [name](std::string_view invisible) {
            return equalsIgnoringCase(name, invisible);
        })) {
        return SurfaceKind::Invisible;
    }
    // Mod_LooksLikeWaterTexture: '!' and Quake's '*', and the names that start with "water" or "laser".
    if (name.starts_with('!') || name.starts_with('*') || startsWithIgnoringCase(name, "water") ||
        startsWithIgnoringCase(name, "laser")) {
        return SurfaceKind::Turbulent;
    }
    return name.starts_with('{') ? SurfaceKind::Masked : SurfaceKind::Opaque;
}

auto lightmapCountOf(const assets::BspFace& face) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(std::ranges::distance(
        face.lightStyles |
        std::views::take_while([](std::uint8_t style) { return style != assets::kNoLightStyle; })));
}

} // namespace nx::render
