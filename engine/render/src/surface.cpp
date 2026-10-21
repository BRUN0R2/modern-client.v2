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

auto cornerOf(const assets::BspView& map, const assets::BspFace& face, std::uint32_t edge) -> Vec3 {
    const assets::SurfaceEdge& surfaceEdge = map.surfaceEdges()[face.firstEdge.value + edge];
    const assets::BspEdge& bspEdge = map.edges()[surfaceEdge.edge.value];
    return map.vertices()[bspEdge.vertices[surfaceEdge.isReversed ? 1 : 0].value];
}

auto textureCoordinateOf(Vec3 point, Vec4 axis) noexcept -> double {
    return (double{point.x} * double{axis.x}) + (double{point.y} * double{axis.y}) +
           (double{point.z} * double{axis.z}) + double{axis.w};
}

auto surfaceExtentOf(const assets::BspView& map, const assets::BspFace& face)
    -> std::optional<SurfaceExtent> {
    const assets::TextureInfo& info = map.textureInfos()[face.textureInfo.value];
    double minS = std::numeric_limits<double>::max();
    double maxS = std::numeric_limits<double>::lowest();
    double minT = minS;
    double maxT = maxS;
    for (std::uint32_t edge = 0; edge < face.edgeCount; ++edge) {
        const Vec3 point = cornerOf(map, face, edge);
        minS = std::min(minS, textureCoordinateOf(point, info.s));
        maxS = std::max(maxS, textureCoordinateOf(point, info.s));
        minT = std::min(minT, textureCoordinateOf(point, info.t));
        maxT = std::max(maxT, textureCoordinateOf(point, info.t));
    }
    const double firstS = std::floor(minS / kLuxelUnits);
    const double firstT = std::floor(minT / kLuxelUnits);
    const auto width = tryRound<std::uint32_t>(std::ceil(maxS / kLuxelUnits) - firstS + 1.0);
    const auto height = tryRound<std::uint32_t>(std::ceil(maxT / kLuxelUnits) - firstT + 1.0);
    if (!width || !height) {
        return std::nullopt;
    }
    return SurfaceExtent{
        .minS = firstS * kLuxelUnits, .minT = firstT * kLuxelUnits, .width = *width, .height = *height};
}

auto lightmapCountOf(const assets::BspFace& face) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(std::ranges::distance(
        face.lightStyles |
        std::views::take_while([](std::uint8_t style) { return style != assets::kNoLightStyle; })));
}

} // namespace nx::render
