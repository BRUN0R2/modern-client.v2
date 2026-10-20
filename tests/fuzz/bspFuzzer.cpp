// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// BSP v30 maps (nx.assets:bsp). A map that parses is walked as a renderer and a collision trace
// would, without checks: parse() promises that every index it returns is in range and that every
// tree ends in leaves, and AddressSanitizer holds it to that.

import std;
import nx.math;
import nx.assets;

#include "fuzzTarget.hpp"

namespace {

using nx::assets::BspView;

[[nodiscard]] auto walkFace(const BspView& map, nx::assets::FaceIndex index) -> float {
    const nx::assets::BspFace& face = map.faces()[index.value];
    float sum = map.planes()[face.plane.value].plane.distance;
    const auto& textureInfo = map.textureInfos()[face.textureInfo.value];
    sum += static_cast<float>(map.textures()[textureInfo.texture.value].has_value());
    for (std::uint32_t edge = 0; edge < face.edgeCount; ++edge) {
        const auto& surfaceEdge = map.surfaceEdges()[face.firstEdge.value + edge];
        const auto& vertices = map.edges()[surfaceEdge.edge.value].vertices;
        sum += map.vertices()[(surfaceEdge.isReversed ? vertices[1] : vertices[0]).value].x;
    }
    if (face.lightmapOffset) {
        sum += static_cast<float>(std::to_integer<int>(map.lighting()[*face.lightmapOffset]));
    }
    return sum;
}

/// Every leaf under `root`, and the faces they mark. Ends because children come after parents.
[[nodiscard]] auto walkNodes(const BspView& map, nx::assets::NodeIndex root) -> float {
    float sum = 0.0F;
    std::vector<nx::assets::NodeChild> pending{root};
    while (!pending.empty()) {
        const nx::assets::NodeChild child = pending.back();
        pending.pop_back();
        if (const auto* node = std::get_if<nx::assets::NodeIndex>(&child)) {
            const nx::assets::BspNode& visited = map.nodes()[node->value];
            sum += map.planes()[visited.plane.value].plane.distance;
            pending.insert(pending.end(), visited.children.begin(), visited.children.end());
            continue;
        }
        const nx::assets::BspLeaf& leaf = map.leaves()[std::get<nx::assets::LeafIndex>(child).value];
        for (std::uint32_t mark = 0; mark < leaf.markSurfaceCount; ++mark) {
            sum += walkFace(map, map.markSurfaces()[leaf.firstMarkSurface.value + mark]);
        }
        if (leaf.visibilityOffset) {
            sum += static_cast<float>(std::to_integer<int>(map.visibility()[*leaf.visibilityOffset]));
        }
    }
    return sum;
}

/// Every clip node under `root`.
[[nodiscard]] auto walkHull(const BspView& map, nx::assets::ClipNodeChild root) -> float {
    float sum = 0.0F;
    std::vector<nx::assets::ClipNodeChild> pending{root};
    while (!pending.empty()) {
        const nx::assets::ClipNodeChild child = pending.back();
        pending.pop_back();
        if (const auto* node = std::get_if<nx::assets::ClipNodeIndex>(&child)) {
            const nx::assets::ClipNode& visited = map.clipNodes()[node->value];
            sum += map.planes()[visited.plane.value].plane.distance;
            pending.insert(pending.end(), visited.children.begin(), visited.children.end());
        }
    }
    return sum;
}

} // namespace

void nx::test::fuzz(std::span<const std::byte> input) {
    const auto map = BspView::parse(input);
    if (!map) {
        return;
    }
    float sum = 0.0F;
    for (std::uint32_t face = 0; face < map->faces().size(); ++face) {
        sum += walkFace(*map, nx::assets::FaceIndex{.value = face});
    }
    for (const nx::assets::BspModel& model : map->models()) {
        sum += walkNodes(*map, model.headNode);
        for (const nx::assets::ClipNodeChild& hull : model.hulls) {
            sum += walkHull(*map, hull);
        }
        for (std::uint32_t face = 0; face < model.faceCount; ++face) {
            sum += walkFace(*map, nx::assets::FaceIndex{.value = model.firstFace.value + face});
        }
    }
    for (const nx::assets::BspEntity& entity : map->entities()) {
        sum += static_cast<float>(entity.find("classname").has_value());
    }
    std::ignore = sum; // the reads are the test
}
