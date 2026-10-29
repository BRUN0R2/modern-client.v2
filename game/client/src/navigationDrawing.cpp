// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.navigation;
import nx.agents;
import nx.render;

namespace cs {

namespace {

using nx::agents::Behavior;
using nx::navigation::AreaKind;
using nx::render::OverlayColor;
using nx::render::OverlayVertex;

/// The color of each kind of area: its edges as they are, its polygons see-through enough that the floor's
/// texture shows, even blended as linear light in the enhanced look.
constexpr nx::EnumArray<AreaKind, OverlayColor> kAreaColors{nx::EnumArray<AreaKind, OverlayColor>::Storage{
    OverlayColor{.red = 64, .green = 208, .blue = 64},    // Floor: green
    OverlayColor{.red = 240, .green = 200, .blue = 32},   // Crouch: yellow
    OverlayColor{.red = 48, .green = 128, .blue = 255}}}; // Water: blue
constexpr std::uint8_t kPolygonAlpha = 32;

/// The colors of the ways, by why their agents move.
constexpr OverlayColor kChaseColor{.red = 255, .green = 48, .blue = 48};
constexpr OverlayColor kInvestigateColor{.red = 255, .green = 144, .blue = 32};
constexpr OverlayColor kGoalColor{.red = 255, .green = 255, .blue = 255};
/// How many times its light a line gives in the enhanced look, a glow its bloom spreads about it: the
/// mesh's edges a little, the ways enough to stand out of the mesh.
constexpr float kEdgeBrightness = 3.0F;
constexpr float kWayBrightness = 24.0F;
/// How tall the mark at a way's end stands: a crouched player's height.
constexpr nx::Vec3 kEndMark{.z = 36.0F};

/// The color of the way of an agent whose last decision was of `behavior`, one that follows its corridor.
[[nodiscard]] constexpr auto wayColorOf(Behavior behavior) noexcept -> OverlayColor {
    if (behavior == Behavior::Chase) {
        return kChaseColor;
    }
    return behavior == Behavior::Investigate ? kInvestigateColor : kGoalColor;
}

} // namespace

NavigationDrawing::NavigationDrawing(const nx::navigation::NavigationMesh& mesh, nx::HammerUnits height)
    : query{mesh}, lift{.z = height.value()} {
    const nx::navigation::MeshOutline outline = mesh.outline();
    for (const nx::navigation::MeshPolygon& polygon : outline.polygons) {
        const std::span<const nx::Vec3> corners =
            std::span{outline.corners}.subspan(polygon.firstCorner, polygon.cornerCount);
        const OverlayColor edge = kAreaColors[polygon.kind];
        const OverlayColor fill{
            .red = edge.red, .green = edge.green, .blue = edge.blue, .alpha = kPolygonAlpha};
        for (std::size_t corner = 2; corner < corners.size(); ++corner) {
            for (const nx::Vec3 point : {corners.front(), corners[corner - 1], corners[corner]}) {
                polygons.push_back({.position = point + lift, .color = fill});
            }
        }
        for (std::size_t corner = 0; corner < corners.size(); ++corner) {
            lines.push_back(
                {.position = corners[corner] + lift, .color = edge, .brightness = kEdgeBrightness});
            lines.push_back({.position = corners[(corner + 1) % corners.size()] + lift,
                             .color = edge,
                             .brightness = kEdgeBrightness});
        }
    }
    edgeVertices = lines.size();
}

void NavigationDrawing::clearWays() {
    lines.resize(edgeVertices);
}

void NavigationDrawing::showWayOf(const nx::agents::Agent& agent) {
    const std::vector<nx::Vec3> way = agent.way(query);
    if (way.empty()) {
        return;
    }
    const OverlayColor color = wayColorOf(agent.behavior);
    for (std::size_t point = 1; point < way.size(); ++point) {
        lines.push_back({.position = way[point - 1] + lift, .color = color, .brightness = kWayBrightness});
        lines.push_back({.position = way[point] + lift, .color = color, .brightness = kWayBrightness});
    }
    const nx::Vec3 end = way.back() + lift;
    lines.push_back({.position = end, .color = color, .brightness = kWayBrightness});
    lines.push_back({.position = end + kEndMark, .color = color, .brightness = kWayBrightness});
}

auto NavigationDrawing::overlay() const noexcept -> SceneOverlay {
    return SceneOverlay{.triangles = polygons, .lines = lines};
}

} // namespace cs
