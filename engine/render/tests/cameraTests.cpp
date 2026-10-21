// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.render;

namespace {

using nx::Vec3;
using nx::Vec4;
using nx::render::Camera;

/// `point` in normalized device coordinates: clip space divided by w.
[[nodiscard]] auto project(const Camera& camera, float aspect, Vec3 point) -> Vec3 {
    const Vec4 clip = nx::render::viewProjectionMatrix(camera, aspect) * nx::extend(point, 1.0F);
    return Vec3{.x = clip.x / clip.w, .y = clip.y / clip.w, .z = clip.z / clip.w};
}

constexpr float kFourByThree = 4.0F / 3.0F;
constexpr float kTolerance = 1.0e-5F;

TEST(CameraTest, WhatIsAheadIsInTheMiddle) {
    const Camera camera{.position = {.x = 100.0F, .y = -50.0F, .z = 20.0F}};
    const Vec3 ahead = project(camera, kFourByThree, {.x = 600.0F, .y = -50.0F, .z = 20.0F});
    EXPECT_NEAR(ahead.x, 0.0F, kTolerance);
    EXPECT_NEAR(ahead.y, 0.0F, kTolerance);
}

TEST(CameraTest, DepthIsOneAtTheNearPlaneAndFallsTowardZero) {
    const Camera camera{};
    const auto depthAt = [&camera](float distance) {
        return project(camera, kFourByThree, {.x = distance}).z;
    };
    EXPECT_NEAR(depthAt(nx::render::kNearPlane), 1.0F, kTolerance);
    EXPECT_NEAR(depthAt(nx::render::kNearPlane * 1000.0F), 0.001F, kTolerance);
    EXPECT_GT(depthAt(100.0F), depthAt(200.0F)) << "nearer is greater: reversed depth";
}

TEST(CameraTest, RightIsPositiveXAndUpIsPositiveY) {
    const Camera camera{}; // looking along +X: +Y is to the left, +Z up
    const Vec3 left = project(camera, kFourByThree, {.x = 100.0F, .y = 10.0F});
    const Vec3 up = project(camera, kFourByThree, {.x = 100.0F, .z = 10.0F});
    EXPECT_LT(left.x, 0.0F);
    EXPECT_GT(up.y, 0.0F);
}

TEST(CameraTest, FieldOfViewIsTheHorizontalAngleOfAFourByThreeView) {
    const Camera camera{.fieldOfView = nx::Degrees{90.0F}};
    // 45 degrees to the right: the right edge of a 4:3 view.
    EXPECT_NEAR(project(camera, kFourByThree, {.x = 100.0F, .y = -100.0F}).x, 1.0F, kTolerance);
    // Wider screens see more to the sides at the same vertical angle (Hor+).
    EXPECT_NEAR(project(camera, 16.0F / 9.0F, {.x = 100.0F, .y = -100.0F}).x, 0.75F, kTolerance);
    // The vertical angle stays that of the 4:3 view: tan(half) = 3/4.
    EXPECT_NEAR(project(camera, 16.0F / 9.0F, {.x = 100.0F, .z = 75.0F}).y, 1.0F, kTolerance);
}

TEST(CameraTest, YawTurnsTheViewToTheLeft) {
    const Camera camera{.angles = {.yaw = nx::Degrees{90.0F}}}; // now looking along +Y
    const Vec3 ahead = project(camera, kFourByThree, {.y = 100.0F});
    EXPECT_NEAR(ahead.x, 0.0F, kTolerance);
    EXPECT_NEAR(ahead.y, 0.0F, kTolerance);
    EXPECT_GT(ahead.z, 0.0F);
}

/// Whether the frustum of `camera` keeps `box`: no plane has it wholly behind.
[[nodiscard]] auto isInView(const Camera& camera, float aspect, const nx::Aabb& box) -> bool {
    return std::ranges::none_of(nx::render::frustumPlanes(camera, aspect), [&box](const nx::Plane& plane) {
        return plane.sideOf(box) == nx::PlaneSide::Back;
    });
}

TEST(CameraTest, TheFrustumKeepsWhatTheProjectionShows) {
    const Camera camera{}; // at the origin, looking along +X: 90 degrees across at 4:3, 73.7 up
    const auto at = [](float x, float y, float z) {
        return nx::Aabb::around({.x = x, .y = y, .z = z}, {.x = 1.0F, .y = 1.0F, .z = 1.0F});
    };
    EXPECT_TRUE(isInView(camera, kFourByThree, at(100.0F, 0.0F, 0.0F))) << "ahead";
    EXPECT_FALSE(isInView(camera, kFourByThree, at(-100.0F, 0.0F, 0.0F))) << "behind";
    EXPECT_TRUE(isInView(camera, kFourByThree, at(100.0F, 95.0F, 0.0F))) << "just inside the left edge";
    EXPECT_FALSE(isInView(camera, kFourByThree, at(100.0F, 110.0F, 0.0F))) << "past the left edge";
    EXPECT_FALSE(isInView(camera, kFourByThree, at(100.0F, -110.0F, 0.0F))) << "past the right edge";
    EXPECT_TRUE(isInView(camera, kFourByThree, at(100.0F, 0.0F, 70.0F))) << "just under the top";
    EXPECT_FALSE(isInView(camera, kFourByThree, at(100.0F, 0.0F, 80.0F))) << "over the top";
    EXPECT_TRUE(isInView(camera, 2.0F, at(100.0F, 140.0F, 0.0F))) << "wider screens see more to the sides";
}
} // namespace
