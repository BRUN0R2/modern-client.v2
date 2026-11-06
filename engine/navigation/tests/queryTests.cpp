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
    const std::array avoided{nx::navigation::AvoidedPlace{.radius = nx::HammerUnits{-1}}};
    const nx::navigation::PathRequest avoiding{.from = {}, .to = {.x = 128}, .avoided = avoided};
    EXPECT_EQ(query.findPath(avoiding).error().code, nx::navigation::NavigationError::InvalidQuery);
    const std::array cheaper{nx::navigation::AvoidedPlace{.radius = nx::HammerUnits{8}, .cost = 0.5F}};
    EXPECT_EQ(nx::navigation::Corridor::plan(query, {.from = {}, .to = {.x = 128}, .avoided = cheaper})
                  .error()
                  .code,
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
    EXPECT_LT(leastDistanceTo(tight->points, wallsEnd), (kRadius * std::numbers::sqrt2_v<float>)+kCell)
        << "the shortest way, the corner of its square box at the wall's end";

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
    // A floor at 0, then one 16 up (a step) and, beside it, one 60 up (a wall to the profile, higher than
    // it jumps).
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 512.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 512.0F, .y = 0.0F, .z = -16.0F},
                     {.x = 1024.0F, .y = 224.0F, .z = 16.0F});
    nx::test::addBox(geometry, {.x = 512.0F, .y = 288.0F, .z = -16.0F},
                     {.x = 1024.0F, .y = 512.0F, .z = 60.0F});
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    NavigationQuery query{mesh};
    const Vec3 below{.x = 100.0F, .y = 256.0F};
    const auto step = query.isReachable(below, {.x = 900.0F, .y = 100.0F, .z = 16.0F});
    ASSERT_TRUE(step.has_value());
    EXPECT_TRUE(*step);
    const auto wall = query.findPath({.from = below, .to = {.x = 900.0F, .y = 400.0F, .z = 60.0F}});
    ASSERT_FALSE(wall.has_value());
    EXPECT_EQ(wall.error().code, NavigationError::Unreachable);
    const auto reachable = query.isReachable(below, {.x = 900.0F, .y = 400.0F, .z = 60.0F});
    ASSERT_TRUE(reachable.has_value());
    EXPECT_FALSE(*reachable);
}

TEST(NavigationQueryTest, AWayGoesUpALedgeItJumpsOntoAndDownOneItDropsFromButNotUpIt) {
    // On a floor at 0, a box 40 tall, which the player jumps onto, and apart one 120 tall, which it drops
    // from unhurt.
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1536.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 256.0F, .y = 128.0F, .z = -16.0F},
                     {.x = 512.0F, .y = 384.0F, .z = 40.0F});
    nx::test::addBox(geometry, {.x = 1024.0F, .y = 128.0F, .z = -16.0F},
                     {.x = 1280.0F, .y = 384.0F, .z = 120.0F});
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    NavigationQuery query{mesh};
    const Vec3 floor{.x = 700.0F, .y = 256.0F};
    const Vec3 lowTop{.x = 384.0F, .y = 256.0F, .z = 40.0F};
    const Vec3 highTop{.x = 1152.0F, .y = 256.0F, .z = 120.0F};
    for (const auto& [from, to] :
         {std::pair{floor, lowTop}, std::pair{lowTop, floor}, std::pair{highTop, floor}}) {
        const auto path = query.findPath({.from = from, .to = to});
        ASSERT_TRUE(path.has_value()) << std::format("{} to {}: {}", from, to, nx::describe(path.error()));
        EXPECT_NEAR(path->points.front().z, from.z, kTolerance);
        EXPECT_NEAR(path->points.back().z, to.z, kTolerance);
    }
    const auto up = query.findPath({.from = floor, .to = highTop});
    ASSERT_FALSE(up.has_value()) << "no way up a ledge higher than it jumps";
    EXPECT_EQ(up.error().code, NavigationError::Unreachable);
}

TEST(NavigationQueryTest, AWayIsKnownToLeadOverEveryLinkItTakesAndNeverAgainstOne) {
    // A floor with a box it jumps onto and one it only drops from, and apart from them a floor no way joins.
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1536.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 256.0F, .y = 128.0F, .z = -16.0F},
                     {.x = 512.0F, .y = 384.0F, .z = 40.0F});
    nx::test::addBox(geometry, {.x = 1024.0F, .y = 128.0F, .z = -16.0F},
                     {.x = 1280.0F, .y = 384.0F, .z = 120.0F});
    nx::test::addFloor(geometry, {.x = 3072.0F, .y = 0.0F}, {.x = 3584.0F, .y = 512.0F}, 0.0F);
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    NavigationQuery query{mesh};
    const Vec3 lowTop{.x = 384.0F, .y = 256.0F, .z = 40.0F};
    const Vec3 highTop{.x = 1152.0F, .y = 256.0F, .z = 120.0F};
    const Vec3 island{.x = 3328.0F, .y = 256.0F};
    const auto reaches = [&query](Vec3 from, Vec3 to) {
        const auto reachable = query.isReachable(from, to);
        EXPECT_TRUE(reachable.has_value());
        return reachable.value_or(false);
    };
    EXPECT_TRUE(reaches(highTop, lowTop)) << "down one link and up another";
    EXPECT_FALSE(reaches(lowTop, highTop));
    EXPECT_TRUE(reaches(island, island));
    EXPECT_FALSE(reaches(lowTop, island));
    EXPECT_FALSE(reaches(island, highTop));
}

TEST(NavigationQueryTest, ALadderJoinsTheFloorsItLeadsToBothWaysAndOneTooShortJoinsNone) {
    // A floor at 0 and a building on it, 256 tall, higher than a body drops or jumps, a ladder up its wall.
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 512.0F, .y = -32.0F, .z = -16.0F},
                     {.x = 1024.0F, .y = 544.0F, .z = 256.0F});
    const Vec3 floor{.x = 200.0F, .y = 256.0F};
    const Vec3 roof{.x = 800.0F, .y = 256.0F, .z = 256.0F};
    {
        const NavigationMesh mesh = nx::test::playerMesh(geometry);
        NavigationQuery query{mesh};
        const auto none = query.isReachable(floor, roof);
        ASSERT_TRUE(none.has_value());
        EXPECT_FALSE(*none) << "no way up without a ladder";
    }
    Geometry shortOne = geometry;
    shortOne.ladders.push_back(
        {.min = {.x = 500.0F, .y = 240.0F}, .max = {.x = 512.0F, .y = 272.0F, .z = 100.0F}});
    {
        const NavigationMesh mesh = nx::test::playerMesh(shortOne);
        NavigationQuery query{mesh};
        const auto none = query.isReachable(floor, roof);
        ASSERT_TRUE(none.has_value());
        EXPECT_FALSE(*none) << "a ladder that ends short of the roof";
    }
    geometry.ladders.push_back(
        {.min = {.x = 500.0F, .y = 240.0F}, .max = {.x = 512.0F, .y = 272.0F, .z = 256.0F}});
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    const auto outline = mesh.outline();
    const auto link = std::ranges::find(outline.links, AreaKind::Ladder, &nx::navigation::MeshLink::kind);
    ASSERT_NE(link, outline.links.end());
    EXPECT_NEAR(link->start.z, 0.0F, kTolerance);
    EXPECT_NEAR(link->end.z, 256.0F, kTolerance);
    EXPECT_NEAR(link->through.x, 506.0F, 1.0F);
    EXPECT_NEAR(link->through.y, 256.0F, 1.0F);
    NavigationQuery query{mesh};
    for (const auto& [from, to] : {std::pair{floor, roof}, std::pair{roof, floor}}) {
        const auto path = query.findPath({.from = from, .to = to});
        ASSERT_TRUE(path.has_value()) << std::format("{} to {}: {}", from, to, nx::describe(path.error()));
        EXPECT_NEAR(path->points.back().z, to.z, kTolerance);
    }
}

TEST(NavigationQueryTest, ALadderLeadsToNoFloorBehindAWall) {
    // A wall 256 tall with a floor on either side, a ladder up one side, too low to get over the wall.
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 512.0F, .y = -32.0F}, {.x = 544.0F, .y = 544.0F, .z = 256.0F});
    geometry.ladders.push_back(
        {.min = {.x = 500.0F, .y = 240.0F}, .max = {.x = 512.0F, .y = 272.0F, .z = 100.0F}});
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    NavigationQuery query{mesh};
    const auto reachable = query.isReachable({.x = 200.0F, .y = 256.0F}, {.x = 800.0F, .y = 256.0F});
    ASSERT_TRUE(reachable.has_value());
    EXPECT_FALSE(*reachable);
}

TEST(NavigationQueryTest, ALadderLeadsToNoFloorPastWhatStopsABody) {
    // The building of ALadderJoinsTheFloorsItLeadsTo..., a rail no surface shows along the roof's edge.
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 512.0F, .y = -32.0F, .z = -16.0F},
                     {.x = 1024.0F, .y = 544.0F, .z = 256.0F});
    geometry.ladders.push_back(
        {.min = {.x = 500.0F, .y = 240.0F}, .max = {.x = 512.0F, .y = 272.0F, .z = 256.0F}});
    nx::test::stopBodies(geometry, {.min = {.x = 532.0F, .y = -64.0F, .z = 256.0F},
                                    .max = {.x = 640.0F, .y = 576.0F, .z = 512.0F}});
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    NavigationQuery query{mesh};
    const auto reachable =
        query.isReachable({.x = 200.0F, .y = 256.0F}, {.x = 800.0F, .y = 256.0F, .z = 256.0F});
    ASSERT_TRUE(reachable.has_value());
    EXPECT_FALSE(*reachable);
}

TEST(NavigationQueryTest, AWayGoesAroundWhatStopsABodyThoughNoSurfaceShowsIt) {
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
    const Vec3 from{.x = 100.0F, .y = 100.0F};
    const Vec3 to{.x = 924.0F, .y = 100.0F};
    constexpr float kBarrierEnd = 384.0F;
    nx::test::stopBodies(geometry, {.min = {.x = 464.0F, .y = -64.0F, .z = -64.0F},
                                    .max = {.x = 560.0F, .y = kBarrierEnd, .z = 256.0F}});
    {
        const NavigationMesh mesh = nx::test::playerMesh(geometry);
        NavigationQuery query{mesh};
        const auto path = query.findPath({.from = from, .to = to});
        ASSERT_TRUE(path.has_value()) << nx::describe(path.error());
        EXPECT_TRUE(std::ranges::any_of(path->points, [](Vec3 point) { return point.y >= kBarrierEnd; }));
        EXPECT_GT(nx::test::lengthOf(path->points), 1000.0F); // 824 straight through it
    }
    // Across the whole floor it leaves no way at all.
    nx::test::stopBodies(geometry, {.min = {.x = 464.0F, .y = -64.0F, .z = -64.0F},
                                    .max = {.x = 560.0F, .y = 576.0F, .z = 256.0F}});
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    NavigationQuery query{mesh};
    const auto reachable = query.isReachable(from, to);
    ASSERT_TRUE(reachable.has_value());
    EXPECT_FALSE(*reachable);
}

TEST(NavigationQueryTest, WhatABodyStepsOntoStopsNoWay) {
    // Across the whole floor, as high above where a standing body's middle is as `height`.
    constexpr float kMiddle = 36.0F;
    const auto isReachableOver = [](float height) {
        Geometry geometry;
        nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
        nx::test::stopBodies(geometry, {.min = {.x = 464.0F, .y = -64.0F, .z = -64.0F},
                                        .max = {.x = 560.0F, .y = 576.0F, .z = kMiddle + height}});
        const NavigationMesh mesh = nx::test::playerMesh(geometry);
        NavigationQuery query{mesh};
        const auto reachable = query.isReachable({.x = 100.0F, .y = 100.0F}, {.x = 924.0F, .y = 100.0F});
        EXPECT_TRUE(reachable.has_value());
        return reachable.value_or(false);
    };
    EXPECT_TRUE(isReachableOver(10.0F)) << "a step";
    EXPECT_FALSE(isReachableOver(30.0F)) << "higher than the step and than a steep floor lifts a body";
}

TEST(NavigationQueryTest, ASteepFloorKeepsABodyAboveItsOwnSolid) {
    // A ramp rising along the diagonal, its normal 0.71 up, and its solid grown by a standing body's box: on
    // a corner, the body's middle is 22.4 units above where it is on a flat floor, higher than a step.
    constexpr float kRise = 0.7F;
    constexpr float kSide = 256.0F;
    Geometry geometry;
    const std::array corners{Vec3{}, Vec3{.x = kSide, .z = kSide * kRise},
                             Vec3{.x = kSide, .y = kSide, .z = 2.0F * kSide * kRise},
                             Vec3{.y = kSide, .z = kSide * kRise}};
    nx::navigation::addConvexPolygon(geometry, corners, AreaKind::Floor);
    const Vec3 normal = nx::normalized(Vec3{.x = -kRise, .y = -kRise, .z = 1.0F});
    constexpr float kMiddleOnFlat = 36.0F;
    constexpr float kRadiusOfBox = 16.0F;
    const float lifted = kMiddleOnFlat + (kRadiusOfBox * 2.0F * kRise);
    geometry.bodyHull = {
        .root = std::uint32_t{0},
        .nodes = {{.plane = {.normal = normal, .distance = normal.z * lifted},
                   .children = {nx::navigation::BodyHullLeaf::Open, nx::navigation::BodyHullLeaf::Solid}}}};
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    const auto outline = mesh.outline();
    EXPECT_EQ(std::ranges::count(outline.polygons, AreaKind::Crouch, &nx::navigation::MeshPolygon::kind), 0)
        << "a standing body fits on it";
    NavigationQuery query{mesh};
    const auto path = query.findPath(
        {.from = {.x = 48.0F, .y = 48.0F, .z = 67.0F}, .to = {.x = 208.0F, .y = 208.0F, .z = 291.0F}});
    ASSERT_TRUE(path.has_value()) << nx::describe(path.error());
    EXPECT_LT(nx::test::distanceAcross(path->points.back(), {.x = 208.0F, .y = 208.0F}), kTolerance);
}

TEST(NavigationQueryTest, NoLinkCrossesWhatStopsABody) {
    // A ledge 64 up, higher than a body jumps, and a rail no surface shows along it.
    constexpr float kLedge = 64.0F;
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = -32.0F, .y = -32.0F, .z = -16.0F},
                     {.x = 512.0F, .y = 544.0F, .z = kLedge});
    const Vec3 top{.x = 200.0F, .y = 256.0F, .z = kLedge};
    const Vec3 low{.x = 800.0F, .y = 256.0F};
    const auto dropsDown = [&](const Geometry& asked) {
        const NavigationMesh mesh = nx::test::playerMesh(asked);
        NavigationQuery query{mesh};
        const auto reachable = query.isReachable(top, low);
        EXPECT_TRUE(reachable.has_value());
        const auto outline = mesh.outline();
        const bool hasLink =
            std::ranges::contains(outline.links, AreaKind::Drop, &nx::navigation::MeshLink::kind);
        EXPECT_EQ(hasLink, reachable.value_or(false));
        return reachable.value_or(false);
    };
    EXPECT_TRUE(dropsDown(geometry));
    nx::test::stopBodies(geometry, {.min = {.x = 500.0F, .y = -64.0F, .z = 64.0F},
                                    .max = {.x = 548.0F, .y = 576.0F, .z = 256.0F}});
    EXPECT_FALSE(dropsDown(geometry));
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

TEST(NavigationQueryTest, AWayKeepsAwayFromAnAvoidedPlaceWhereAnotherWayGoes) {
    // A corridor along y = 0 to 256 and another along y = 512 to 768, joined at both ends.
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 256.0F, .y = 768.0F}, 0.0F);
    nx::test::addFloor(geometry, {.x = 256.0F, .y = 0.0F}, {.x = 768.0F, .y = 256.0F}, 0.0F);
    nx::test::addFloor(geometry, {.x = 256.0F, .y = 512.0F}, {.x = 768.0F, .y = 768.0F}, 0.0F);
    nx::test::addFloor(geometry, {.x = 768.0F, .y = 0.0F}, {.x = 1024.0F, .y = 768.0F}, 0.0F);
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    NavigationQuery query{mesh};
    const std::array jammed{nx::navigation::AvoidedPlace{
        .position = {.x = 512.0F, .y = 128.0F}, .radius = nx::HammerUnits{75.0F}, .cost = 20.0F}};
    PathRequest request{.from = {.x = 128.0F, .y = 128.0F}, .to = {.x = 896.0F, .y = 128.0F}};
    const auto near = query.findPath(request);
    ASSERT_TRUE(near.has_value());
    EXPECT_TRUE(std::ranges::all_of(near->points, [](Vec3 point) { return point.y < 256.0F; }));
    request.avoided = jammed;
    const auto around = query.findPath(request);
    ASSERT_TRUE(around.has_value()) << nx::describe(around.error());
    // Past the middle of the hole between them, which only the far corridor is: round its corners, the way
    // runs along the very edge of that corridor's floor.
    const auto isOnTheFarWay = [](Vec3 point) {
        return point.y > 384.0F;
    };
    EXPECT_TRUE(std::ranges::any_of(around->points, isOnTheFarWay));
    const auto corridor = nx::navigation::Corridor::plan(query, request);
    ASSERT_TRUE(corridor.has_value());
    std::vector<Vec3> way;
    corridor->appendWay(query, way);
    EXPECT_TRUE(std::ranges::any_of(way, isOnTheFarWay)) << "a corridor too";
    // A place off the mesh avoids nothing.
    const std::array offTheMesh{
        nx::navigation::AvoidedPlace{.position = {.x = 512.0F, .y = 384.0F, .z = 4096.0F},
                                     .radius = nx::HammerUnits{75.0F},
                                     .cost = 20.0F}};
    request.avoided = offTheMesh;
    EXPECT_EQ(query.findPath(request)->points, near->points);
}

TEST(NavigationQueryTest, AWayStillGoesThroughAnAvoidedPlaceWhenNoOtherWayDoes) {
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 256.0F}, 0.0F);
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    NavigationQuery query{mesh};
    const std::array jammed{nx::navigation::AvoidedPlace{
        .position = {.x = 512.0F, .y = 128.0F}, .radius = nx::HammerUnits{75.0F}, .cost = 20.0F}};
    const auto path = query.findPath(
        {.from = {.x = 128.0F, .y = 128.0F}, .to = {.x = 896.0F, .y = 128.0F}, .avoided = jammed});
    ASSERT_TRUE(path.has_value()) << nx::describe(path.error());
    EXPECT_LT(nx::test::distanceAcross(path->points.back(), {.x = 896.0F, .y = 128.0F}), kTolerance);
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
    // Past the middle of the hole: on the bridge, along its very edge.
    EXPECT_TRUE(std::ranges::any_of(dry->points, [](Vec3 point) { return point.y > 384.0F; }));
}

TEST(NavigationQueryTest, ABodyStandingPastTheFloorsEndIsNearTheMeshButOneFartherIsNot) {
    // A platform 200 tall, nothing under its edge within the standing height: the mesh goes as far as its
    // end, and a body stands until its middle is its radius past it.
    constexpr float kEnd = 512.0F;
    Geometry geometry;
    nx::test::addBox(geometry, {.x = 0.0F, .y = 0.0F, .z = -200.0F}, {.x = kEnd, .y = 512.0F, .z = 0.0F});
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    NavigationQuery query{mesh};
    const Vec3 standing{.x = kEnd + kRadius, .y = 256.0F};
    const auto near = query.nearestPoint(standing);
    ASSERT_TRUE(near.has_value()) << nx::describe(near.error());
    EXPECT_NEAR(near->x, kEnd, kCell);
    EXPECT_NEAR(near->z, 0.0F, kTolerance);
    const auto path = query.findPath({.from = standing, .to = {.x = 100.0F, .y = 100.0F}});
    ASSERT_TRUE(path.has_value()) << nx::describe(path.error());
    const Vec3 falling{.x = kEnd + (4.0F * kRadius), .y = 256.0F};
    const auto beyond = query.nearestPoint(falling);
    ASSERT_FALSE(beyond.has_value());
    EXPECT_EQ(beyond.error().code, NavigationError::OutsideMesh);
}

TEST(NavigationQueryTest, ABodyOnAWallNarrowerThanItStandsOnTheMeshAndGoesOnFromThere) {
    // A wall 24 thick and 40 tall on a floor, a way around its end: its top is narrower than the body, whose
    // box stands on it all the same, so it is floor, and the way on goes down off it.
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 500.0F, .y = 0.0F, .z = -16.0F}, {.x = 524.0F, .y = 384.0F, .z = 40.0F});
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    NavigationQuery query{mesh};
    const Vec3 onTop{.x = 512.0F, .y = 256.0F, .z = 40.0F};
    const auto standing = query.nearestPoint(onTop);
    ASSERT_TRUE(standing.has_value()) << nx::describe(standing.error());
    EXPECT_NEAR(standing->z, 40.0F, kTolerance);
    const auto path = query.findPath({.from = onTop, .to = {.x = 900.0F, .y = 256.0F}});
    ASSERT_TRUE(path.has_value()) << nx::describe(path.error());
    EXPECT_NEAR(path->points.back().z, 0.0F, kTolerance);
}

TEST(NavigationQueryTest, ABodyOnAPostTooSmallForTheMeshFindsTheFloorBesideIt) {
    // A post 24 wide and 40 tall on a floor, within one tile: its top is an island smaller than the mesh
    // keeps, and a body standing on it is near the floor beside it.
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 564.0F, .y = 180.0F, .z = -16.0F},
                     {.x = 588.0F, .y = 204.0F, .z = 40.0F});
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    NavigationQuery query{mesh};
    const Vec3 onTop{.x = 576.0F, .y = 192.0F, .z = 40.0F};
    const auto floor = query.nearestPoint(onTop);
    ASSERT_TRUE(floor.has_value()) << nx::describe(floor.error());
    EXPECT_NEAR(floor->z, 0.0F, kTolerance);
    const auto path = query.findPath({.from = onTop, .to = {.x = 900.0F, .y = 256.0F}});
    ASSERT_TRUE(path.has_value()) << nx::describe(path.error());
}

TEST(NavigationQueryTest, ABodyOnAPostItDropsFromUnhurtFindsTheFloorAndOneOnAHigherPostDoesNot) {
    // Posts 24 wide on a floor, too small for the mesh: one 150 tall, which the player drops from unhurt
    // (156.25), and one 200 tall, which it does not.
    constexpr float kLow = 150.0F;
    constexpr float kHigh = 200.0F;
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 1024.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 564.0F, .y = 180.0F, .z = -16.0F},
                     {.x = 588.0F, .y = 204.0F, .z = kLow});
    nx::test::addBox(geometry, {.x = 564.0F, .y = 820.0F, .z = -16.0F},
                     {.x = 588.0F, .y = 844.0F, .z = kHigh});
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    NavigationQuery query{mesh};
    const auto floor = query.nearestPoint({.x = 576.0F, .y = 192.0F, .z = kLow});
    ASSERT_TRUE(floor.has_value()) << nx::describe(floor.error());
    EXPECT_NEAR(floor->z, 0.0F, kTolerance);
    const auto path =
        query.findPath({.from = {.x = 576.0F, .y = 192.0F, .z = kLow}, .to = {.x = 900.0F, .y = 192.0F}});
    ASSERT_TRUE(path.has_value()) << nx::describe(path.error());
    const auto tooHigh = query.nearestPoint({.x = 576.0F, .y = 832.0F, .z = kHigh});
    ASSERT_FALSE(tooHigh.has_value());
    EXPECT_EQ(tooHigh.error().code, NavigationError::OutsideMesh);
}

TEST(NavigationQueryTest, AStraightWalkStopsAtTheEdgeOfTheMesh) {
    const NavigationMesh mesh = nx::test::playerMesh(wallAcrossAFloor());
    NavigationQuery query{mesh};
    const auto clear = query.raycast({.x = 100.0F, .y = 100.0F}, {.x = 400.0F, .y = 200.0F});
    ASSERT_TRUE(clear.has_value());
    EXPECT_GE(clear->fraction, 1.0F);
    EXPECT_LT(nx::test::distanceAcross(clear->end, {.x = 400.0F, .y = 200.0F}), 1.0F);
    const auto blocked = query.raycast({.x = 100.0F, .y = 100.0F}, {.x = 900.0F, .y = 100.0F});
    ASSERT_TRUE(blocked.has_value());
    EXPECT_LT(blocked->fraction, 1.0F);
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
