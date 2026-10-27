// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.game;

import std;
import nx.core;
import nx.math;
import nx.data;
import nx.ecs;
import nx.assets;
import nx.navigation;

namespace nx::game {

namespace {

/// How far above the middle of an upward face the liquid it lies under is looked for: inside the leaf
/// over it.
constexpr float kLiquidProbeHeight = 1.0F;

[[nodiscard]] auto isLiquid(assets::Contents contents) noexcept -> bool {
    return contents == assets::Contents::Water || contents == assets::Contents::Slime ||
           contents == assets::Contents::Lava;
}

/// Where a brush model is: turned by `axes` about its origin, then moved to `origin`.
struct BrushPlacement {
    AngleVectors axes{};
    Vec3 origin{};
};

/// The corners of `face` of `map`, placed by `placement`, counterclockwise seen from where the face
/// looks, whichever way its compiler wound its edges.
[[nodiscard]] auto cornersOf(const assets::BspView& map, const assets::BspFace& face,
                             const BrushPlacement& placement) -> std::vector<Vec3> {
    std::vector<Vec3> corners;
    corners.reserve(face.edgeCount);
    for (std::uint32_t edge = 0; edge < face.edgeCount; ++edge) {
        corners.push_back(placement.origin + outOfAxes(placement.axes, assets::cornerOf(map, face, edge)));
    }
    const Plane& plane = map.planes()[face.plane.value].plane;
    const Vec3 facing = outOfAxes(placement.axes, face.isBackSide ? -plane.normal : plane.normal);
    // The polygon's own normal, the sum of its fan's, against the face's.
    Vec3 wound{};
    for (std::size_t corner = 2; corner < corners.size(); ++corner) {
        wound += cross(corners[corner - 1] - corners.front(), corners[corner] - corners.front());
    }
    if (dot(wound, facing) < 0.0F) {
        std::ranges::reverse(corners);
    }
    return corners;
}

/// Adds to `geometry` the faces of `model` of `map` placed by `placement`, but those of the sky and
/// of liquids' surfaces; a face under a liquid as Water.
void addModelFaces(navigation::Geometry& geometry, const assets::BspView& map, assets::ModelIndex model,
                   const BrushPlacement& placement) {
    const assets::BspModel& brushModel = map.models()[model.value];
    for (std::uint32_t offset = 0; offset < brushModel.faceCount; ++offset) {
        const assets::BspFace& face = map.faces()[brushModel.firstFace.value + offset];
        if (map.textureInfos()[face.textureInfo.value].isSpecial) {
            continue; // the sky, and the surfaces of liquids
        }
        const std::vector<Vec3> corners = cornersOf(map, face, placement);
        Vec3 middle{};
        for (const Vec3 corner : corners) {
            middle += corner;
        }
        middle = middle / static_cast<float>(corners.size());
        const assets::Contents above =
            map.leaves()[map.leafAt(middle + Vec3{.z = kLiquidProbeHeight}).value].contents;
        navigation::addConvexPolygon(
            geometry, corners, isLiquid(above) ? navigation::AreaKind::Water : navigation::AreaKind::Floor);
    }
}

} // namespace

auto readNavigationData(std::string_view text, std::string_view sourceName)
    -> Result<NavigationData, GameError> {
    auto read = data::parseToml<NavigationData>(text, sourceName);
    if (!read) {
        return fail(GameError::ConfigInvalid, std::string{sourceName}, std::move(read.error()));
    }
    if (auto checked = navigation::NavigationMeshes::create(read->profiles, read->build); !checked) {
        return fail(GameError::ConfigInvalid, std::string{sourceName}, std::move(checked.error()));
    }
    return std::move(*read);
}

auto navigationGeometryOf(const assets::BspView& map, ecs::World& world) -> navigation::Geometry {
    navigation::Geometry geometry;
    addModelFaces(geometry, map, assets::kWorldModel, BrushPlacement{.axes = angleVectors(EulerAngles{})});
    ecs::Query<const BrushModel&, const Transform&, const BrushSolidity&>{world}.forEach(
        [&geometry, &map](const BrushModel& model, const Transform& transform,
                          const BrushSolidity& solidity) {
            if (solidity.solid == Solid::Bsp) {
                addModelFaces(
                    geometry, map, model.index,
                    BrushPlacement{.axes = angleVectors(transform.angles), .origin = transform.origin});
            }
        });
    return geometry;
}

} // namespace nx::game
