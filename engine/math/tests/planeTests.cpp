// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;

namespace {

using nx::Aabb;
using nx::Plane;
using nx::PlaneSide;
using nx::Vec3;

/// The floor at height 64, facing up.
constexpr Plane kFloor{.normal = Vec3{.z = 1}, .distance = 64};

/// A wall at x = -100 facing -X: its front is x < -100.
constexpr Plane kWall{.normal = Vec3{.x = -1}, .distance = 100};

constexpr Aabb kCrate = Aabb::around(Vec3{}, Vec3{.x = 8, .y = 8, .z = 8});

TEST(PlaneTest, SignedDistance) {
    static_assert(kFloor.signedDistance(Vec3{.z = 100}) == 36);
    static_assert(kFloor.signedDistance(Vec3{.x = 5, .z = 64}) == 0);
    static_assert(kFloor.signedDistance(Vec3{.z = 0}) == -64);
    static_assert(kWall.signedDistance(Vec3{.x = -150}) == 50);
    static_assert(kWall.signedDistance(Vec3{}) == -100);
}

TEST(PlaneTest, ThroughAPoint) {
    static_assert(Plane::through(Vec3{.x = 7, .y = -3, .z = 64}, Vec3{.z = 1}) == kFloor);
    static_assert(Plane::through(Vec3{.x = -100, .y = 20}, Vec3{.x = -1}) == kWall);
}

TEST(PlaneTest, BoxSides) {
    static_assert(kFloor.sideOf(kCrate.translated(Vec3{.z = 100})) == PlaneSide::Front);
    static_assert(kFloor.sideOf(kCrate) == PlaneSide::Back);
    static_assert(kFloor.sideOf(kCrate.translated(Vec3{.z = 60})) == PlaneSide::Spanning);

    // Normals with negative components pick the opposite corners.
    static_assert(kWall.sideOf(kCrate.translated(Vec3{.x = -200})) == PlaneSide::Front);
    static_assert(kWall.sideOf(kCrate) == PlaneSide::Back);
    static_assert(kWall.sideOf(kCrate.translated(Vec3{.x = -100})) == PlaneSide::Spanning);
}

TEST(PlaneTest, ABoxTouchingFromTheFrontIsInFront) {
    // Resting on the floor: the bottom face lies on the plane.
    static_assert(kFloor.sideOf(kCrate.translated(Vec3{.z = 72})) == PlaneSide::Front);
    // Touching from behind is spanning: the top face is at distance 0, which counts as the front.
    static_assert(kFloor.sideOf(kCrate.translated(Vec3{.z = 56})) == PlaneSide::Spanning);
}

TEST(PlaneTest, ObliquePlanes) {
    const Vec3 normal = nx::normalized(Vec3{.x = 1, .y = 1});
    const Plane diagonal = Plane::through(Vec3{}, normal);
    EXPECT_EQ(diagonal.sideOf(kCrate.translated(Vec3{.x = 20, .y = 20})), PlaneSide::Front);
    EXPECT_EQ(diagonal.sideOf(kCrate.translated(Vec3{.x = -20, .y = -20})), PlaneSide::Back);
    EXPECT_EQ(diagonal.sideOf(kCrate), PlaneSide::Spanning);
    // Far along the plane itself, the box still straddles it.
    EXPECT_EQ(diagonal.sideOf(kCrate.translated(Vec3{.x = 500, .y = -500})), PlaneSide::Spanning);
}

} // namespace
