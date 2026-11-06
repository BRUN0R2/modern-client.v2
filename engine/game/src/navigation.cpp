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
/// How far past a brush model's bounds the hull of a body's box reaches, a standing player's being the
/// largest.
constexpr Vec3 kBodyReach{.x = 16.0F, .y = 16.0F, .z = 36.0F};

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

/// What stops a body at `model` of `map`, by the clip hull `clipHull` its compiler grew by the body's box: a
/// map's clip brushes stop bodies in these hulls alone, with no face to show them.
[[nodiscard]] auto bodyHullOf(const assets::BspView& map, assets::ModelIndex model, std::size_t clipHull)
    -> navigation::BodyHull {
    const std::span<const assets::ClipNode> nodes = map.clipNodes();
    const assets::ClipNodeChild& root = map.models()[model.value].hulls[clipHull];
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

auto gatesOf(const assets::BspView& map, ecs::World& world) -> std::vector<navigation::Gate> {
    std::vector<navigation::Gate> gates;
    const auto middleOf = [&map](const BrushModel& model, Vec3 origin) {
        const Aabb& bounds = map.models()[model.index.value].bounds;
        return ((bounds.min + bounds.max) * 0.5F) + origin;
    };
    ecs::Query<const Door&, const BrushModel&, const Transform&, const BrushSolidity&,
               ecs::Optional<const Named>>{world}
        .forEach([&](const Door& door, const BrushModel& model, const Transform& transform,
                     const BrushSolidity& solidity, const Named* named) {
            if (solidity.solid != Solid::Bsp) {
                return;
            }
            const auto* const slide = std::get_if<DoorSlide>(&door.motion);
            navigation::Gate gate{
                .middle = middleOf(model, slide != nullptr ? slide->closed : transform.origin),
                // Walking into it opens one that nothing else does.
                .isOpen = door.state == DoorState::Open || (!door.isNamed && !door.isUseOnly)};
            if (door.isUseOnly && !door.isNamed) {
                gate.openers.push_back(gate.middle);
            }
            if (named != nullptr) {
                ecs::Query<const Button&, const Targets&, const BrushModel&, const Transform&>{world}.forEach(
                    [&](const Button& button, const Targets& targets, const BrushModel& pressed,
                        const Transform& placed) {
                        if (targets.target == named->name && button.isUsable) {
                            gate.openers.push_back(middleOf(pressed, placed.origin));
                        }
                    });
            }
            gates.push_back(std::move(gate));
        });
    return gates;
}

auto navigationGeometryOf(const assets::BspView& map, ecs::World& world) -> navigation::Geometry {
    navigation::Geometry geometry;
    geometry.bodyHull = bodyHullOf(map, assets::kWorldModel, kStandingClipHull);
    geometry.crouchedBodyHull = bodyHullOf(map, assets::kWorldModel, kDuckedClipHull);
    addModelFaces(geometry, map, assets::kWorldModel, BrushPlacement{.axes = angleVectors(EulerAngles{})});
    ecs::Query<ecs::Entity, const BrushModel&, const Transform&, const BrushSolidity&>{world}.forEach(
        [&geometry, &map, &world](ecs::Entity entity, const BrushModel& model, const Transform& transform,
                                  const BrushSolidity& solidity) {
            if (solidity.solid == Solid::Bsp) {
                addModelFaces(
                    geometry, map, model.index,
                    BrushPlacement{.axes = angleVectors(transform.angles), .origin = transform.origin});
                // A hull is of its model unturned: one turned stops bodies by its faces alone.
                if (transform.angles == EulerAngles{}) {
                    const Aabb& bounds = map.models()[model.index.value].bounds;
                    const Aabb reached{.min = bounds.min + transform.origin - kBodyReach,
                                       .max = bounds.max + transform.origin + kBodyReach};
                    geometry.brushBodyHulls.push_back(
                        {.origin = transform.origin,
                         .bounds = reached,
                         .hull = bodyHullOf(map, model.index, kStandingClipHull)});
                    geometry.crouchedBrushBodyHulls.push_back(
                        {.origin = transform.origin,
                         .bounds = reached,
                         .hull = bodyHullOf(map, model.index, kDuckedClipHull)});
                }
            }
            if (solidity.solid == Solid::Bsp && world.has<Door>(entity)) {
                const Aabb& bounds = map.models()[model.index.value].bounds;
                geometry.doors.push_back(linkedBoxOf(bounds, transform.origin, transform.angles));
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
