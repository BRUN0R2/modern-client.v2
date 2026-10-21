// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;

#include "contractRecorder.hpp"
#include "mathAssertions.hpp"

namespace {

using nx::EulerAngles;
using nx::Quat;
using nx::Radians;
using nx::Vec3;
using nx::test::isNear;
using nx::test::isSameRotation;

using namespace nx::literals;

constexpr Vec3 kAxisZ{.z = 1};
constexpr Vec3 kAxisX{.x = 1};

[[nodiscard]] auto quarterTurn() -> Radians {
    return nx::toRadians(90_deg);
}

TEST(QuaternionTest, DefaultIsTheIdentity) {
    static_assert(std::is_trivially_copyable_v<Quat>);
    constexpr Vec3 v{.x = 1, .y = 2, .z = 3};
    static_assert(nx::rotate(Quat{}, v) == v);
    EXPECT_EQ(nx::rotationMatrix(Quat{}), nx::Mat3{});
}

TEST(QuaternionTest, FromAxisAngleRotatesCounterClockwiseAboutTheAxis) {
    const Quat quarter = Quat::fromAxisAngle(kAxisZ, quarterTurn());
    EXPECT_TRUE(isNear(nx::rotate(quarter, Vec3{.x = 1}), Vec3{.y = 1}));
    EXPECT_TRUE(isNear(nx::rotate(quarter, Vec3{.y = 1}), Vec3{.x = -1}));
    EXPECT_TRUE(isNear(nx::rotate(quarter, kAxisZ), kAxisZ));
}

TEST(QuaternionTest, FromAxisAngleNeedsAUnitAxis) {
    const nx::test::ContractRecorder recorder;
    std::ignore = Quat::fromAxisAngle(Vec3{.z = 2}, quarterTurn());
    EXPECT_EQ(nx::test::ContractRecorder::count(), 1);
}

TEST(QuaternionTest, FromEulerAnglesIsTheRotationOfTheAngles) {
    for (const EulerAngles angles : {EulerAngles{.pitch = 30_deg, .yaw = 45_deg},
                                     EulerAngles{.pitch = -60_deg, .yaw = 170_deg, .roll = 15_deg},
                                     EulerAngles{.pitch = 89_deg, .yaw = -120_deg, .roll = -30_deg}}) {
        SCOPED_TRACE(std::format("angles {}", angles));
        const Quat rotation = Quat::fromEulerAngles(angles);
        EXPECT_TRUE(isNear(nx::rotationMatrix(rotation), nx::rotationMatrix(angles)));
        EXPECT_TRUE(isNear(nx::rotate(rotation, Vec3{.x = 1}), nx::angleVectors(angles).forward));
    }
    EXPECT_TRUE(isSameRotation(Quat::fromEulerAngles(EulerAngles{.yaw = 90_deg}),
                               Quat::fromAxisAngle(kAxisZ, quarterTurn())));
}

TEST(QuaternionTest, ProductAppliesTheRightOperandFirst) {
    const Quat yaw = Quat::fromAxisAngle(kAxisZ, quarterTurn());
    const Quat roll = Quat::fromAxisAngle(kAxisX, quarterTurn());
    constexpr Vec3 v{.y = 1};

    // Roll takes +Y to +Z, which yaw keeps; yaw takes +Y to -X, which roll keeps.
    EXPECT_TRUE(isNear(nx::rotate(yaw * roll, v), Vec3{.z = 1}));
    EXPECT_TRUE(isNear(nx::rotate(roll * yaw, v), Vec3{.x = -1}));
    EXPECT_TRUE(isNear(nx::rotationMatrix(yaw * roll), nx::rotationMatrix(yaw) * nx::rotationMatrix(roll)));
}

TEST(QuaternionTest, ConjugateIsTheInverseRotation) {
    const Quat rotation = Quat::fromEulerAngles(EulerAngles{.pitch = 20_deg, .yaw = -35_deg, .roll = 5_deg});
    constexpr Vec3 v{.x = 3, .y = -1, .z = 2};
    EXPECT_TRUE(isNear(nx::rotate(nx::conjugate(rotation), nx::rotate(rotation, v)), v));
    EXPECT_TRUE(isSameRotation(rotation * nx::conjugate(rotation), Quat{}));
}

TEST(QuaternionTest, Normalized) {
    const Quat scaled{.x = 0, .y = 0, .z = 3, .w = 4};
    EXPECT_EQ(nx::normalized(scaled), (Quat{.x = 0, .y = 0, .z = 0.6F, .w = 0.8F}));

    const nx::test::ContractRecorder recorder;
    std::ignore = nx::normalized(Quat{.w = 0});
    EXPECT_EQ(nx::test::ContractRecorder::count(), 1);
}

TEST(QuaternionTest, NormalizesLargeAndTinyComponents) {
    const nx::test::ContractRecorder recorder;
    // 3e30 and 4e30 are not exact floats: the results are near 0.6 and 0.8, not equal.
    EXPECT_TRUE(isSameRotation(nx::normalized(Quat{.x = 0, .y = 0, .z = 3e30F, .w = 4e30F}),
                               Quat{.x = 0, .y = 0, .z = 0.6F, .w = 0.8F}));
    EXPECT_TRUE(isSameRotation(nx::normalized(Quat{.x = 0, .y = 0, .z = 3e-30F, .w = 4e-30F}),
                               Quat{.x = 0, .y = 0, .z = 0.6F, .w = 0.8F}));
    EXPECT_EQ(nx::test::ContractRecorder::count(), 0);
}

TEST(QuaternionTest, SlerpMovesAtConstantAngularSpeed) {
    const Quat start{};
    const Quat end = Quat::fromAxisAngle(kAxisZ, quarterTurn());

    EXPECT_TRUE(isSameRotation(nx::slerp(start, end, 0), start));
    EXPECT_TRUE(isSameRotation(nx::slerp(start, end, 1), end));
    EXPECT_TRUE(
        isSameRotation(nx::slerp(start, end, 0.5F), Quat::fromAxisAngle(kAxisZ, nx::toRadians(45_deg))));
    EXPECT_TRUE(
        isSameRotation(nx::slerp(start, end, 1.0F / 3), Quat::fromAxisAngle(kAxisZ, nx::toRadians(30_deg))));
}

TEST(QuaternionTest, SlerpTakesTheShorterArc) {
    const Quat start{};
    const Quat end = Quat::fromAxisAngle(kAxisZ, quarterTurn());
    // -end is the same rotation; the path still turns 45 degrees, not 135, at the midpoint.
    EXPECT_TRUE(
        isSameRotation(nx::slerp(start, -end, 0.5F), Quat::fromAxisAngle(kAxisZ, nx::toRadians(45_deg))));
}

TEST(QuaternionTest, SlerpOfNearlyEqualRotationsStaysUnit) {
    const Quat start = Quat::fromAxisAngle(kAxisZ, nx::toRadians(10_deg));
    const Quat end = Quat::fromAxisAngle(kAxisZ, nx::toRadians(10.01_deg));
    const Quat middle = nx::slerp(start, end, 0.5F);
    EXPECT_TRUE(isNear(std::sqrt(nx::dot(middle, middle)), 1.0F));
    EXPECT_TRUE(isSameRotation(middle, Quat::fromAxisAngle(kAxisZ, nx::toRadians(10.005_deg))));
}

TEST(QuaternionTest, SlerpOutsideTheUnitIntervalIsAViolation) {
    const nx::test::ContractRecorder recorder;
    std::ignore = nx::slerp(Quat{}, Quat{}, 1.5F);
    EXPECT_EQ(nx::test::ContractRecorder::count(), 1);
}

TEST(QuaternionTest, RotationsAboutTheAxesAreTheEulerAnglesInRadians) {
    // A bone's X, Y and Z angles are the roll, the pitch and the yaw of EulerAngles.
    const Quat bone =
        Quat::fromRotations(nx::toRadians(5_deg), nx::toRadians(20_deg), nx::toRadians(-35_deg));
    const Quat euler = Quat::fromEulerAngles(EulerAngles{.pitch = 20_deg, .yaw = -35_deg, .roll = 5_deg});
    EXPECT_NEAR(bone.x, euler.x, 1e-6F);
    EXPECT_NEAR(bone.y, euler.y, 1e-6F);
    EXPECT_NEAR(bone.z, euler.z, 1e-6F);
    EXPECT_NEAR(bone.w, euler.w, 1e-6F);
    EXPECT_EQ(Quat::fromRotations(nx::Radians{0.0F}, nx::Radians{0.0F}, nx::Radians{0.0F}), Quat{});
}

} // namespace
