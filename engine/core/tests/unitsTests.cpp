// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;

using namespace nx::literals;

namespace {

constexpr float kAngleTolerance = 1e-5F;

} // namespace

TEST(Units, LiteralsProduceTypedQuantities) {
    static_assert(std::same_as<decltype(64_hu), nx::HammerUnits>);
    static_assert(std::same_as<decltype(250.0_ups), nx::UnitsPerSecond>);
    static_assert(std::same_as<decltype(5000_hp), nx::HitPoints>);
    static_assert(std::same_as<decltype(100_ap), nx::ArmorPoints>);
    static_assert(std::same_as<decltype(36_dmg), nx::Damage>);
    static_assert(std::same_as<decltype(0.75_x), nx::Scale>);
    static_assert(std::same_as<decltype(90_deg), nx::Degrees>);
    EXPECT_EQ((5000_hp).value(), 5000.0F);
}

TEST(Units, SpeedTimesTimeIsDistance) {
    constexpr auto distance = 250_ups * nx::Seconds{2.0F};

    static_assert(std::same_as<decltype(distance), const nx::HammerUnits>);
    EXPECT_EQ(distance, 500_hu);
    EXPECT_EQ(nx::Seconds{2.0F} * 250_ups, 500_hu);
}

TEST(Units, DistanceOverTimeIsSpeed) {
    EXPECT_EQ(320_hu / nx::Seconds{1.0F}, 320_ups);
}

TEST(Units, DistanceOverSpeedIsTime) {
    EXPECT_EQ(500_hu / 250_ups, nx::Seconds{2.0F});
}

TEST(Units, DamageReducesHealthAndArmor) {
    EXPECT_EQ(100_hp - 36_dmg, 64_hp);
    EXPECT_EQ(100_ap - 18_dmg, 82_ap);
}

TEST(Units, HealthIsFractionalLikeTheOriginal) {
    EXPECT_EQ(100_hp - 0.5_dmg, 99.5_hp);
}

TEST(Units, HealthHasNoArtificialUpperLimit) {
    constexpr auto bossHealth = 100000_hp;

    EXPECT_EQ(bossHealth - 1_dmg, 99999_hp);
    // Default float formatting is the shortest round-trip form ("1e+05hp"); displays use a precision.
    EXPECT_EQ(std::format("{:.0f}", bossHealth), "100000hp");
}

TEST(Units, ScalesApplyToDamageAndSpeed) {
    EXPECT_EQ(36_dmg * 4_x, 144_dmg);
    EXPECT_EQ(0.5_x * 250_ups, 125_ups);
}

TEST(Units, DegreesAndRadiansConvertExplicitly) {
    static_assert(!std::is_convertible_v<nx::Degrees, nx::Radians>);

    EXPECT_NEAR(nx::toRadians(180_deg).value(), std::numbers::pi_v<float>, kAngleTolerance);
    EXPECT_NEAR(nx::toDegrees(nx::toRadians(90_deg)).value(), 90.0F, kAngleTolerance);
}
