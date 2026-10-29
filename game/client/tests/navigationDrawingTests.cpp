// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// What nav_draw draws of a mesh: its polygons, see-through, and their edges, above it; and the way each
// agent follows on it while it moves along its corridor, as arrows above each stretch, their shafts in the
// color of its team and their heads in that of why it moves, found again for a tick, the same in batches in
// jobs as on one thread.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.ecs;
import nx.navigation;
import nx.agents;
import nx.render;
import cs.shared;
import cs.client;

#include "testAgents.hpp"
#include "testGeometry.hpp"

namespace {

using nx::Vec3;
using nx::render::OverlayArrows;
using nx::render::OverlayVertex;

constexpr nx::HammerUnits kLift{2.0F};
constexpr std::uint8_t kSeeThrough = 32;

[[nodiscard]] auto floorMesh() -> nx::navigation::NavigationMesh {
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = -512.0F, .y = -512.0F}, {.x = 512.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = -32.0F, .y = 64.0F, .z = 0.0F}, {.x = 32.0F, .y = 128.0F, .z = 128.0F});
    return nx::test::playerMesh(geometry, 0);
}

[[nodiscard]] auto brainRules() -> nx::agents::Rules {
    return {.memoryLifetime = nx::Seconds{2.0F},
            .attackDistance = nx::HammerUnits{40.0F},
            .arrivalDistance = nx::HammerUnits{4.0F},
            .replanDistance = nx::HammerUnits{16.0F},
            .memoryLimit = 2};
}

/// An agent moving to `goal` from `feet` on the mesh of `query`, around the box when it is in the way.
[[nodiscard]] auto movingAgent(nx::navigation::NavigationQuery& query, Vec3 feet, Vec3 goal)
    -> nx::agents::Agent {
    nx::agents::Agent agent{.brain = nx::agents::Brain{brainRules()}, .stuck = nx::test::stuckWatch()};
    agent.brain.setGoal(goal);
    agent.behavior = agent.brain.decide(feet, query).behavior;
    return agent;
}

TEST(NavigationDrawingTest, KeepsEachPolygonSeeThroughAndItsEdgesGlowingAboveTheMesh) {
    const nx::navigation::NavigationMesh mesh = floorMesh();
    const nx::navigation::MeshOutline outline = mesh.outline();
    const cs::NavigationDrawing drawing{mesh, kLift};
    const nx::render::OverlayMesh kept = drawing.mesh();

    std::size_t triangles = 0;
    for (const nx::navigation::MeshPolygon& polygon : outline.polygons) {
        triangles += polygon.cornerCount - 2;
    }
    EXPECT_EQ(kept.triangles.size(), triangles * 3);
    EXPECT_EQ(kept.lines.size(), outline.corners.size() * 2) << "an edge for each corner";
    for (const OverlayVertex& vertex : kept.triangles) {
        EXPECT_EQ(vertex.color.alpha, kSeeThrough);
        EXPECT_EQ(vertex.brightness, 1.0F) << "see-through, no glow";
    }
    const auto lowest = std::ranges::min(outline.corners, {}, &Vec3::z).z;
    for (const OverlayVertex& vertex : kept.lines) {
        EXPECT_GE(vertex.position.z, lowest + kLift.value());
        EXPECT_EQ(vertex.color.alpha, 255);
        EXPECT_GT(vertex.brightness, 1.0F) << "a glow in the enhanced look";
    }
    EXPECT_TRUE(drawing.overlay().isMeshShown);
    EXPECT_TRUE(drawing.overlay().arrows.empty()) << "no way shown yet";
}

TEST(NavigationDrawingTest, ShowsTheWayOfAnAgentMovingAlongItsCorridorAsArrowsAboveEachStretch) {
    const nx::navigation::NavigationMesh mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    const auto jobs = nx::test::jobSystem(0);
    cs::NavigationDrawing drawing{mesh, kLift};

    const nx::agents::Agent moving = movingAgent(query, {.y = -100.0F}, {.y = 300.0F});
    ASSERT_EQ(moving.behavior, nx::agents::Behavior::MoveTo);
    std::vector<Vec3> way;
    moving.appendWay(query, way);
    ASSERT_GE(way.size(), 3U) << "around the box";
    const nx::agents::Agent idle{.brain = nx::agents::Brain{brainRules()}, .stuck = nx::test::stuckWatch()};
    const std::array agents{cs::ShownAgent{.agent = &moving, .team = cs::Team::CounterTerrorists},
                            cs::ShownAgent{.agent = &idle, .team = cs::Team::Terrorists}};
    const nx::GameTime now = nx::GameTime{}.after(nx::Seconds{1.0F}, nx::kDefaultTickRate);
    EXPECT_FALSE(drawing.showsWaysOf(now));

    drawing.showWays(*jobs, now, agents);
    EXPECT_TRUE(drawing.showsWaysOf(now));
    const std::span<const OverlayArrows> arrows = drawing.overlay().arrows;
    // A stretch between each two points of the way; nothing of the agent standing still.
    ASSERT_EQ(arrows.size(), way.size() - 1);
    for (std::size_t stretch = 0; stretch < arrows.size(); ++stretch) {
        EXPECT_EQ(arrows[stretch].start.x, way[stretch].x);
        EXPECT_EQ(arrows[stretch].end.y, way[stretch + 1].y);
        EXPECT_GT(arrows[stretch].start.z, way[stretch].z + kLift.value()) << "above the mesh's edges";
        EXPECT_GT(arrows[stretch].brightness, 1.0F);
    }
}

TEST(NavigationDrawingTest, ColorsTheShaftsByTeamAndTheHeadsByWhyTheAgentGoes) {
    const nx::navigation::NavigationMesh mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    const auto jobs = nx::test::jobSystem(0);
    nx::ecs::World world;
    nx::agents::Agent chasing{.brain = nx::agents::Brain{brainRules()}, .stuck = nx::test::stuckWatch()};
    chasing.brain.perceive(
        std::array{nx::agents::Observation{.entity = world.spawn(), .position = {.x = 200.0F, .y = -100.0F}}},
        {}, nx::kDefaultTickRate);
    chasing.behavior = chasing.brain.decide({.y = -100.0F}, query).behavior;
    ASSERT_EQ(chasing.behavior, nx::agents::Behavior::Chase);
    const nx::agents::Agent routing = movingAgent(query, {.y = -100.0F}, {.y = 300.0F});
    ASSERT_EQ(routing.behavior, nx::agents::Behavior::MoveTo);

    const auto firstArrowsOf = [&](const nx::agents::Agent& agent, std::optional<cs::Team> team) {
        cs::NavigationDrawing drawing{mesh, kLift};
        drawing.showWays(*jobs, nx::GameTime{}, std::array{cs::ShownAgent{.agent = &agent, .team = team}});
        EXPECT_FALSE(drawing.overlay().arrows.empty());
        return drawing.overlay().arrows.empty() ? OverlayArrows{} : drawing.overlay().arrows.front();
    };
    const OverlayArrows counterTerrorist = firstArrowsOf(routing, cs::Team::CounterTerrorists);
    const OverlayArrows terrorist = firstArrowsOf(routing, cs::Team::Terrorists);
    const OverlayArrows noTeam = firstArrowsOf(routing, std::nullopt);
    const OverlayArrows chaser = firstArrowsOf(chasing, cs::Team::Terrorists);

    EXPECT_GT(counterTerrorist.shaftColor.blue, counterTerrorist.shaftColor.red)
        << "a counter-terrorist's blue";
    EXPECT_GT(terrorist.shaftColor.red, terrorist.shaftColor.blue) << "a terrorist's red";
    EXPECT_NE(noTeam.shaftColor, counterTerrorist.shaftColor);
    EXPECT_NE(noTeam.shaftColor, terrorist.shaftColor);
    EXPECT_EQ(counterTerrorist.headColor, terrorist.headColor) << "the same reason, its route";
    EXPECT_EQ(chaser.shaftColor, terrorist.shaftColor) << "the same team";
    EXPECT_NE(chaser.headColor, terrorist.headColor) << "chasing, not on its route";
    EXPECT_NE(chaser.headColor, chaser.shaftColor);
}

TEST(NavigationDrawingTest, ColorsTheHeadsApartWhileTheAgentIsStuckOnItsWay) {
    const nx::navigation::NavigationMesh mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    const auto jobs = nx::test::jobSystem(0);
    const auto firstArrowsOf = [&](const nx::agents::Agent& agent) {
        cs::NavigationDrawing drawing{mesh, kLift};
        drawing.showWays(*jobs, nx::GameTime{},
                         std::array{cs::ShownAgent{.agent = &agent, .team = cs::Team::Terrorists}});
        EXPECT_FALSE(drawing.overlay().arrows.empty());
        return drawing.overlay().arrows.empty() ? OverlayArrows{} : drawing.overlay().arrows.front();
    };
    constexpr nx::Vec3 kFeet{.y = -100.0F};
    nx::agents::Agent agent = movingAgent(query, kFeet, {.y = 300.0F});
    const OverlayArrows routing = firstArrowsOf(agent);
    // Its feet stay where they are as it means to run along its way.
    for (int tick = 0; tick < 100 && !agent.stuck.isStuck(); ++tick) {
        nx::agents::Intent intent{.direction = {.y = 1.0F}, .ahead = {.y = 1.0F}};
        std::ignore = agent.stuck.watch(intent, kFeet, nx::UnitsPerSecond{250.0F}, true, nx::Seconds{0.01F});
    }
    ASSERT_TRUE(agent.stuck.isStuck());
    const OverlayArrows stuck = firstArrowsOf(agent);
    EXPECT_NE(stuck.headColor, routing.headColor) << "stuck, not on its route";
    EXPECT_NE(stuck.headColor, stuck.shaftColor);
    EXPECT_EQ(stuck.shaftColor, routing.shaftColor) << "the same team";
}

TEST(NavigationDrawingTest, FindsTheSameWaysInBatchesInJobsAsOnOneThread) {
    constexpr int kAgents = 64;
    const nx::navigation::NavigationMesh mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    std::vector<nx::agents::Agent> agents;
    agents.reserve(kAgents);
    for (int index = 0; index < kAgents; ++index) {
        const float across = static_cast<float>(index - (kAgents / 2)) * 8.0F;
        agents.push_back(movingAgent(query, {.x = across, .y = -200.0F}, {.x = -across, .y = 300.0F}));
    }
    std::vector<cs::ShownAgent> shown;
    for (const nx::agents::Agent& agent : agents) {
        shown.push_back(cs::ShownAgent{.agent = &agent, .team = cs::Team::Terrorists});
    }
    const auto arrowsWith = [&](std::uint32_t workers) {
        const auto jobs = nx::test::jobSystem(workers);
        cs::NavigationDrawing drawing{mesh, kLift};
        drawing.showWays(*jobs, nx::GameTime{}, shown);
        return drawing.overlay().arrows | std::views::transform([](const OverlayArrows& stretch) {
                   return std::array{stretch.start.x, stretch.start.y, stretch.end.x, stretch.end.y};
               }) |
               std::ranges::to<std::vector>();
    };

    const auto alone = arrowsWith(0);
    ASSERT_GE(alone.size(), static_cast<std::size_t>(kAgents));
    EXPECT_EQ(arrowsWith(3), alone);
    EXPECT_EQ(arrowsWith(15), alone);
}

} // namespace
