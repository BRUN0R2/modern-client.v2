// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;

#include "mathAssertions.hpp"

namespace {

using nx::Mat3;
using nx::Mat3x4;
using nx::Mat4;
using nx::Vec3;
using nx::Vec4;
using nx_test::isNear;

/// Quarter turn about +Z: +X becomes +Y.
constexpr Mat3 kQuarterTurnAboutZ{
    .rows = {Vec3{.x = 0, .y = -1, .z = 0}, Vec3{.x = 1, .y = 0, .z = 0}, Vec3{.x = 0, .y = 0, .z = 1}}};

/// Scales X by 2, Y by 3 and Z by 4.
constexpr Mat3 kScale{.rows = {Vec3{.x = 2}, Vec3{.y = 3}, Vec3{.z = 4}}};

TEST(MatrixTest, DefaultIsTheIdentity) {
    static_assert(std::is_trivially_copyable_v<Mat3>);
    static_assert(std::is_trivially_copyable_v<Mat3x4>);
    static_assert(std::is_trivially_copyable_v<Mat4>);
    static_assert(sizeof(Mat3x4) == 48, "bone palette entry (docs/05, section 9.1)");

    constexpr Vec3 v{.x = 1, .y = 2, .z = 3};
    static_assert(Mat3{} * v == v);
    static_assert(nx::transformPoint(Mat3x4{}, v) == v);
    static_assert(Mat4{} * Vec4{.x = 1, .y = 2, .z = 3, .w = 4} == Vec4{.x = 1, .y = 2, .z = 3, .w = 4});
}

TEST(MatrixTest, Mat3AppliesToColumnVectors) {
    static_assert(kQuarterTurnAboutZ * Vec3{.x = 1} == Vec3{.y = 1});
    static_assert(kQuarterTurnAboutZ * Vec3{.y = 1} == Vec3{.x = -1});
    static_assert(kScale * Vec3{.x = 1, .y = 1, .z = 1} == Vec3{.x = 2, .y = 3, .z = 4});
}

TEST(MatrixTest, ProductAppliesTheRightOperandFirst) {
    constexpr Vec3 v{.x = 1, .y = 1, .z = 1};
    // Scale (2, 3, 4), then rotate: (-3, 2, 4). Rotate (-1, 1, 1), then scale: (-2, 3, 4).
    static_assert((kQuarterTurnAboutZ * kScale) * v == Vec3{.x = -3, .y = 2, .z = 4});
    static_assert((kScale * kQuarterTurnAboutZ) * v == Vec3{.x = -2, .y = 3, .z = 4});
    static_assert(kQuarterTurnAboutZ * Mat3{} == kQuarterTurnAboutZ);
}

TEST(MatrixTest, TransposedSwapsRowsAndColumns) {
    constexpr Mat3 m{
        .rows = {Vec3{.x = 1, .y = 2, .z = 3}, Vec3{.x = 4, .y = 5, .z = 6}, Vec3{.x = 7, .y = 8, .z = 9}}};
    static_assert(nx::transposed(m).rows[0] == Vec3{.x = 1, .y = 4, .z = 7});
    static_assert(nx::transposed(nx::transposed(m)) == m);
    // The transpose of a rotation is its inverse.
    static_assert(nx::transposed(kQuarterTurnAboutZ) * kQuarterTurnAboutZ == Mat3{});
}

TEST(MatrixTest, AffineTransformsPointsAndDirections) {
    constexpr Vec3 translation{.x = 10, .y = 20, .z = 30};
    constexpr Mat3x4 transform = nx::affine(kQuarterTurnAboutZ, translation);

    static_assert(nx::linearPart(transform) == kQuarterTurnAboutZ);
    static_assert(nx::translationPart(transform) == translation);
    static_assert(nx::transformPoint(transform, Vec3{.x = 1}) == Vec3{.x = 10, .y = 21, .z = 30});
    static_assert(nx::transformDirection(transform, Vec3{.x = 1}) == Vec3{.y = 1});
}

TEST(MatrixTest, Mat3x4ProductAppliesTheRightOperandFirst) {
    constexpr Mat3x4 rotate = nx::affine(kQuarterTurnAboutZ, Vec3{});
    constexpr Mat3x4 move = nx::affine(Mat3{}, Vec3{.x = 5});
    constexpr Vec3 origin{};

    // Move to (5, 0, 0), then rotate to (0, 5, 0); or rotate the origin, then move it.
    static_assert(nx::transformPoint(rotate * move, origin) == Vec3{.y = 5});
    static_assert(nx::transformPoint(move * rotate, origin) == Vec3{.x = 5});

    constexpr Vec3 point{.x = 1, .y = 2, .z = 3};
    static_assert(nx::transformPoint(rotate * move, point) ==
                  nx::transformPoint(rotate, nx::transformPoint(move, point)));
}

TEST(MatrixTest, RigidInverseUndoesARigidTransform) {
    const Mat3x4 transform =
        nx::affine(nx::rotationMatrix(nx::EulerAngles{
                       .pitch = nx::Degrees{30}, .yaw = nx::Degrees{-75}, .roll = nx::Degrees{10}}),
                   Vec3{.x = 100, .y = -40, .z = 12.5F});
    const Mat3x4 inverse = nx::rigidInverse(transform);
    constexpr Vec3 point{.x = 3, .y = -7, .z = 11};

    EXPECT_TRUE(isNear(nx::transformPoint(inverse, nx::transformPoint(transform, point)), point, 1e-4F));
    EXPECT_TRUE(isNear(nx::linearPart(inverse * transform), Mat3{}));
    EXPECT_TRUE(isNear(nx::translationPart(transform * inverse), Vec3{}, 1e-4F));
}

TEST(MatrixTest, Mat4) {
    constexpr Mat3x4 transform = nx::affine(kScale, Vec3{.x = 1, .y = 2, .z = 3});
    constexpr Mat4 full = nx::toMat4(transform);
    constexpr Vec3 point{.x = 1, .y = 1, .z = 1};

    static_assert(nx::xyz(full * nx::extend(point, 1)) == nx::transformPoint(transform, point));
    static_assert((full * nx::extend(point, 1)).w == 1);
    static_assert(nx::xyz(full * nx::extend(point, 0)) == nx::transformDirection(transform, point));
    static_assert(nx::toMat4(transform * transform) == full * full);
    static_assert(nx::transposed(nx::transposed(full)) == full);
    static_assert(nx::transposed(full).rows[3] == Vec4{.x = 1, .y = 2, .z = 3, .w = 1});
}

} // namespace
