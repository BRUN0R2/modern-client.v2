// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;

#include "contractRecorder.hpp"

namespace {

using nx::Aabb;
using nx::Vec3;

/// A standing player's hull in GoldSrc: 32 x 32 x 72 units around the origin.
constexpr Aabb kPlayer{.min = Vec3{.x = -16, .y = -16, .z = -36}, .max = Vec3{.x = 16, .y = 16, .z = 36}};

TEST(AabbTest, AroundACenter) {
    static_assert(Aabb::around(Vec3{}, Vec3{.x = 16, .y = 16, .z = 36}) == kPlayer);
    static_assert(kPlayer.center() == Vec3{});
    static_assert(kPlayer.size() == Vec3{.x = 32, .y = 32, .z = 72});
    static_assert(kPlayer.translated(Vec3{.z = 36}).center() == Vec3{.z = 36});
}

TEST(AabbTest, Validity) {
    static_assert(kPlayer.isValid());
    static_assert(Aabb{}.isValid(), "a point is a valid box");
    static_assert(!Aabb{.min = Vec3{.x = 1}, .max = Vec3{}}.isValid());
}

TEST(AabbTest, ContainsIsClosed) {
    static_assert(kPlayer.contains(Vec3{}));
    static_assert(kPlayer.contains(kPlayer.min));
    static_assert(kPlayer.contains(kPlayer.max));
    static_assert(kPlayer.contains(Vec3{.x = 16, .y = 0, .z = -36}));
    static_assert(!kPlayer.contains(Vec3{.x = 16.5F}));
    static_assert(!kPlayer.contains(Vec3{.z = -37}));
}

TEST(AabbTest, TouchingBoxesIntersect) {
    constexpr Aabb beside = kPlayer.translated(Vec3{.x = 32});
    constexpr Aabb apart = kPlayer.translated(Vec3{.x = 32.5F});
    constexpr Aabb above = kPlayer.translated(Vec3{.x = 10, .z = 50});

    static_assert(kPlayer.intersects(beside) && beside.intersects(kPlayer));
    static_assert(!kPlayer.intersects(apart) && !apart.intersects(kPlayer));
    static_assert(kPlayer.intersects(above));
    static_assert(kPlayer.intersects(kPlayer));
}

TEST(AabbTest, MergedAndIncluding) {
    constexpr Aabb other{.min = Vec3{.x = 0, .y = -40, .z = 0}, .max = Vec3{.x = 50, .y = 0, .z = 10}};
    static_assert(kPlayer.merged(other) ==
                  Aabb{.min = Vec3{.x = -16, .y = -40, .z = -36}, .max = Vec3{.x = 50, .y = 16, .z = 36}});
    static_assert(kPlayer.including(Vec3{.y = 100}) ==
                  Aabb{.min = kPlayer.min, .max = Vec3{.x = 16, .y = 100, .z = 36}});
    static_assert(kPlayer.including(Vec3{}) == kPlayer);
}

TEST(AabbTest, ExpandedIsTheMinkowskiSum) {
    constexpr Vec3 halfHull{.x = 16, .y = 16, .z = 36};
    constexpr Aabb crate = Aabb::around(Vec3{.x = 100}, Vec3{.x = 8, .y = 8, .z = 8});
    constexpr Aabb grown = crate.expanded(halfHull);

    static_assert(grown == Aabb::around(Vec3{.x = 100}, Vec3{.x = 24, .y = 24, .z = 44}));
    // The player at `origin` touches the crate exactly when `origin` is inside the grown box.
    constexpr Vec3 touching{.x = 76};
    static_assert(grown.contains(touching) && Aabb::around(touching, halfHull).intersects(crate));
    constexpr Vec3 clear{.x = 75};
    static_assert(!grown.contains(clear) && !Aabb::around(clear, halfHull).intersects(crate));
}

TEST(AabbTest, NegativeSizesAreViolations) {
    const nx::test::ContractRecorder recorder;
    std::ignore = kPlayer.expanded(Vec3{.x = -1});
    std::ignore = Aabb::around(Vec3{}, Vec3{.z = -1});
    EXPECT_EQ(nx::test::ContractRecorder::count(), 2);
}

} // namespace
