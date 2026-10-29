// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// What nav_draw draws of a mesh: its polygons, see-through, and their edges, above it; and the way an
// agent follows on it while it moves along its corridor, in the color of why it moves, a mark at its end.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.ecs;
import nx.navigation;
import nx.agents;
import nx.render;
import cs.client;

#include "testGeometry.hpp"

namespace {

using nx::Vec3;
using nx::render::OverlayVertex;

constexpr nx::HammerUnits kLift{2.0F};
constexpr std::uint8_t kSeeThrough = 32;

[[nodiscard]] auto floorMesh() -> nx::navigation::NavigationMesh {
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = -512.0F, .y = -512.0F}, {.x = 512.0F, .y = 512.0F}, 0.0F);
    return nx::test::playerMesh(geometry, 0);
}

[[nodiscard]] auto brainRules() -> nx::agents::Rules {
    return {.memoryLifetime = nx::Seconds{2.0F},
            .attackDistance = nx::HammerUnits{40.0F},
            .arrivalDistance = nx::HammerUnits{4.0F},
            .replanDistance = nx::HammerUnits{16.0F},
            .memoryLimit = 2};
}

[[nodiscard]] auto isWhite(nx::render::OverlayColor color) -> bool {
    return color.red == 255 && color.green == 255 && color.blue == 255 && color.alpha == 255;
}

TEST(NavigationDrawingTest, DrawsEachPolygonSeeThroughAndItsEdgesAboveTheMesh) {
    const nx::navigation::NavigationMesh mesh = floorMesh();
    const nx::navigation::MeshOutline outline = mesh.outline();
    const cs::NavigationDrawing drawing{mesh, kLift};
    const cs::SceneOverlay overlay = drawing.overlay();

    std::size_t triangles = 0;
    for (const nx::navigation::MeshPolygon& polygon : outline.polygons) {
        triangles += polygon.cornerCount - 2;
    }
    EXPECT_EQ(overlay.triangles.size(), triangles * 3);
    EXPECT_EQ(overlay.lines.size(), outline.corners.size() * 2) << "an edge for each corner";
    for (const OverlayVertex& vertex : overlay.triangles) {
        EXPECT_EQ(vertex.color.alpha, kSeeThrough);
        EXPECT_GT(vertex.color.green, vertex.color.red) << "the floor's green";
    }
    const auto lowest = std::ranges::min(outline.corners, {}, &Vec3::z).z;
    for (const OverlayVertex& vertex : overlay.lines) {
        EXPECT_GE(vertex.position.z, lowest + kLift.value());
        EXPECT_EQ(vertex.color.alpha, 255);
        EXPECT_GT(vertex.brightness, 1.0F) << "a glow in the enhanced look";
    }
    for (const OverlayVertex& vertex : overlay.triangles) {
        EXPECT_EQ(vertex.brightness, 1.0F) << "see-through, no glow";
    }
}

TEST(NavigationDrawingTest, DrawsTheWayOfAnAgentMovingAlongItsCorridorWithAMarkAtItsEnd) {
    const nx::navigation::NavigationMesh mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    cs::NavigationDrawing drawing{mesh, kLift};
    const std::size_t edges = drawing.overlay().lines.size();

    nx::agents::Agent moving{.brain = nx::agents::Brain{brainRules()}};
    const Vec3 goal{.y = 200.0F};
    moving.brain.setGoal(goal);
    moving.behavior = moving.brain.decide({}, query).behavior;
    ASSERT_EQ(moving.behavior, nx::agents::Behavior::MoveTo);
    const std::vector<Vec3> way = moving.way(query);
    ASSERT_GE(way.size(), 2U);
    const nx::agents::Agent idle{.brain = nx::agents::Brain{brainRules()}};

    drawing.showWayOf(moving);
    drawing.showWayOf(idle);
    const std::span<const OverlayVertex> lines = drawing.overlay().lines.subspan(edges);
    // A line between each two points of the way, then the mark; nothing of the agent standing still.
    ASSERT_EQ(lines.size(), ((way.size() - 1) * 2) + 2);
    EXPECT_EQ(lines.front().position, way.front() + Vec3{.z = kLift.value()});
    const OverlayVertex& markFoot = lines[lines.size() - 2];
    const OverlayVertex& markTop = lines.back();
    EXPECT_EQ(markFoot.position, way.back() + Vec3{.z = kLift.value()});
    EXPECT_EQ(markTop.position.x, markFoot.position.x);
    EXPECT_EQ(markTop.position.y, markFoot.position.y);
    EXPECT_GT(markTop.position.z, markFoot.position.z);
    EXPECT_TRUE(std::ranges::all_of(lines, [](const OverlayVertex& vertex) { return isWhite(vertex.color); }))
        << "the color of a move to its goal";
    EXPECT_GT(lines.front().brightness, drawing.overlay().lines.front().brightness)
        << "brighter than the mesh's edges";

    drawing.clearWays();
    EXPECT_EQ(drawing.overlay().lines.size(), edges) << "the mesh's edges are left";
}

TEST(NavigationDrawingTest, ColorsAWayRedWhileItsAgentChasesATargetItSees) {
    const nx::navigation::NavigationMesh mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    cs::NavigationDrawing drawing{mesh, kLift};
    const std::size_t edges = drawing.overlay().lines.size();
    nx::ecs::World world;
    nx::agents::Agent chasing{.brain = nx::agents::Brain{brainRules()}};
    chasing.brain.perceive(
        std::array{nx::agents::Observation{.entity = world.spawn(), .position = {.x = 200.0F}}}, {},
        nx::kDefaultTickRate);
    chasing.behavior = chasing.brain.decide({}, query).behavior;
    ASSERT_EQ(chasing.behavior, nx::agents::Behavior::Chase);

    drawing.showWayOf(chasing);
    const std::span<const OverlayVertex> lines = drawing.overlay().lines.subspan(edges);
    ASSERT_FALSE(lines.empty());
    for (const OverlayVertex& vertex : lines) {
        EXPECT_GT(vertex.color.red, vertex.color.green);
        EXPECT_GT(vertex.color.red, vertex.color.blue);
    }
}

} // namespace
