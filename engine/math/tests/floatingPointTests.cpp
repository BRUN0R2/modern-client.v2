// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Guards of the deterministic floating point policy (docs/07-simulacao-e-fisica.md, section 6).
// The worker-count side of determinism is covered by tests/integration/determinismTests.cpp.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;

namespace {

using nx::Vec3;

/// Read at run time, so the compiler cannot fold the expressions under test.
[[nodiscard]] auto runtimeEpsilon() -> float {
    static volatile float epsilon = std::numeric_limits<float>::epsilon();
    return epsilon;
}

TEST(FloatingPointTest, MultiplyAddIsNeverFused) {
    // a * a == 1 + 2^-22 + 2^-46, which rounds to 1 + 2^-22: the sum with -(1 + 2^-22) is exactly 0
    // when the product is rounded first, and 2^-46 when a fused multiply-add skips that rounding.
    const float epsilon = runtimeEpsilon();
    const float a = 1.0F + epsilon;
    const float c = -(1.0F + (epsilon + epsilon));
    EXPECT_EQ(nx::dot(Vec3{.x = a, .y = 1.0F}, Vec3{.x = a, .y = c}), 0.0F);
}

TEST(FloatingPointTest, TrigonometryIsTheSameOnEveryThread) {
    // Every thread starts with the default floating point environment, and nothing in the engine
    // changes it: the same inputs give the same bits anywhere.
    const auto sample = [] {
        std::vector<float> bits;
        for (int step = 0; step < 360; ++step) {
            const auto angle = nx::Degrees{static_cast<float>(step) * 1.37F};
            const nx::AngleVectors axes =
                nx::angleVectors(nx::EulerAngles{.pitch = angle, .yaw = angle * 2.0F, .roll = -angle});
            bits.insert(bits.end(),
                        {axes.forward.x, axes.forward.y, axes.forward.z, axes.right.x, axes.up.z});
            bits.push_back(nx::vectorAngles(axes.forward).yaw.value());
        }
        return bits;
    };

    const std::vector<float> reference = sample();
    std::vector<float> fromThread;
    std::thread worker{[&fromThread, &sample] { fromThread = sample(); }};
    worker.join();
    ASSERT_EQ(fromThread.size(), reference.size());
    EXPECT_EQ(std::memcmp(fromThread.data(), reference.data(), reference.size() * sizeof(float)), 0);
}

} // namespace
