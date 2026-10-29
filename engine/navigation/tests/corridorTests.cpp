// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// A corridor that follows an agent around a wall as it moves, and its target as that moves, without a
// search again; and the way left along it.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.navigation;

#include "testGeometry.hpp"

namespace {

using nx::Vec3;
using nx::navigation::Corridor;
using nx::navigation::Geometry;
using nx::navigation::NavigationError;
using nx::navigation::NavigationMesh;
using nx::navigation::NavigationQuery;
using nx::navigation::Path;
using nx::navigation::PathRequest;

constexpr float kTolerance = 8.0F;
/// How far the agent moves in a step: a player's run in a tick of 100 a second, about.
constexpr float kStep = 3.0F;

[[nodiscard]] auto wallAcrossAFloor() -> Geometry {
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 480.0F, .y = -32.0F, .z = 0.0F},
                     {.x = 544.0F, .y = 384.0F, .z = 128.0F});
    return geometry;
}

TEST(CorridorTest, AnAgentSteeringToItsCornersReachesItsTarget) {
    const NavigationMesh mesh = nx::test::playerMesh(wallAcrossAFloor());
    NavigationQuery query{mesh};
    const Vec3 to{.x = 924.0F, .y = 100.0F};
    auto corridor = Corridor::plan(query, {.from = {.x = 100.0F, .y = 100.0F}, .to = to});
    ASSERT_TRUE(corridor.has_value()) << nx::describe(corridor.error());
    EXPECT_LT(nx::test::distanceAcross(corridor->target(), to), kTolerance);
    const std::size_t polygonsAtStart = corridor->polygonCount();
    ASSERT_GT(polygonsAtStart, 1U);

    // The first corner is where the way turns around the wall's end, not the target.
    const std::vector<Vec3> ahead = corridor->corners(query, 2);
    ASSERT_EQ(ahead.size(), 2U);
    EXPECT_GT(ahead.front().y, 384.0F);
    EXPECT_LT(ahead.front().x, 480.0F);

    // Steering a step at a time to the next corner, the agent never passes through the wall.
    for (int step = 0; step < 1000 && nx::test::distanceAcross(corridor->position(), to) > kStep; ++step) {
        const std::vector<Vec3> next = corridor->corners(query, 1);
        ASSERT_EQ(next.size(), 1U);
        const Vec3 here = corridor->position();
        const Vec3 way = next.front() - here;
        const float left = nx::test::distanceAcross(next.front(), here);
        const Vec3 moved = corridor->moveAgent(
            query, left <= kStep ? next.front() : here + (Vec3{.x = way.x, .y = way.y} * (kStep / left)));
        EXPECT_FALSE(moved.x > 480.0F && moved.x < 544.0F && moved.y < 384.0F) << std::format("{}", moved);
    }
    EXPECT_LT(nx::test::distanceAcross(corridor->position(), to), kTolerance);
    EXPECT_LT(corridor->polygonCount(), polygonsAtStart);
}

TEST(CorridorTest, AMoveIntoTheWallStopsAtItsEdge) {
    const NavigationMesh mesh = nx::test::playerMesh(wallAcrossAFloor());
    NavigationQuery query{mesh};
    auto corridor =
        Corridor::plan(query, {.from = {.x = 400.0F, .y = 100.0F}, .to = {.x = 924.0F, .y = 100.0F}});
    ASSERT_TRUE(corridor.has_value());
    const Vec3 moved = corridor->moveAgent(query, {.x = 520.0F, .y = 100.0F});
    EXPECT_NEAR(moved.x, 464.0F, kTolerance); // the wall less the radius
}

TEST(CorridorTest, TheTargetMovesAlongTheMesh) {
    const NavigationMesh mesh = nx::test::playerMesh(wallAcrossAFloor());
    NavigationQuery query{mesh};
    auto corridor =
        Corridor::plan(query, {.from = {.x = 100.0F, .y = 100.0F}, .to = {.x = 924.0F, .y = 100.0F}});
    ASSERT_TRUE(corridor.has_value());
    const Vec3 target = corridor->moveTarget(query, {.x = 924.0F, .y = 300.0F});
    EXPECT_LT(nx::test::distanceAcross(target, {.x = 924.0F, .y = 300.0F}), kTolerance);
    EXPECT_LT(nx::test::distanceAcross(corridor->target(), target), 0.01F);
}

TEST(CorridorTest, AChangedTargetAndItsLastCornerStayOnTheFloor) {
    constexpr float airborneHeight = 32.0F;
    const NavigationMesh mesh = nx::test::playerMesh(wallAcrossAFloor());
    NavigationQuery query{mesh};
    auto corridor =
        Corridor::plan(query, {.from = {.x = 100.0F, .y = 100.0F}, .to = {.x = 924.0F, .y = 100.0F}});
    ASSERT_TRUE(corridor.has_value());
    const float floorHeight = corridor->target().z;
    const Vec3 moved =
        corridor->moveTarget(query, {.x = 924.0F, .y = 300.0F, .z = floorHeight + airborneHeight});
    EXPECT_FLOAT_EQ(moved.z, floorHeight);
    const auto corners = corridor->corners(query, 2);
    ASSERT_FALSE(corners.empty());
    EXPECT_FLOAT_EQ(corners.back().z, floorHeight);
}

TEST(CorridorTest, ATargetPushedAgainstAWallStaysOnItsBoundaryAndFloor) {
    constexpr Vec3 kFrom{.x = 100.0F, .y = 100.0F};
    constexpr Vec3 kInitialTarget{.x = 400.0F, .y = 100.0F};
    constexpr Vec3 kBlockedTarget{.x = 520.0F, .y = 100.0F};
    constexpr float kAirborneHeight = 32.0F;
    constexpr float kBoundaryX = 464.0F;
    const NavigationMesh mesh = nx::test::playerMesh(wallAcrossAFloor());
    NavigationQuery query{mesh};
    auto corridor = Corridor::plan(query, {.from = kFrom, .to = kInitialTarget});
    ASSERT_TRUE(corridor.has_value());
    const float floorHeight = corridor->target().z;
    const Vec3 moved = corridor->moveTarget(query, kBlockedTarget + Vec3{.z = floorHeight + kAirborneHeight});
    EXPECT_NEAR(moved.x, kBoundaryX, kTolerance);
    EXPECT_FLOAT_EQ(moved.z, floorHeight);
    const auto corners = corridor->corners(query, 2);
    ASSERT_FALSE(corners.empty());
    EXPECT_EQ(corners.back(), moved);
}

TEST(CorridorTest, ItsWayIsTheWayFoundUntilTheAgentMovesThenStartsWhereItIs) {
    const NavigationMesh mesh = nx::test::playerMesh(wallAcrossAFloor());
    NavigationQuery query{mesh};
    const PathRequest request{.from = {.x = 100.0F, .y = 100.0F}, .to = {.x = 924.0F, .y = 100.0F}};
    auto corridor = Corridor::plan(query, request);
    ASSERT_TRUE(corridor.has_value());
    const auto found = query.findPath(request);
    ASSERT_TRUE(found.has_value());
    const Path planned = corridor->way(query);
    EXPECT_EQ(planned.points, found->points);
    ASSERT_GE(planned.points.size(), 3U) << "around the wall's end";
    EXPECT_EQ(planned.points.front(), corridor->position());
    EXPECT_EQ(planned.points.back(), corridor->target());

    const Vec3 moved = corridor->moveAgent(query, {.x = 100.0F, .y = 200.0F});
    const Path left = corridor->way(query);
    ASSERT_FALSE(left.points.empty());
    EXPECT_EQ(left.points.front(), moved);
    EXPECT_EQ(left.points.back(), corridor->target());
}

TEST(CorridorTest, NoCorridorJoinsAPointOffTheMesh) {
    const NavigationMesh mesh = nx::test::playerMesh(wallAcrossAFloor());
    NavigationQuery query{mesh};
    const auto corridor =
        Corridor::plan(query, {.from = {.x = 100.0F, .y = 100.0F}, .to = {.x = 100.0F, .y = 900.0F}});
    ASSERT_FALSE(corridor.has_value());
    EXPECT_EQ(corridor.error().code, NavigationError::OutsideMesh);
}

} // namespace
