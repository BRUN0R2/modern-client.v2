// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.render;

namespace {

using nx::Vec3;
using nx::render::ProbeBlend;
using nx::render::probeBlendOf;

/// What a point reflects of probes that each show `values[probe]`: the blend's share of each.
[[nodiscard]] auto reflectedAt(std::span<const Vec3> probes, std::span<const float> values, Vec3 point)
    -> float {
    const std::optional<ProbeBlend> blend = probeBlendOf(probes, point);
    EXPECT_TRUE(blend.has_value());
    if (!blend) {
        return 0.0F;
    }
    return (values[blend->first] * (1.0F - blend->secondShare)) +
           (values[blend->second] * blend->secondShare);
}

TEST(ProbeBlendTest, NoProbesNoBlend) {
    EXPECT_FALSE(probeBlendOf({}, Vec3{}).has_value());
}

TEST(ProbeBlendTest, ASingleProbeIsReflectedWhole) {
    const std::array probes{Vec3{.x = 100.0F}};
    EXPECT_EQ(probeBlendOf(probes, Vec3{.y = 50.0F}),
              (ProbeBlend{.first = 0, .second = 0, .secondShare = 0.0F}));
}

TEST(ProbeBlendTest, TwoProbesShareByNearness) {
    const std::array probes{Vec3{}, Vec3{.x = 400.0F}};
    const auto halfway = probeBlendOf(probes, Vec3{.x = 200.0F});
    ASSERT_TRUE(halfway.has_value());
    EXPECT_NEAR(halfway->secondShare, 0.5F, 1e-6F);
    const auto nearTheFirst = probeBlendOf(probes, Vec3{.x = 100.0F});
    ASSERT_TRUE(nearTheFirst.has_value());
    EXPECT_EQ(nearTheFirst->first, 0U);
    EXPECT_NEAR(nearTheFirst->secondShare, 0.25F, 1e-6F) << "a third as near, a third of the weight";
}

TEST(ProbeBlendTest, WhereAProbeStandsItIsReflectedAlmostWhole) {
    const std::array probes{Vec3{}, Vec3{.x = 300.0F}, Vec3{.x = 600.0F, .y = 100.0F}};
    const auto blend = probeBlendOf(probes, Vec3{.x = 300.0F});
    ASSERT_TRUE(blend.has_value());
    EXPECT_EQ(blend->first, 1U);
    EXPECT_LT(blend->secondShare, 0.01F);
}

TEST(ProbeBlendTest, AThirdProbeOvertakingTheSecondTakesNothingAtOnce) {
    // The second and third nearest as near: the second's share is nothing, so the swap changes nothing.
    const std::array probes{Vec3{}, Vec3{.x = 500.0F}, Vec3{.y = 500.0F}};
    const auto blend = probeBlendOf(probes, Vec3{.x = 10.0F, .y = 10.0F});
    ASSERT_TRUE(blend.has_value());
    EXPECT_EQ(blend->first, 0U);
    EXPECT_NEAR(blend->secondShare, 0.0F, 1e-6F);
}

TEST(ProbeBlendTest, WalkingPastProbesTheReflectionNeverJumps) {
    // Probes showing light and dark in turn, scattered, as far apart as a map's; a walk past all of them
    // in steps of a unit: the nearest-probe way jumped from 0 to 1 as it passed each middle.
    const std::array probes{Vec3{.x = 0.0F, .y = 40.0F},    Vec3{.x = 384.0F, .y = -60.0F},
                            Vec3{.x = 768.0F, .y = 20.0F},  Vec3{.x = 1152.0F, .y = -30.0F},
                            Vec3{.x = 1536.0F, .y = 70.0F}, Vec3{.x = 700.0F, .y = 500.0F}};
    const std::array values{0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 1.0F};
    constexpr float kStep = 1.0F;
    constexpr float kMostChangeAStep = 0.02F;
    float previous = reflectedAt(probes, values, Vec3{.x = -200.0F});
    float mostChange = 0.0F;
    for (float x = -200.0F + kStep; x <= 1800.0F; x += kStep) {
        const float reflected = reflectedAt(probes, values, Vec3{.x = x});
        mostChange = std::max(mostChange, std::abs(reflected - previous));
        previous = reflected;
    }
    EXPECT_LT(mostChange, kMostChangeAStep);
}

} // namespace
