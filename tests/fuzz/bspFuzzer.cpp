// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// BSP v30 maps (nx.assets:bsp). A map that parses is walked as a renderer would, without checks:
// parse() promises that every index it returns is in range, and AddressSanitizer holds it to that.

import std;
import nx.math;
import nx.assets;

#include "fuzzTarget.hpp"

void nx::test::fuzz(std::span<const std::byte> input) {
    const auto map = nx::assets::BspView::parse(input);
    if (!map) {
        return;
    }
    float sum = 0.0F;
    for (const nx::assets::BspFace& face : map->faces()) {
        sum += map->planes()[face.plane.value].plane.distance;
        const auto& textureInfo = map->textureInfos()[face.textureInfo.value];
        sum += static_cast<float>(map->textures()[textureInfo.texture.value].has_value());
        for (std::uint32_t edge = 0; edge < face.edgeCount; ++edge) {
            const auto& surfaceEdge = map->surfaceEdges()[face.firstEdge.value + edge];
            const auto& vertices = map->edges()[surfaceEdge.edge.value].vertices;
            sum += map->vertices()[(surfaceEdge.isReversed ? vertices[1] : vertices[0]).value].x;
        }
        if (face.lightmapOffset) {
            sum += static_cast<float>(std::to_integer<int>(map->lighting()[*face.lightmapOffset]));
        }
    }
    std::ignore = sum; // the reads are the test
}
