// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the renderer reads of the faces of a BSP map, for the world it draws and for the light it
/// finds there (not exported): how a face is drawn, from its texture's name, and how many lightmaps it
/// has. Where its luxels lie on it is nx.assets:bspSurfaces'.
export module nx.render:surface;

import std;
import nx.core;
import nx.math;
import nx.assets;

namespace nx::render {

/// How a face is drawn, from its texture's name.
enum class SurfaceKind : std::uint8_t {
    Opaque,
    Masked,    ///< `{`: palette index 255 is transparent.
    Sky,       ///< Shows the side of the sky it is seen toward.
    Turbulent, ///< Water and lasers: unlit, its texture warping.
    Invisible, ///< Triggers and tool textures.
};

/// How a face whose texture is named `name` is drawn.
[[nodiscard]] auto surfaceKindOf(std::string_view name) noexcept -> SurfaceKind;

/// The lightmaps `face` has: one for each of its styles, until the first unused one.
[[nodiscard]] auto lightmapCountOf(const assets::BspFace& face) noexcept -> std::uint32_t;

} // namespace nx::render
