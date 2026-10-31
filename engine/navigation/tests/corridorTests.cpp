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

TEST(CorridorTest, CornerLimitsAndOtherQueriesKeepTheSameWay) {
    const NavigationMesh mesh = nx::test::playerMesh(wallAcrossAFloor());
    NavigationQuery query{mesh};
    const PathRequest request{.from = {.x = 100.0F, .y = 100.0F}, .to = {.x = 924.0F, .y = 100.0F}};
    auto corridor = Corridor::plan(query, request);
    ASSERT_TRUE(corridor.has_value()) << nx::describe(corridor.error());
    const auto full = corridor->corners(query, std::numeric_limits<std::size_t>::max());
    ASSERT_GE(full.size(), 2U);
    EXPECT_TRUE(corridor->corners(query, 0).empty());
    for (std::size_t limit = 1; limit <= full.size(); ++limit) {
        const auto limited = corridor->corners(query, limit);
        EXPECT_TRUE(std::ranges::equal(limited, std::span{full}.first(limit)));
    }
    const auto path = query.findPath(request);
    ASSERT_TRUE(path.has_value()) << nx::describe(path.error());
    EXPECT_EQ(corridor->corners(query, std::numeric_limits<std::size_t>::max()), full)
        << "the query's scratch can be reused by another search";
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
    std::vector<Vec3> planned;
    corridor->appendWay(query, planned);
    EXPECT_EQ(planned, found->points);
    ASSERT_GE(planned.size(), 3U) << "around the wall's end";
    EXPECT_EQ(planned.front(), corridor->position());
    EXPECT_EQ(planned.back(), corridor->target());

    const Vec3 moved = corridor->moveAgent(query, {.x = 100.0F, .y = 200.0F});
    std::vector<Vec3> left{Vec3{.z = -1.0F}};
    corridor->appendWay(query, left);
    ASSERT_GE(left.size(), 2U);
    EXPECT_EQ(left.front(), (Vec3{.z = -1.0F})) << "appended after what was there";
    EXPECT_EQ(left[1], moved);
    EXPECT_EQ(left.back(), corridor->target());
}

TEST(CorridorTest, ItSteersAsItWantsWithNothingInTheWayAndAroundABodyStandingInIt) {
    constexpr nx::UnitsPerSecond kSpeed{250.0F};
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    NavigationQuery query{mesh};
    auto corridor =
        Corridor::plan(query, {.from = {.x = 100.0F, .y = 256.0F}, .to = {.x = 900.0F, .y = 256.0F}});
    ASSERT_TRUE(corridor.has_value());
    const Vec3 desired{.x = kSpeed.value()};
    const Vec3 free = corridor->steer(query, {.velocity = desired, .desired = desired, .maxSpeed = kSpeed});
    EXPECT_GT(free.x, 0.9F * kSpeed.value()) << "on its way, nothing about it";
    EXPECT_LT(std::abs(free.y), 0.1F * kSpeed.value());

    const std::array standing{nx::navigation::SteeringBody{.feet = corridor->position() + Vec3{.x = 48.0F},
                                                           .radius = nx::HammerUnits{16.0F}}};
    const Vec3 around = corridor->steer(
        query, {.velocity = desired, .desired = desired, .maxSpeed = kSpeed, .bodies = standing});
    EXPECT_GT(std::abs(around.y), 0.2F * kSpeed.value()) << "to a side of the body ahead";
    EXPECT_EQ(around,
              corridor->steer(
                  query, {.velocity = desired, .desired = desired, .maxSpeed = kSpeed, .bodies = standing}))
        << "the same for the same inputs";
}

/// A floor with a box 120 tall on it, which the player drops from unhurt.
[[nodiscard]] auto boxOnAFloor() -> Geometry {
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 384.0F, .y = 128.0F, .z = -16.0F},
                     {.x = 640.0F, .y = 384.0F, .z = 120.0F});
    return geometry;
}

TEST(CorridorTest, ItCrossesALinkFromItsStartAndGoesOnFromItsEnd) {
    const NavigationMesh mesh = nx::test::playerMesh(boxOnAFloor());
    NavigationQuery query{mesh};
    const Vec3 to{.x = 900.0F, .y = 256.0F};
    auto corridor = Corridor::plan(query, {.from = {.x = 512.0F, .y = 256.0F, .z = 120.0F}, .to = to});
    ASSERT_TRUE(corridor.has_value()) << nx::describe(corridor.error());
    const std::size_t polygonsAtStart = corridor->polygonCount();
    const auto next = corridor->nextLink(query);
    ASSERT_TRUE(next.has_value()) << "off the box";
    EXPECT_EQ(next->kind, nx::navigation::AreaKind::Drop);
    EXPECT_GT(next->start.z, next->end.z);
    const std::vector<Vec3> corners = corridor->corners(query, 4);
    ASSERT_FALSE(corners.empty());
    EXPECT_LT(nx::distance(corners.back(), next->start), kTolerance) << "the corners stop at the link";
    EXPECT_EQ(corridor->nextLink(query), next) << "until it crosses it";

    const auto crossed = corridor->crossLink(query);
    ASSERT_TRUE(crossed.has_value());
    EXPECT_EQ(crossed, next);
    EXPECT_EQ(corridor->position(), crossed->end);
    EXPECT_LT(corridor->polygonCount(), polygonsAtStart);
    EXPECT_FALSE(corridor->nextLink(query).has_value()) << "on along the floor";
    EXPECT_FALSE(corridor->crossLink(query).has_value());
    const std::vector<Vec3> onFloor = corridor->corners(query, 4);
    ASSERT_FALSE(onFloor.empty());
    EXPECT_EQ(onFloor.back(), corridor->target());
}

TEST(CorridorTest, ItSteersOffTheLedgeItsNextLinkLeavesFromAsItWants) {
    constexpr nx::UnitsPerSecond kSpeed{250.0F};
    constexpr float kApproach = 24.0F;
    const NavigationMesh mesh = nx::test::playerMesh(boxOnAFloor());
    NavigationQuery query{mesh};
    auto corridor = Corridor::plan(
        query, {.from = {.x = 560.0F, .y = 256.0F, .z = 120.0F}, .to = {.x = 900.0F, .y = 256.0F}});
    ASSERT_TRUE(corridor.has_value()) << nx::describe(corridor.error());
    const auto next = corridor->nextLink(query);
    ASSERT_TRUE(next.has_value());
    const Vec3 fromStart = corridor->position() - next->start;
    const Vec3 across = Vec3{.x = fromStart.x, .y = fromStart.y} /
                        nx::test::distanceAcross(corridor->position(), next->start);
    const Vec3 near = corridor->moveAgent(query, next->start + (across * kApproach));
    ASSERT_NEAR(nx::test::distanceAcross(near, next->start), kApproach, kTolerance);
    const Vec3 desired = -across * kSpeed.value();
    const Vec3 steered =
        corridor->steer(query, {.velocity = desired, .desired = desired, .maxSpeed = kSpeed});
    EXPECT_GT(nx::dot(steered, desired) / kSpeed.value(), 0.9F * kSpeed.value())
        << "the ledge it drops from is no wall to it";
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
