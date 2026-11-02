// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.math;

namespace {

TEST(EasingTest, SmoothstepGoesFromNoneToAllThroughItsMiddle) {
    static_assert(nx::smoothstep(0.0F) == 0.0F);
    static_assert(nx::smoothstep(0.5F) == 0.5F);
    static_assert(nx::smoothstep(1.0F) == 1.0F);
}

TEST(EasingTest, SmoothstepIsItsCubic) {
    EXPECT_FLOAT_EQ(nx::smoothstep(0.25F), 0.15625F);
    EXPECT_FLOAT_EQ(nx::smoothstep(0.75F), 0.84375F);
}

TEST(EasingTest, SmoothstepOnlyRisesSlowFirstAndLast) {
    constexpr int kSteps = 64;
    float previous = 0.0F;
    for (int step = 1; step <= kSteps; ++step) {
        const float eased = nx::smoothstep(static_cast<float>(step) / static_cast<float>(kSteps));
        EXPECT_GT(eased, previous) << "at step " << step;
        previous = eased;
    }
    constexpr float kEarly = 0.125F;
    EXPECT_LT(nx::smoothstep(kEarly), kEarly) << "behind its share at first";
    EXPECT_LT(1.0F - nx::smoothstep(1.0F - kEarly), kEarly) << "and as little left of it at last";
}

} // namespace
