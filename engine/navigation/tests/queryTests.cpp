// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Queries of meshes built from geometry made here: ways around obstacles, up steps and not up walls,
// chosen by what areas cost; points off the mesh and unreachable ones; straight walks, random points
// and reachability; the same answers for the same inputs.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.navigation;

#include "testGeometry.hpp"

namespace {

TEST(NavigationQueryValidation, RejectsNonfiniteCoordinatesInvalidCostsAndNegativeRadius) {
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = -256, .y = -256}, {.x = 256, .y = 256}, 0);
    auto mesh = nx::test::playerMesh(geometry, 0);
    nx::navigation::NavigationQuery query{mesh};
    const nx::Vec3 invalid{.x = std::numeric_limits<float>::quiet_NaN()};
    EXPECT_EQ(query.nearestPoint(invalid).error().code, nx::navigation::NavigationError::InvalidQuery);
    EXPECT_EQ(query.raycast({}, invalid).error().code, nx::navigation::NavigationError::InvalidQuery);
    nx::navigation::AreaCosts costs{1.0F};
    costs[nx::navigation::AreaKind::Floor] = 0.0F;
    const nx::navigation::PathRequest request{.from = {}, .to = {.x = 128}, .costs = costs};
    EXPECT_EQ(query.findPath(request).error().code, nx::navigation::NavigationError::InvalidQuery);
    EXPECT_EQ(nx::navigation::Corridor::plan(query, request).error().code,
              nx::navigation::NavigationError::InvalidQuery);
    nx::Random random{1, "query validation"};
    EXPECT_EQ(query.randomPoint({}, nx::HammerUnits{-1}, random).error().code,
              nx::navigation::NavigationError::InvalidQuery);
}

using nx::Vec3;
using nx::navigation::AreaKind;
using nx::navigation::Geometry;
using nx::navigation::NavigationError;
using nx::navigation::NavigationMesh;
using nx::navigation::NavigationQuery;
using nx::navigation::Path;
using nx::navigation::PathRequest;

/// How far a point of a way may be from where it was asked for: a cell and the floor's detail.
constexpr float kTolerance = 8.0F;
/// The player's radius, and the cell an edge of the mesh may stray by.
constexpr float kRadius = 16.0F;
constexpr float kCell = 4.0F;

/// A floor 1024 by 512 with a wall across it from y = 0 to 384, at x = 480 to 544: the way from one
/// side to the other goes around its end.
[[nodiscard]] auto wallAcrossAFloor() -> Geometry {
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 480.0F, .y = -32.0F, .z = 0.0F},
                     {.x = 544.0F, .y = 384.0F, .z = 128.0F});
    return geometry;
}

TEST(NavigationQueryTest, AWayGoesAroundAWall) {
    const NavigationMesh mesh = nx::test::playerMesh(wallAcrossAFloor());
    NavigationQuery query{mesh};
    const Vec3 from{.x = 100.0F, .y = 100.0F, .z = 36.0F};
    const Vec3 to{.x = 924.0F, .y = 100.0F, .z = 36.0F};
    const auto path = query.findPath({.from = from, .to = to});
    ASSERT_TRUE(path.has_value()) << nx::describe(path.error());
    ASSERT_GE(path->points.size(), 3U);
    EXPECT_LT(nx::test::distanceAcross(path->points.front(), from), kTolerance);
    EXPECT_LT(nx::test::distanceAcross(path->points.back(), to), kTolerance);
    EXPECT_NEAR(path->points.front().z, 0.0F, kTolerance);
    // Around the wall's end, as far from it as the radius, never through it.
    EXPECT_TRUE(
        std::ranges::any_of(path->points, [](Vec3 point) { return point.y >= 384.0F + kRadius - 4.0F; }));
    for (const Vec3 point : path->points) {
        EXPECT_FALSE(point.x > 480.0F && point.x < 544.0F && point.y < 384.0F) << std::format("{}", point);
    }
    EXPECT_GT(nx::test::lengthOf(path->points), 1000.0F); // 824 straight through the wall
}

/// The least distance across from the corners of `points` to `corner`.
[[nodiscard]] auto leastDistanceTo(std::span<const Vec3> points, Vec3 corner) -> float {
    float least = std::numeric_limits<float>::max();
    for (const Vec3 point : points) {
        least = std::min(least, nx::test::distanceAcross(point, corner));
    }
    return least;
}

TEST(NavigationQueryTest, AWayKeepsTheWallClearanceFromAWallsEndWhereThereIsRoom) {
    constexpr float kClearance = 16.0F;
    const Vec3 from{.x = 100.0F, .y = 100.0F};
    const Vec3 to{.x = 924.0F, .y = 100.0F};
    const Vec3 wallsEnd{.x = 480.0F, .y = 384.0F};
    const NavigationMesh hugging = nx::test::playerMesh(wallAcrossAFloor());
    NavigationQuery huggingQuery{hugging};
    const auto tight = huggingQuery.findPath({.from = from, .to = to});
    ASSERT_TRUE(tight.has_value()) << nx::describe(tight.error());
    EXPECT_LT(leastDistanceTo(tight->points, wallsEnd), kRadius + kCell) << "the shortest way, at its radius";

    const NavigationMesh cleared =
        nx::test::playerMesh(wallAcrossAFloor(), 3, nx::test::clearedPlayerProfile());
    NavigationQuery clearedQuery{cleared};
    const auto kept = clearedQuery.findPath({.from = from, .to = to});
    ASSERT_TRUE(kept.has_value()) << nx::describe(kept.error());
    EXPECT_GE(leastDistanceTo(kept->points, wallsEnd), kRadius + kClearance - kCell)
        << "128 units between the wall's end and the floor's side leave room for its clearance";
}

TEST(NavigationQueryTest, AWayStillPassesWhereThereIsRoomForTheRadiusAlone) {
    // Two walls leave a gap of 48 units at y = 232 to 280: room for a player, not for its clearance.
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 480.0F, .y = -32.0F, .z = 0.0F},
                     {.x = 544.0F, .y = 232.0F, .z = 128.0F});
    nx::test::addBox(geometry, {.x = 480.0F, .y = 280.0F, .z = 0.0F},
                     {.x = 544.0F, .y = 544.0F, .z = 128.0F});
    const NavigationMesh mesh = nx::test::playerMesh(geometry, 3, nx::test::clearedPlayerProfile());
    NavigationQuery query{mesh};
    const auto way = query.findPath({.from = {.x = 100.0F, .y = 100.0F}, .to = {.x = 924.0F, .y = 100.0F}});
    ASSERT_TRUE(way.has_value()) << nx::describe(way.error());
    EXPECT_TRUE(std::ranges::any_of(way->points, [](Vec3 point) {
        return point.y > 232.0F && point.y < 280.0F;
    })) << "through the gap";
}

TEST(NavigationQueryTest, TheSameRequestGivesTheSameWay) {
    const Geometry geometry = wallAcrossAFloor();
    const NavigationMesh mesh = nx::test::playerMesh(geometry, 0);
    const NavigationMesh rebuilt = nx::test::playerMesh(geometry, 3);
    NavigationQuery first{mesh};
    NavigationQuery second{mesh};
    NavigationQuery onRebuilt{rebuilt};
    const PathRequest request{.from = {.x = 100.0F, .y = 100.0F}, .to = {.x = 924.0F, .y = 450.0F}};
    const auto path = first.findPath(request);
    ASSERT_TRUE(path.has_value());
    for (NavigationQuery* const query : {&first, &second, &onRebuilt}) {
        const auto again = query->findPath(request);
        ASSERT_TRUE(again.has_value());
        EXPECT_EQ(again->points, path->points);
    }
}

TEST(NavigationQueryTest, AStepIsClimbedButAWallIsNot) {
    // A floor at 0, then one 16 up (a step) and, beside it, one 40 up (a wall to the profile).
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 512.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 512.0F, .y = 0.0F, .z = -16.0F},
                     {.x = 1024.0F, .y = 224.0F, .z = 16.0F});
    nx::test::addBox(geometry, {.x = 512.0F, .y = 288.0F, .z = -16.0F},
                     {.x = 1024.0F, .y = 512.0F, .z = 40.0F});
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    NavigationQuery query{mesh};
    const Vec3 below{.x = 100.0F, .y = 256.0F};
    const auto step = query.isReachable(below, {.x = 900.0F, .y = 100.0F, .z = 16.0F});
    ASSERT_TRUE(step.has_value());
    EXPECT_TRUE(*step);
    const auto wall = query.findPath({.from = below, .to = {.x = 900.0F, .y = 400.0F, .z = 40.0F}});
    ASSERT_FALSE(wall.has_value());
    EXPECT_EQ(wall.error().code, NavigationError::Unreachable);
    const auto reachable = query.isReachable(below, {.x = 900.0F, .y = 400.0F, .z = 40.0F});
    ASSERT_TRUE(reachable.has_value());
    EXPECT_FALSE(*reachable);
}

TEST(NavigationQueryTest, APointFarFromTheMeshIsOutsideIt) {
    const NavigationMesh mesh = nx::test::playerMesh(wallAcrossAFloor());
    NavigationQuery query{mesh};
    const Vec3 above{.x = 100.0F, .y = 100.0F, .z = 500.0F};
    const auto nearest = query.nearestPoint(above);
    ASSERT_FALSE(nearest.has_value());
    EXPECT_EQ(nearest.error().code, NavigationError::OutsideMesh);
    EXPECT_NE(nearest.error().detail.find("500"), std::string::npos);
    const auto path = query.findPath({.from = {.x = 100.0F, .y = 100.0F}, .to = {.x = 2000.0F, .y = 100.0F}});
    ASSERT_FALSE(path.has_value());
    EXPECT_EQ(path.error().code, NavigationError::OutsideMesh);
    const auto onIt = query.nearestPoint({.x = 100.0F, .y = 100.0F, .z = 36.0F});
    ASSERT_TRUE(onIt.has_value());
    EXPECT_NEAR(onIt->z, 0.0F, kTolerance);
}

TEST(NavigationQueryTest, CostsChooseTheWay) {
    // A pool in the middle of a corridor along y = 0 to 256, and a dry bridge around it at y = 512 to
    // 768, a hole between.
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 384.0F, .y = 768.0F}, 0.0F);
    nx::test::addFloor(geometry, {.x = 384.0F, .y = 0.0F}, {.x = 640.0F, .y = 256.0F}, 0.0F, AreaKind::Water);
    nx::test::addFloor(geometry, {.x = 384.0F, .y = 512.0F}, {.x = 640.0F, .y = 768.0F}, 0.0F);
    nx::test::addFloor(geometry, {.x = 640.0F, .y = 0.0F}, {.x = 1024.0F, .y = 768.0F}, 0.0F);
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    NavigationQuery query{mesh};
    PathRequest request{.from = {.x = 100.0F, .y = 128.0F}, .to = {.x = 924.0F, .y = 128.0F}};
    const auto wading = query.findPath(request);
    ASSERT_TRUE(wading.has_value());
    EXPECT_TRUE(std::ranges::all_of(wading->points, [](Vec3 point) { return point.y < 256.0F; }));

    nx::navigation::AreaCosts costs{1.0F};
    costs[AreaKind::Water] = 10.0F;
    request.costs = costs;
    const auto dry = query.findPath(request);
    ASSERT_TRUE(dry.has_value()) << nx::describe(dry.error());
    EXPECT_TRUE(std::ranges::any_of(dry->points, [](Vec3 point) { return point.y > 512.0F; }));
}

TEST(NavigationQueryTest, AStraightWalkStopsAtTheEdgeOfTheMesh) {
    const NavigationMesh mesh = nx::test::playerMesh(wallAcrossAFloor());
    NavigationQuery query{mesh};
    const auto clear = query.raycast({.x = 100.0F, .y = 100.0F}, {.x = 400.0F, .y = 200.0F});
    ASSERT_TRUE(clear.has_value());
    EXPECT_TRUE(clear->isClear());
    EXPECT_LT(nx::test::distanceAcross(clear->end, {.x = 400.0F, .y = 200.0F}), 1.0F);
    const auto blocked = query.raycast({.x = 100.0F, .y = 100.0F}, {.x = 900.0F, .y = 100.0F});
    ASSERT_TRUE(blocked.has_value());
    EXPECT_FALSE(blocked->isClear());
    EXPECT_NEAR(blocked->end.x, 480.0F - kRadius, kTolerance);
    EXPECT_NEAR(blocked->end.z, 0.0F, kTolerance);
}

TEST(NavigationQueryTest, RandomPointsAreOnTheMeshWithinTheRadiusAndDrawnAgainFromTheSameSeed) {
    const NavigationMesh mesh = nx::test::playerMesh(wallAcrossAFloor());
    NavigationQuery query{mesh};
    const Vec3 center{.x = 400.0F, .y = 200.0F};
    const nx::HammerUnits radius{150.0F};
    nx::Random random{7, "navigationTest"};
    nx::Random again{7, "navigationTest"};
    std::vector<Vec3> drawn;
    for (int draw = 0; draw < 64; ++draw) {
        const auto point = query.randomPoint(center, radius, random);
        ASSERT_TRUE(point.has_value());
        EXPECT_LE(nx::test::distanceAcross(*point, center), radius.value() + 0.01F);
        const auto nearest = query.nearestPoint(*point);
        ASSERT_TRUE(nearest.has_value());
        EXPECT_LT(nx::distance(*nearest, *point), 1.0F);
        EXPECT_FALSE(point->x > 480.0F - kRadius + kCell && point->y < 384.0F); // never beyond the wall
        const auto repeated = query.randomPoint(center, radius, again);
        ASSERT_TRUE(repeated.has_value());
        EXPECT_EQ(*repeated, *point);
        drawn.push_back(*point);
    }
    std::ranges::sort(drawn, {}, [](Vec3 point) { return std::tuple{point.x, point.y, point.z}; });
    EXPECT_GT(std::ranges::distance(drawn.begin(), std::ranges::unique(drawn).begin()), 32);
}

} // namespace
