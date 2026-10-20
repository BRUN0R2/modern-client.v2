// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;

#include "contractRecorder.hpp"
#include "mathAssertions.hpp"

namespace {

using nx::Vec3;
using nx::Vec4;
using nx_test::isNear;

TEST(VectorTest, IsAPlainAggregate) {
    static_assert(std::is_trivially_copyable_v<Vec3>);
    static_assert(std::is_trivially_copyable_v<Vec4>);
    static_assert(sizeof(Vec3) == 3 * sizeof(float));
    static_assert(sizeof(Vec4) == 4 * sizeof(float));
    static_assert(Vec3{} == Vec3{.x = 0, .y = 0, .z = 0});
}

TEST(VectorTest, Arithmetic) {
    constexpr Vec3 a{.x = 1, .y = 2, .z = 3};
    constexpr Vec3 b{.x = 4, .y = -5, .z = 0.5F};

    static_assert(a + b == Vec3{.x = 5, .y = -3, .z = 3.5F});
    static_assert(a - b == Vec3{.x = -3, .y = 7, .z = 2.5F});
    static_assert(-a == Vec3{.x = -1, .y = -2, .z = -3});
    static_assert(a * 2 == Vec3{.x = 2, .y = 4, .z = 6});
    static_assert(2 * a == a * 2);
    static_assert(b / 2 == Vec3{.x = 2, .y = -2.5F, .z = 0.25F});

    Vec3 c = a;
    c += b;
    EXPECT_EQ(c, a + b);
    c -= b;
    EXPECT_EQ(c, a);
    c *= 3;
    EXPECT_EQ(c, (Vec3{.x = 3, .y = 6, .z = 9}));
}

TEST(VectorTest, DivisionByZeroIsAViolation) {
    const nx_test::ContractRecorder recorder;
    std::ignore = Vec3{.x = 1} / 0.0F;
    EXPECT_EQ(nx_test::ContractRecorder::count(), 1);
}

TEST(VectorTest, DotAndCrossProducts) {
    constexpr Vec3 x{.x = 1};
    constexpr Vec3 y{.y = 1};
    constexpr Vec3 z{.z = 1};

    static_assert(nx::dot(Vec3{.x = 1, .y = 2, .z = 3}, Vec3{.x = 4, .y = -5, .z = 6}) == 12);
    static_assert(nx::dot(x, y) == 0);
    // Right-handed axes.
    static_assert(nx::cross(x, y) == z);
    static_assert(nx::cross(y, z) == x);
    static_assert(nx::cross(z, x) == y);
    static_assert(nx::cross(y, x) == -z);

    constexpr Vec3 a{.x = 1, .y = 2, .z = 3};
    constexpr Vec3 b{.x = -2, .y = 0.5F, .z = 4};
    EXPECT_EQ(nx::dot(nx::cross(a, b), a), 0.0F);
    EXPECT_EQ(nx::dot(nx::cross(a, b), b), 0.0F);
}

TEST(VectorTest, Length) {
    constexpr Vec3 v{.x = 2, .y = 3, .z = 6};
    static_assert(nx::lengthSquared(v) == 49);
    EXPECT_EQ(nx::length(v), 7.0F);
    EXPECT_EQ(nx::distance(Vec3{.x = 1, .y = 1, .z = 1}, Vec3{.x = 3, .y = 4, .z = 7}), 7.0F);
}

TEST(VectorTest, NormalizedHasUnitLength) {
    EXPECT_EQ(nx::normalized(Vec3{.y = -8}), (Vec3{.y = -1}));
    const Vec3 direction = nx::normalized(Vec3{.x = 1, .y = 2, .z = 3});
    EXPECT_TRUE(isNear(nx::length(direction), 1.0F));
    EXPECT_TRUE(isNear(nx::cross(direction, Vec3{.x = 1, .y = 2, .z = 3}), Vec3{}));
}

TEST(VectorTest, EveryFiniteNonZeroVectorHasADirection) {
    // Regression: the squares of large components overflowed to infinity (and the result was the
    // zero vector), and those of tiny ones vanished (and the result was a false violation).
    const nx_test::ContractRecorder recorder;
    EXPECT_TRUE(isNear(nx::normalized(Vec3{.x = 4e30F, .y = 3e30F}), Vec3{.x = 0.8F, .y = 0.6F}));
    EXPECT_TRUE(isNear(nx::normalized(Vec3{.x = 3e-30F, .z = -4e-30F}), Vec3{.x = 0.6F, .z = -0.8F}));
    EXPECT_EQ(nx::normalized(Vec3{.z = std::numeric_limits<float>::max()}), (Vec3{.z = 1}));
    EXPECT_EQ(nx_test::ContractRecorder::count(), 0);
}

TEST(VectorTest, NormalizingTheZeroVectorIsAViolation) {
    const nx_test::ContractRecorder recorder;
    EXPECT_EQ(nx::normalized(Vec3{}), Vec3{});
    EXPECT_EQ(nx_test::ContractRecorder::count(), 1);
}

TEST(VectorTest, LerpAndComponentBounds) {
    constexpr Vec3 a{.x = 0, .y = 10, .z = -4};
    constexpr Vec3 b{.x = 8, .y = 6, .z = 4};
    static_assert(nx::lerp(a, b, 0) == a);
    static_assert(nx::lerp(a, b, 1) == b);
    static_assert(nx::lerp(a, b, 0.25F) == Vec3{.x = 2, .y = 9, .z = -2});
    static_assert(nx::componentMin(a, b) == Vec3{.x = 0, .y = 6, .z = -4});
    static_assert(nx::componentMax(a, b) == Vec3{.x = 8, .y = 10, .z = 4});
}

TEST(VectorTest, Vec4) {
    constexpr Vec4 a{.x = 1, .y = 2, .z = 3, .w = 4};
    static_assert(nx::dot(a, Vec4{.x = 1, .y = 1, .z = 1, .w = 1}) == 10);
    static_assert(nx::xyz(a) == Vec3{.x = 1, .y = 2, .z = 3});
    static_assert(nx::extend(Vec3{.x = 1, .y = 2, .z = 3}, 1) == Vec4{.x = 1, .y = 2, .z = 3, .w = 1});
}

TEST(VectorTest, Formats) {
    EXPECT_EQ(std::format("{}", Vec3{.x = 1, .y = -2, .z = 0.5F}), "(1 -2 0.5)");
    EXPECT_EQ(std::format("{:.1f}", Vec3{.x = 1, .y = 2, .z = 3}), "(1.0 2.0 3.0)");
}

} // namespace
