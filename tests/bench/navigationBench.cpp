// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The navigation's queries, which agents ask every tick (docs/adr/0023-navigation.md): a way across a
// map-sized floor among pillars, the nearest point, and a corridor's corners; and the build of its mesh.
// The data/cstrike/navigation.toml settings and the player's profile.

#include <benchmark/benchmark.h>

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.navigation;

namespace {

using nx::Vec3;
using nx::navigation::AgentProfile;
using nx::navigation::BuildSettings;
using nx::navigation::Geometry;
using nx::navigation::NavigationMesh;
using nx::navigation::NavigationQuery;

/// A floor of 4096 units a side with a pillar of 64 every 256, 15 across and 15 down, as large as a big
/// map's ground.
constexpr float kFloorSide = 4096.0F;
constexpr float kPillarSpacing = 256.0F;
constexpr int kPillarsAcross = 15;
constexpr float kPillarSide = 64.0F;
constexpr float kPillarHeight = 128.0F;
/// The ways asked, from one corner's side to the other's, in turn.
constexpr std::size_t kWays = 64;

[[nodiscard]] auto playerProfile() -> AgentProfile {
    return AgentProfile{.name = "player",
                        .radius = nx::HammerUnits{16.0F},
                        .standingHeight = nx::HammerUnits{72.0F},
                        .crouchedHeight = nx::HammerUnits{36.0F},
                        .stepHeight = nx::HammerUnits{18.0F},
                        .jumpHeight = nx::HammerUnits{41.8F},
                        .minGroundNormalZ = 0.7F,
                        .areaCosts = {}};
}

[[nodiscard]] auto settings() -> BuildSettings {
    return BuildSettings{.cellSize = nx::HammerUnits{4.0F},
                         .cellHeight = nx::HammerUnits{2.0F},
                         .tileCells = 64,
                         .smallestIsland = nx::HammerUnits{32.0F},
                         .smallestRegion = nx::HammerUnits{80.0F},
                         .longestEdge = nx::HammerUnits{96.0F},
                         .edgeError = nx::HammerUnits{5.0F},
                         .detailSampleDistance = nx::HammerUnits{24.0F},
                         .detailHeightError = nx::HammerUnits{2.0F}};
}

void addQuad(Geometry& geometry, std::array<Vec3, 4> corners) {
    nx::navigation::addConvexPolygon(geometry, corners, nx::navigation::AreaKind::Floor);
}

[[nodiscard]] auto pillars() -> Geometry {
    Geometry geometry;
    addQuad(geometry,
            {Vec3{}, Vec3{.x = kFloorSide}, Vec3{.x = kFloorSide, .y = kFloorSide}, Vec3{.y = kFloorSide}});
    for (int column = 1; column <= kPillarsAcross; ++column) {
        for (int row = 1; row <= kPillarsAcross; ++row) {
            const float x = kPillarSpacing * static_cast<float>(column);
            const float y = kPillarSpacing * static_cast<float>(row);
            const float right = x + kPillarSide;
            const float back = y + kPillarSide;
            addQuad(geometry,
                    {Vec3{.x = x, .y = y, .z = kPillarHeight}, Vec3{.x = right, .y = y, .z = kPillarHeight},
                     Vec3{.x = right, .y = back, .z = kPillarHeight},
                     Vec3{.x = x, .y = back, .z = kPillarHeight}});
            addQuad(geometry,
                    {Vec3{.x = x, .y = y}, Vec3{.x = right, .y = y},
                     Vec3{.x = right, .y = y, .z = kPillarHeight}, Vec3{.x = x, .y = y, .z = kPillarHeight}});
            addQuad(geometry, {Vec3{.x = right, .y = y}, Vec3{.x = right, .y = back},
                               Vec3{.x = right, .y = back, .z = kPillarHeight},
                               Vec3{.x = right, .y = y, .z = kPillarHeight}});
            addQuad(geometry, {Vec3{.x = right, .y = back}, Vec3{.x = x, .y = back},
                               Vec3{.x = x, .y = back, .z = kPillarHeight},
                               Vec3{.x = right, .y = back, .z = kPillarHeight}});
            addQuad(geometry,
                    {Vec3{.x = x, .y = back}, Vec3{.x = x, .y = y}, Vec3{.x = x, .y = y, .z = kPillarHeight},
                     Vec3{.x = x, .y = back, .z = kPillarHeight}});
        }
    }
    return geometry;
}

/// The mesh of the pillars, which each benchmark builds before it measures.
[[nodiscard]] auto pillarMesh() -> NavigationMesh {
    auto jobs = nx::jobs::JobSystem::create();
    nx::invariant(jobs.has_value(), "navigationBench: a job system starts");
    auto mesh = NavigationMesh::build(**jobs, pillars(), playerProfile(), settings());
    nx::invariant(mesh.has_value(), "navigationBench: the pillars' mesh is built");
    return std::move(mesh).value();
}

/// Way `index`: from the near side's edge to the far side's, each at its own place along it.
[[nodiscard]] auto wayOf(std::size_t index) -> nx::navigation::PathRequest {
    const float along = (kFloorSide / static_cast<float>(kWays)) * (static_cast<float>(index) + 0.5F);
    return {.from = {.x = 100.0F, .y = along}, .to = {.x = kFloorSide - 100.0F, .y = kFloorSide - along}};
}

/// A way across the map: A* over its polygons, then the funnel.
void findPath(benchmark::State& state) {
    const NavigationMesh mesh = pillarMesh();
    NavigationQuery query{mesh};
    std::size_t way = 0;
    for ([[maybe_unused]] auto iteration : state) {
        auto path = query.findPath(wayOf(way++ % kWays));
        benchmark::DoNotOptimize(path);
    }
}

/// The point of the mesh nearest an agent.
void nearestPoint(benchmark::State& state) {
    const NavigationMesh mesh = pillarMesh();
    NavigationQuery query{mesh};
    std::size_t way = 0;
    for ([[maybe_unused]] auto iteration : state) {
        auto point = query.nearestPoint(wayOf(way++ % kWays).from);
        benchmark::DoNotOptimize(point);
    }
}

/// The next corners of an agent's corridor, as it steers each tick.
void corridorCorners(benchmark::State& state) {
    const NavigationMesh mesh = pillarMesh();
    NavigationQuery query{mesh};
    auto corridor = nx::navigation::Corridor::plan(query, wayOf(0));
    nx::invariant(corridor.has_value(), "navigationBench: the first way has a corridor");
    for ([[maybe_unused]] auto iteration : state) {
        auto corners = corridor->corners(query, 4);
        benchmark::DoNotOptimize(corners);
    }
}

/// The mesh of the pillars, built in jobs.
void buildMesh(benchmark::State& state) {
    auto jobs = nx::jobs::JobSystem::create();
    nx::invariant(jobs.has_value(), "navigationBench: a job system starts");
    const Geometry geometry = pillars();
    for ([[maybe_unused]] auto iteration : state) {
        auto mesh = NavigationMesh::build(**jobs, geometry, playerProfile(), settings());
        benchmark::DoNotOptimize(mesh);
    }
}

} // namespace

BENCHMARK(findPath);
BENCHMARK(nearestPoint);
BENCHMARK(corridorCorners);
BENCHMARK(buildMesh)->Unit(benchmark::kMillisecond);
