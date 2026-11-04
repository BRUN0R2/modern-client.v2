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

/// Which of a model's clip hulls is a standing player's and a ducked one's, after the large entities' between
/// them (docs/07-simulation-and-physics.md, section 3.3).
constexpr std::size_t kStandingClipHull = 0;
constexpr std::size_t kDuckedClipHull = 2;

/// The clip nodes of `nodes` under `root`, each before those it names: a compiler writes a subtree that
/// several nodes share once, which may come before them.
[[nodiscard]] auto parentsFirst(std::span<const assets::ClipNode> nodes, const assets::ClipNodeChild& root)
    -> std::vector<std::uint32_t> {
    struct Visit {
        std::uint32_t node = 0;
        std::size_t nextChild = 0;
    };
    std::vector<std::uint32_t> childrenFirst;
    std::vector<bool> isSeen(nodes.size());
    std::vector<Visit> pending;
    const auto visit = [&](const assets::ClipNodeChild& child) {
        const auto* index = std::get_if<assets::ClipNodeIndex>(&child);
        if (index != nullptr && !isSeen[index->value]) {
            isSeen[index->value] = true;
            pending.push_back({.node = index->value});
        }
    };
    visit(root);
    while (!pending.empty()) {
        Visit& top = pending.back();
        const auto& children = nodes[top.node].children;
        if (top.nextChild == children.size()) {
            childrenFirst.push_back(top.node);
            pending.pop_back();
        } else {
            visit(children[top.nextChild++]);
        }
    }
    std::ranges::reverse(childrenFirst);
    return childrenFirst;
}

/// What stops a body in the world of `map`, by the clip hull `clipHull` its compiler grew by the body's box:
/// its clip brushes stop bodies in these hulls alone, with no face to show them.
[[nodiscard]] auto worldHullOf(const assets::BspView& map, std::size_t clipHull) -> navigation::BodyHull {
    const std::span<const assets::ClipNode> nodes = map.clipNodes();
    const assets::ClipNodeChild& root = map.models()[assets::kWorldModel.value].hulls[clipHull];
    const std::vector<std::uint32_t> order = parentsFirst(nodes, root);
    std::vector<std::uint32_t> places(nodes.size());
    for (std::uint32_t place = 0; place < order.size(); ++place) {
        places[order[place]] = place;
    }
    const auto childOf = [&places](const assets::ClipNodeChild& child) -> navigation::BodyHullChild {
        if (const auto* index = std::get_if<assets::ClipNodeIndex>(&child)) {
            return places[index->value];
        }
        return std::get<assets::Contents>(child) == assets::Contents::Solid ? navigation::BodyHullLeaf::Solid
                                                                            : navigation::BodyHullLeaf::Open;
    };
    navigation::BodyHull hull{.root = childOf(root)};
    hull.nodes.reserve(order.size());
    for (const std::uint32_t index : order) {
        const assets::ClipNode& node = nodes[index];
        hull.nodes.push_back({.plane = map.planes()[node.plane.value].plane,
                              .children = {childOf(node.children[0]), childOf(node.children[1])}});
    }
    return hull;
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
    geometry.bodyHull = worldHullOf(map, kStandingClipHull);
    geometry.crouchedBodyHull = worldHullOf(map, kDuckedClipHull);
    addModelFaces(geometry, map, assets::kWorldModel, BrushPlacement{.axes = angleVectors(EulerAngles{})});
    ecs::Query<const BrushModel&, const Transform&, const BrushSolidity&>{world}.forEach(
        [&geometry, &map](const BrushModel& model, const Transform& transform,
                          const BrushSolidity& solidity) {
            if (solidity.solid == Solid::Bsp) {
                addModelFaces(
                    geometry, map, model.index,
                    BrushPlacement{.axes = angleVectors(transform.angles), .origin = transform.origin});
            }
            if (solidity.contents == assets::Contents::Ladder) {
                const Aabb& bounds = map.models()[model.index.value].bounds;
                geometry.ladders.push_back(
                    Aabb{.min = bounds.min + transform.origin, .max = bounds.max + transform.origin});
            }
        });
    return geometry;
}

} // namespace nx::game
