// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the renderer reads of the faces of a BSP map, for the world it draws and for the light it
/// finds there (not exported): how a face is drawn, from its texture's name, and where its luxels lie
/// on it, as the compilers that wrote its lightmaps measured them.
export module nx.render:surface;

import std;
import nx.core;
import nx.math;
import nx.assets;

namespace nx::render {

/// Map units per lightmap sample: GoldSrc's luxel.
inline constexpr double kLuxelUnits = 16.0;

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

/// Where a face's luxels lie on it: in texels of its texture, the first luxel's coordinates (the next
/// ones 16 texels apart), and how many luxels it has across and down.
struct SurfaceExtent {
    double minS = 0.0;
    double minT = 0.0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

/// Corner `edge` of `face`, in the order the compiler wound its edges.
[[nodiscard]] auto cornerOf(const assets::BspView& map, const assets::BspFace& face, std::uint32_t edge)
    -> Vec3;

/// The texture coordinate of `point` along `axis` (a texture's s or t), in double precision as the
/// compilers compute it.
[[nodiscard]] auto textureCoordinateOf(Vec3 point, Vec4 axis) noexcept -> double;

/// The extent of `face`, as the compilers measure it from its corners (CalcFaceExtents); none when
/// it is more luxels than a count holds.
[[nodiscard]] auto surfaceExtentOf(const assets::BspView& map, const assets::BspFace& face)
    -> std::optional<SurfaceExtent>;

/// The lightmaps `face` has: one for each of its styles, until the first unused one.
[[nodiscard]] auto lightmapCountOf(const assets::BspFace& face) noexcept -> std::uint32_t;

} // namespace nx::render
