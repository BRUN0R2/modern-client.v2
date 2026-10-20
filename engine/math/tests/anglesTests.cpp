// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;

#include "contractRecorder.hpp"
#include "mathAssertions.hpp"

namespace {

using nx::AngleVectors;
using nx::Degrees;
using nx::EulerAngles;
using nx::Vec3;
using nx::test::isNear;

using namespace nx::literals;

/// A spread of orientations, including negative and wrapped-around angles.
constexpr std::array kOrientations{
    EulerAngles{},
    EulerAngles{.pitch = 30_deg, .yaw = 45_deg, .roll = 0_deg},
    EulerAngles{.pitch = -60_deg, .yaw = 170_deg, .roll = 15_deg},
    EulerAngles{.pitch = 89_deg, .yaw = -120_deg, .roll = -30_deg},
    EulerAngles{.pitch = 12.5_deg, .yaw = 725_deg, .roll = 180_deg},
};

TEST(AnglesTest, EulerAnglesIsAPlainAggregate) {
    static_assert(std::is_trivially_copyable_v<EulerAngles>);
    static_assert(EulerAngles{} == EulerAngles{.pitch = 0_deg, .yaw = 0_deg, .roll = 0_deg});
    EXPECT_EQ(std::format("{}", EulerAngles{.pitch = -10_deg, .yaw = 90_deg, .roll = 0.5_deg}),
              "(-10 90 0.5)");
}

TEST(AnglesTest, ZeroAnglesFaceXWithRightOnMinusY) {
    const AngleVectors axes = nx::angleVectors(EulerAngles{});
    EXPECT_EQ(axes.forward, (Vec3{.x = 1}));
    EXPECT_EQ(axes.right, (Vec3{.y = -1}));
    EXPECT_EQ(axes.up, (Vec3{.z = 1}));
}

TEST(AnglesTest, PositiveYawTurnsLeft) {
    const AngleVectors axes = nx::angleVectors(EulerAngles{.yaw = 90_deg});
    EXPECT_TRUE(isNear(axes.forward, Vec3{.y = 1}));
    EXPECT_TRUE(isNear(axes.right, Vec3{.x = 1}));
    EXPECT_TRUE(isNear(axes.up, Vec3{.z = 1}));
}

TEST(AnglesTest, PositivePitchLooksDown) {
    const AngleVectors axes = nx::angleVectors(EulerAngles{.pitch = 90_deg});
    EXPECT_TRUE(isNear(axes.forward, Vec3{.z = -1}));
    EXPECT_TRUE(isNear(axes.right, Vec3{.y = -1}));
    EXPECT_TRUE(isNear(axes.up, Vec3{.x = 1}));

    const AngleVectors slightlyDown = nx::angleVectors(EulerAngles{.pitch = 10_deg});
    EXPECT_LT(slightlyDown.forward.z, 0.0F);
}

TEST(AnglesTest, PositiveRollLeansTheViewToTheRight) {
    const AngleVectors axes = nx::angleVectors(EulerAngles{.roll = 90_deg});
    EXPECT_TRUE(isNear(axes.forward, Vec3{.x = 1}));
    EXPECT_TRUE(isNear(axes.right, Vec3{.z = -1}));
    EXPECT_TRUE(isNear(axes.up, Vec3{.y = -1}));
}

TEST(AnglesTest, AxesAreOrthonormalAndRightHanded) {
    for (const EulerAngles& angles : kOrientations) {
        SCOPED_TRACE(std::format("angles {}", angles));
        const AngleVectors axes = nx::angleVectors(angles);
        EXPECT_TRUE(isNear(nx::length(axes.forward), 1.0F));
        EXPECT_TRUE(isNear(nx::length(axes.right), 1.0F));
        EXPECT_TRUE(isNear(nx::length(axes.up), 1.0F));
        EXPECT_TRUE(isNear(nx::dot(axes.forward, axes.right), 0.0F));
        EXPECT_TRUE(isNear(nx::dot(axes.forward, axes.up), 0.0F));
        EXPECT_TRUE(isNear(nx::dot(axes.right, axes.up), 0.0F));
        // forward x left = up, with left = -right.
        EXPECT_TRUE(isNear(nx::cross(axes.forward, -axes.right), axes.up));
    }
}

TEST(AnglesTest, TheRotationMatrixHoldsForwardLeftAndUpInItsColumns) {
    for (const EulerAngles& angles : kOrientations) {
        SCOPED_TRACE(std::format("angles {}", angles));
        const auto [forward, left, up] = nx::transposed(nx::rotationMatrix(angles)).rows;
        const AngleVectors axes = nx::angleVectors(angles);
        // Same computation: equal to the bit.
        EXPECT_EQ(forward, axes.forward);
        EXPECT_EQ(left, -axes.right);
        EXPECT_EQ(up, axes.up);
        EXPECT_EQ(nx::rotationMatrix(angles) * Vec3{.x = 1}, axes.forward);
    }
}

TEST(AnglesTest, TheRotationAppliesRollThenPitchThenYaw) {
    const EulerAngles angles{.pitch = 20_deg, .yaw = 35_deg, .roll = -50_deg};
    const nx::Mat3 composed = nx::rotationMatrix(EulerAngles{.yaw = angles.yaw}) *
                              nx::rotationMatrix(EulerAngles{.pitch = angles.pitch}) *
                              nx::rotationMatrix(EulerAngles{.roll = angles.roll});
    EXPECT_TRUE(isNear(nx::rotationMatrix(angles), composed));
}

TEST(AnglesTest, VectorAnglesFacesTheDirection) {
    for (const EulerAngles& angles : kOrientations) {
        SCOPED_TRACE(std::format("angles {}", angles));
        const Vec3 forward = nx::angleVectors(angles).forward;
        const EulerAngles facing = nx::vectorAngles(forward);
        EXPECT_EQ(facing.roll, 0_deg);
        EXPECT_TRUE(isNear(nx::angleVectors(facing).forward, forward));
    }
}

TEST(AnglesTest, VectorAnglesOfTheAxes) {
    EXPECT_EQ(nx::vectorAngles(Vec3{.x = 2}), EulerAngles{});
    EXPECT_EQ(nx::vectorAngles(Vec3{.y = 3}), (EulerAngles{.yaw = 90_deg}));
    EXPECT_EQ(nx::vectorAngles(Vec3{.y = -3}), (EulerAngles{.yaw = -90_deg}));
    EXPECT_EQ(nx::vectorAngles(Vec3{.x = -1}), (EulerAngles{.yaw = 180_deg}));
    // Straight down is positive pitch, like the view convention; the yaw is 0 even for -0 components.
    EXPECT_EQ(nx::vectorAngles(Vec3{.z = -5}), (EulerAngles{.pitch = 90_deg}));
    EXPECT_EQ(nx::vectorAngles(Vec3{.x = -0.0F, .z = 5}), (EulerAngles{.pitch = -90_deg}));
    EXPECT_TRUE(isNear(nx::vectorAngles(Vec3{.x = 1, .z = -1}).pitch.value(), 45.0F));
}

TEST(AnglesTest, VectorAnglesOfTheZeroVectorIsAViolation) {
    const nx::test::ContractRecorder recorder;
    std::ignore = nx::vectorAngles(Vec3{});
    EXPECT_EQ(nx::test::ContractRecorder::count(), 1);
}

TEST(AnglesTest, NormalizeAngleWrapsIntoTheHalfOpenRange) {
    EXPECT_EQ(nx::normalizeAngle(0_deg), 0_deg);
    EXPECT_EQ(nx::normalizeAngle(180_deg), 180_deg);
    EXPECT_EQ(nx::normalizeAngle(-180_deg), 180_deg);
    EXPECT_EQ(nx::normalizeAngle(190_deg), -170_deg);
    EXPECT_EQ(nx::normalizeAngle(-190_deg), 170_deg);
    EXPECT_EQ(nx::normalizeAngle(360_deg), 0_deg);
    EXPECT_EQ(nx::normalizeAngle(725_deg), 5_deg);
    EXPECT_EQ(nx::normalizeAngle(-1085_deg), -5_deg);
    EXPECT_EQ(nx::normalizeAngle(Degrees{359.5F}), Degrees{-0.5F});
}

TEST(AnglesTest, NormalizingAnInfiniteAngleIsAViolation) {
    const nx::test::ContractRecorder recorder;
    std::ignore = nx::normalizeAngle(Degrees{std::numeric_limits<float>::infinity()});
    EXPECT_EQ(nx::test::ContractRecorder::count(), 1);
}

} // namespace
