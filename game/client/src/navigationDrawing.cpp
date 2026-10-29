// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.navigation;
import nx.agents;
import nx.render;
import cs.shared;

namespace cs {

namespace {

using nx::agents::Behavior;
using nx::navigation::AreaKind;
using nx::render::OverlayArrows;
using nx::render::OverlayColor;

/// The color of each kind of area: its edges as they are, its polygons see-through enough that the floor's
/// texture shows, even blended as linear light in the enhanced look.
constexpr nx::EnumArray<AreaKind, OverlayColor> kAreaColors{nx::EnumArray<AreaKind, OverlayColor>::Storage{
    OverlayColor{.red = 64, .green = 208, .blue = 64},    // Floor: green
    OverlayColor{.red = 240, .green = 200, .blue = 32},   // Crouch: yellow
    OverlayColor{.red = 48, .green = 128, .blue = 255}}}; // Water: blue
constexpr std::uint8_t kPolygonAlpha = 32;

/// The colors of the arrows' shafts, one for each team, and for an agent of no team.
constexpr nx::EnumArray<Team, OverlayColor> kTeamColors{nx::EnumArray<Team, OverlayColor>::Storage{
    OverlayColor{.red = 255, .green = 64, .blue = 48},                     // Terrorists: red
    OverlayColor{.red = 64, .green = 144, .blue = 255}}};                  // CounterTerrorists: blue
constexpr OverlayColor kNoTeamColor{.red = 184, .green = 72, .blue = 255}; // purple

/// The colors of the arrows' heads, one for each reason an agent goes where it goes.
constexpr OverlayColor kRouteColor{.red = 0, .green = 224, .blue = 255};       // its route to its goal: cyan
constexpr OverlayColor kInvestigateColor{.red = 255, .green = 176, .blue = 0}; // where it saw one: amber
constexpr OverlayColor kChaseColor{.red = 255, .green = 255, .blue = 255};     // an enemy it sees: white
constexpr OverlayColor kStuckColor{.red = 255, .green = 0, .blue = 160};       // stuck on its way: magenta

/// How many times its light a line or an arrow gives in the enhanced look, a glow its bloom spreads
/// about it: the mesh's edges a little, the ways' arrows enough to stand out of the mesh.
constexpr float kEdgeBrightness = 3.0F;
constexpr float kArrowBrightness = 6.0F;

/// The ways' arrows as the original's beam of sprites/arrow1.spr draws a way (TE_BEAMPOINTS, as a plugin
/// draws a nav path with it: a width of 64, the band twice the tenths of it across, 12.8; the sprite's two
/// arrows once every 100 units, so one every 50), 16 units above the way's points.
constexpr nx::render::OverlayArrowStyle kArrowStyle{.width = nx::HammerUnits{12.8F},
                                                    .spacing = nx::HammerUnits{50.0F}};
constexpr nx::Vec3 kArrowLift{.z = 16.0F};

/// The fewest agents a batch of showWays finds the ways of: a way costs some microseconds on a large map,
/// as a decision does (nx::agents::kSmallestBatch), so that a batch outweighs handing it to a worker.
constexpr std::size_t kSmallestWayBatch = nx::agents::kSmallestBatch;

/// The color of the arrows' heads of `agent`, whose last decision follows its corridor: stuck, whatever it
/// pursues, else of why it goes there.
[[nodiscard]] auto headColorOf(const nx::agents::Agent& agent) noexcept -> OverlayColor {
    if (agent.stuck.isStuck()) {
        return kStuckColor;
    }
    const Behavior behavior = agent.behavior;
    if (behavior == Behavior::Chase) {
        return kChaseColor;
    }
    return behavior == Behavior::Investigate ? kInvestigateColor : kRouteColor;
}

} // namespace

NavigationDrawing::NavigationDrawing(const nx::navigation::NavigationMesh& mesh, nx::HammerUnits height)
    : drawnMesh{&mesh}, lift{.z = height.value()} {
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
            edges.push_back(
                {.position = corners[corner] + lift, .color = edge, .brightness = kEdgeBrightness});
            edges.push_back({.position = corners[(corner + 1) % corners.size()] + lift,
                             .color = edge,
                             .brightness = kEdgeBrightness});
        }
    }
}

auto NavigationDrawing::mesh() const noexcept -> nx::render::OverlayMesh {
    return nx::render::OverlayMesh{.triangles = polygons, .lines = edges};
}

void NavigationDrawing::showWays(nx::jobs::JobSystem& jobs, nx::GameTime now,
                                 std::span<const ShownAgent> agents) {
    waysTime = now;
    const std::size_t count = agents.size();
    const std::size_t threads = std::size_t{jobs.workerCount()} + 1;
    const std::size_t batchCount = std::clamp(count / kSmallestWayBatch, std::size_t{1}, threads);
    const std::size_t batchSize = nx::divideRoundingUp(std::max(count, std::size_t{1}), batchCount);
    while (batches.size() < batchCount) {
        batches.push_back(WayBatch{.query = nx::navigation::NavigationQuery{*drawnMesh}});
    }
    // Batch `batch` finds its agents' ways with a query of its own, so that batches never share one.
    const auto findBatch = [this, agents, batchSize, count](std::size_t batch) {
        WayBatch& found = batches[batch];
        found.arrows.clear();
        for (std::size_t index = batch * batchSize; index < std::min(count, (batch + 1) * batchSize);
             ++index) {
            addWayOf(agents[index], found);
        }
    };
    if (batchCount == 1) {
        findBatch(0);
    } else {
        jobs.wait(jobs.parallelFor({.label = "navigation.drawWays", .priority = nx::jobs::Priority::Critical},
                                   std::views::iota(std::size_t{0}, batchCount), nx::jobs::Grain::fixed(1),
                                   findBatch));
    }
    arrows.clear();
    for (const WayBatch& batch : std::span{batches}.first(batchCount)) {
        arrows.insert(arrows.end(), batch.arrows.begin(), batch.arrows.end());
    }
}

void NavigationDrawing::addWayOf(const ShownAgent& shown, WayBatch& batch) const {
    batch.points.clear();
    shown.agent->appendWay(batch.query, batch.points);
    const OverlayColor head = headColorOf(*shown.agent);
    const OverlayColor shaft = shown.team ? kTeamColors[*shown.team] : kNoTeamColor;
    for (std::size_t point = 1; point < batch.points.size(); ++point) {
        batch.arrows.push_back(OverlayArrows{.start = batch.points[point - 1] + kArrowLift,
                                             .end = batch.points[point] + kArrowLift,
                                             .headColor = head,
                                             .shaftColor = shaft,
                                             .brightness = kArrowBrightness});
    }
}

auto NavigationDrawing::overlay() const noexcept -> SceneOverlay {
    return SceneOverlay{.isMeshShown = true, .arrows = arrows, .arrowStyle = kArrowStyle};
}

} // namespace cs
