// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;
import nx.math;

namespace nx::assets {

auto cornerOf(const BspView& map, const BspFace& face, std::uint32_t edge) -> Vec3 {
    const SurfaceEdge& surfaceEdge = map.surfaceEdges()[face.firstEdge.value + edge];
    const BspEdge& bspEdge = map.edges()[surfaceEdge.edge.value];
    return map.vertices()[bspEdge.vertices[surfaceEdge.isReversed ? 1 : 0].value];
}

auto textureCoordinateOf(Vec3 point, Vec4 axis) noexcept -> double {
    return (double{point.x} * double{axis.x}) + (double{point.y} * double{axis.y}) +
           (double{point.z} * double{axis.z}) + double{axis.w};
}

auto engineTextureCoordinateOf(Vec3 point, Vec4 axis) noexcept -> float {
    return dot(point, xyz(axis)) + axis.w;
}

auto surfaceExtentOf(const BspView& map, const BspFace& face) -> std::optional<SurfaceExtent> {
    const TextureInfo& info = map.textureInfos()[face.textureInfo.value];
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

auto surfaceExtentsOf(const BspView& map) -> std::vector<std::optional<SurfaceExtent>> {
    return map.faces() |
           std::views::transform([&map](const BspFace& face) { return surfaceExtentOf(map, face); }) |
           std::ranges::to<std::vector>();
}

auto isWithinExtent(Vec3 point, Vec4 s, Vec4 t, const SurfaceExtent& extent) noexcept -> bool {
    // The original truncates the coordinates to whole texels first.
    const float ds = std::trunc(engineTextureCoordinateOf(point, s)) - static_cast<float>(extent.minS);
    const float dt = std::trunc(engineTextureCoordinateOf(point, t)) - static_cast<float>(extent.minT);
    const auto luxelUnits = static_cast<float>(kLuxelUnits);
    return ds >= 0.0F && dt >= 0.0F && ds <= static_cast<float>(extent.width - 1) * luxelUnits &&
           dt <= static_cast<float>(extent.height - 1) * luxelUnits;
}

auto surfaceAt(const BspView& map, std::span<const std::optional<SurfaceExtent>> extents, ModelIndex model,
               Vec3 start, Vec3 end) -> std::optional<FaceIndex> {
    expects(extents.size() == map.faces().size(), "surfaceAt: an extent for each face of the map");
    if (model.value >= map.models().size()) {
        return std::nullopt;
    }
    const auto meets = [&](FaceIndex index, Vec3 point) -> std::optional<FaceIndex> {
        const std::optional<SurfaceExtent>& extent = extents[index.value];
        const TextureInfo& info = map.textureInfos()[map.faces()[index.value].textureInfo.value];
        if (!extent || !isWithinExtent(point, info.s, info.t, *extent)) {
            return std::nullopt;
        }
        return index;
    };
    return firstAlong<FaceIndex>(map, NodeChild{map.models()[model.value].headNode}, start, end, meets);
}

} // namespace nx::assets
