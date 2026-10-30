// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The navigation's queries, which agents ask every tick (docs/adr/0023-navigation.md): a way across a
// map-sized floor among pillars, the nearest point, and a corridor's corners; the brains' decisions, one by
// one and in batches in jobs; and the build of its mesh.
// The data/cstrike/navigation.toml settings and the player's profile.

#include <benchmark/benchmark.h>

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.navigation;
import nx.agents;
import nx.ecs;

namespace {

void agentDecisions(benchmark::State& state);

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
                        .dropHeight = nx::HammerUnits{156.25F},
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

namespace {
/// Where the brains of agentDecisions stand, and the target they chase across the map.
constexpr nx::Vec3 kStart{.x = 64, .y = 64};
constexpr nx::Vec3 kGoal{.x = kFloorSide - kPillarSide, .y = kFloorSide - kPillarSide};

/// `count` brains that chase `target` at kGoal from kStart along their corridors on `mesh`; none when
/// the target cannot be reached.
[[nodiscard]] auto chasingBrains(const NavigationMesh& mesh, nx::ecs::Entity target, std::size_t count)
    -> std::vector<nx::agents::Brain> {
    NavigationQuery query{mesh};
    std::vector<nx::agents::Brain> brains;
    brains.reserve(count);
    const nx::agents::Rules rules{.memoryLifetime = nx::Seconds{6},
                                  .attackDistance = nx::HammerUnits{64},
                                  .arrivalDistance = nx::HammerUnits{8},
                                  .replanDistance = nx::HammerUnits{24},
                                  .memoryLimit = 32};
    const std::array seen{nx::agents::Observation{target, kGoal}};
    for (std::size_t index = 0; index < count; ++index) {
        brains.emplace_back(rules);
        brains.back().perceive(seen, {}, nx::kDefaultTickRate);
        if (brains.back().decide(kStart, query).behavior != nx::agents::Behavior::Chase) {
            return {};
        }
    }
    return brains;
}

/// The brains decide one by one on the calling thread, each tick.
void agentDecisions(benchmark::State& state) {
    auto mesh = pillarMesh();
    NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto target = world.spawn();
    const auto count = nx::narrow<std::size_t>(state.range(0));
    std::vector<nx::agents::Brain> brains = chasingBrains(mesh, target, count);
    if (brains.empty()) {
        state.SkipWithError("the benchmark's target must be reachable");
        return;
    }
    const std::array seen{nx::agents::Observation{target, kGoal}};
    for (auto iteration : state) {
        for (auto& brain : brains) {
            brain.perceive(seen, {}, nx::kDefaultTickRate);
            auto decision = brain.decide(kStart, query);
            benchmark::DoNotOptimize(decision);
        }
    }
    state.SetItemsProcessed(state.iterations() * nx::narrow<std::int64_t>(count));
}
BENCHMARK(agentDecisions)->Arg(32)->Arg(64)->Arg(256)->Arg(1024);

/// The same brains perceive one by one, then decide together in batches in jobs (nx::agents::Deciders),
/// as the bots and the horde do each tick.
void agentDecisionsInJobs(benchmark::State& state) {
    auto mesh = pillarMesh();
    nx::ecs::World world;
    const auto target = world.spawn();
    const auto count = nx::narrow<std::size_t>(state.range(0));
    std::vector<nx::agents::Brain> brains = chasingBrains(mesh, target, count);
    if (brains.empty()) {
        state.SkipWithError("the benchmark's target must be reachable");
        return;
    }
    auto jobs = nx::jobs::JobSystem::create();
    nx::invariant(jobs.has_value(), "navigationBench: a job system starts");
    nx::agents::Deciders deciders{mesh};
    const std::vector<nx::agents::DecisionRequest> requests =
        brains | std::views::transform([](nx::agents::Brain& brain) {
            return nx::agents::DecisionRequest{.brain = &brain, .feet = kStart};
        }) |
        std::ranges::to<std::vector>();
    std::vector<nx::agents::Decision> decisions(count);
    const std::array seen{nx::agents::Observation{target, kGoal}};
    for (auto iteration : state) {
        for (auto& brain : brains) {
            brain.perceive(seen, {}, nx::kDefaultTickRate);
        }
        deciders.decide(**jobs, requests, decisions);
        benchmark::DoNotOptimize(decisions.data());
    }
    state.SetItemsProcessed(state.iterations() * nx::narrow<std::int64_t>(count));
}
BENCHMARK(agentDecisionsInJobs)->Arg(32)->Arg(64)->Arg(256)->Arg(1024)->UseRealTime();
} // namespace

BENCHMARK(findPath);
BENCHMARK(nearestPoint);
BENCHMARK(corridorCorners);
BENCHMARK(buildMesh)->Unit(benchmark::kMillisecond);
