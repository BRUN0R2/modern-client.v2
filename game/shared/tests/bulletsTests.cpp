// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// A round followed as the original's FireBullets3 follows it: its damage by the distance it went, and
// how far it goes through what it meets by what that is made of.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;
import cs.shared;

namespace {

using cs::BulletHit;
using cs::Shot;
using cs::TextureMaterial;
using cs::WeaponHit;
using nx::Vec3;

constexpr float kTolerance = 1e-3F;

/// An AK-47's round fired along +x from the origin: 36 damage, a range modifier of 0.98, two things it
/// may meet, 7.62 mm's power of 39 and distance of 5000.
[[nodiscard]] auto akShot() -> Shot {
    return Shot{.origin = Vec3{},
                .direction = Vec3{.x = 1.0F},
                .reach = nx::HammerUnits{8192.0F},
                .round = cs::ShotRound{.damage = nx::Damage{36.0F},
                                       .rangeModifier = 0.98F,
                                       .penetration = 2,
                                       .penetrationPower = 39,
                                       .penetrationDistance = nx::HammerUnits{5000.0F}}};
}

/// A way traced by a round: from, and to.
struct Traced {
    Vec3 start{};
    Vec3 end{};
};

/// A tracer that meets what `met` says, in turn, nothing once they are done, recording each way in `traced`.
[[nodiscard]] auto tracerOf(std::vector<WeaponHit> met, std::vector<Traced>& traced) -> cs::WeaponTracer {
    return [met = std::move(met), &traced](nx::collision::Hull, Vec3 start, Vec3 end) {
        const std::size_t index = traced.size();
        traced.push_back(Traced{.start = start, .end = end});
        return index < met.size() ? met[index] : WeaponHit{};
    };
}

/// A hit on the world at `x` along a way of `length` from x = `from`, its texture made of what
/// materialOf says.
[[nodiscard]] auto wallAt(float x, float from, float length) -> WeaponHit {
    return WeaponHit{.trace = {.fraction = (x - from) / length, .end = Vec3{.x = x}}, .isBrushModel = true};
}

/// A material for every hit.
[[nodiscard]] auto madeOf(TextureMaterial material) -> cs::HitMaterial {
    return [material](const WeaponHit&, Vec3, Vec3) {
        return std::optional{material};
    };
}

TEST(BulletsTest, ARoundThatMeetsNothingGoesAsFarAsItsReach) {
    std::vector<Traced> traced;
    const std::vector<BulletHit> hits =
        cs::followRound(akShot(), tracerOf({}, traced), madeOf(TextureMaterial::Concrete));
    EXPECT_TRUE(hits.empty());
    ASSERT_EQ(traced.size(), 1U);
    EXPECT_FLOAT_EQ(traced[0].end.x, 8192.0F);
}

TEST(BulletsTest, ItsDamageIsItsRangeModifiersPowerOfTheDistanceItWent) {
    std::vector<Traced> traced;
    const std::vector<BulletHit> hits = cs::followRound(
        akShot(), tracerOf({wallAt(4096.0F, 0.0F, 8192.0F)}, traced), madeOf(TextureMaterial::Dirt));
    ASSERT_GE(hits.size(), 1U);
    // 36 times 0.98 to the 4096 / 500, cut to a whole number.
    const auto expected =
        static_cast<float>(static_cast<std::int32_t>(36.0F * std::pow(0.98F, 4096.0F / 500.0F)));
    EXPECT_EQ(hits[0].damage.value(), expected);
    EXPECT_TRUE(hits[0].isBrushModel);
}

TEST(BulletsTest, RangeDecayKeepsTheFloatProductBeforeIntegerTruncation) {
    Shot shot = akShot();
    shot.reach = nx::HammerUnits{1000.0F};
    shot.round.damage = nx::Damage{30.0F};
    shot.round.rangeModifier = 0.7F;
    std::vector<Traced> traced;
    const std::vector<BulletHit> hits = cs::followRound(
        shot, tracerOf({wallAt(500.0F, 0.0F, 1000.0F)}, traced), madeOf(TextureMaterial::Dirt));
    ASSERT_EQ(hits.size(), 1U);
    EXPECT_EQ(hits[0].damage.value(), 21.0F)
        << "the float product rounds to 21; promoting its operands would truncate to 20";
}

TEST(BulletsTest, ARoundGoesOnThroughABrushModelAsFarAsItsMaterialLetsIt) {
    std::vector<Traced> traced;
    // Concrete at x = 100 keeps a quarter of 39, 9, and leaves the default half of the damage.
    const std::vector<BulletHit> hits = cs::followRound(
        akShot(), tracerOf({wallAt(100.0F, 0.0F, 8192.0F), wallAt(300.0F, 109.0F, 4046.0F)}, traced),
        madeOf(TextureMaterial::Concrete));
    ASSERT_EQ(hits.size(), 2U);
    ASSERT_EQ(traced.size(), 2U);
    EXPECT_NEAR(traced[1].start.x, 109.0F, kTolerance) << "9 units beyond where it met the wall";
    EXPECT_NEAR(traced[1].end.x, 109.0F + ((8192.0F - 100.0F) * 0.5F), kTolerance) << "half of the way left";
    const auto first = static_cast<std::int32_t>(36.0F * std::pow(0.98F, 100.0F / 500.0F));
    EXPECT_EQ(hits[0].damage.value(), static_cast<float>(first));
    const float travelled = (300.0F - 109.0F);
    const auto second = static_cast<std::int32_t>(
        static_cast<float>(static_cast<std::int32_t>(static_cast<float>(first) * 0.5F)) *
        std::pow(0.98F, travelled / 500.0F));
    EXPECT_EQ(hits[1].damage.value(), static_cast<float>(second));
}

TEST(BulletsTest, MetalStopsMostOfItsWayAndOfItsDamage) {
    std::vector<Traced> traced;
    const std::vector<BulletHit> hits = cs::followRound(
        akShot(), tracerOf({wallAt(100.0F, 0.0F, 8192.0F)}, traced), madeOf(TextureMaterial::Metal));
    ASSERT_EQ(traced.size(), 2U);
    // 39 times 0.15 is 5.85: 5 units on.
    EXPECT_NEAR(traced[1].start.x, 105.0F, kTolerance);
    ASSERT_EQ(hits.size(), 1U);
}

TEST(BulletsTest, ABodyLetsItOn42UnitsWithThreeQuartersOfItsDamageAndWay) {
    std::vector<Traced> traced;
    WeaponHit body{.trace = {.fraction = 100.0F / 8192.0F, .end = Vec3{.x = 100.0F}}, .isBody = true};
    const std::vector<BulletHit> hits = cs::followRound(
        akShot(), tracerOf({body, wallAt(300.0F, 142.0F, 6069.0F)}, traced), madeOf(TextureMaterial::Flesh));
    ASSERT_EQ(traced.size(), 2U);
    EXPECT_NEAR(traced[1].start.x, 142.0F, kTolerance);
    EXPECT_NEAR(traced[1].end.x, 142.0F + ((8192.0F - 100.0F) * 0.75F), kTolerance);
    ASSERT_EQ(hits.size(), 2U);
    EXPECT_FALSE(hits[0].isBrushModel);
    const auto first = static_cast<std::int32_t>(36.0F * std::pow(0.98F, 100.0F / 500.0F));
    const auto carried = static_cast<std::int32_t>(static_cast<float>(first) * 0.75F);
    const auto second =
        static_cast<std::int32_t>(static_cast<float>(carried) * std::pow(0.98F, 158.0F / 500.0F));
    EXPECT_EQ(hits[1].damage.value(), static_cast<float>(second));
}

TEST(BulletsTest, PastItsPenetrationDistanceItGoesThroughNothingMore) {
    std::vector<Traced> traced;
    const std::vector<BulletHit> hits = cs::followRound(
        akShot(), tracerOf({wallAt(6000.0F, 0.0F, 8192.0F)}, traced), madeOf(TextureMaterial::Concrete));
    EXPECT_EQ(hits.size(), 1U);
    EXPECT_EQ(traced.size(), 1U) << "6000 units are past 7.62 mm's 5000";
}

TEST(BulletsTest, ARoundThatMayMeetOneThingStopsAtIt) {
    Shot pistol = akShot();
    pistol.round.penetration = 1;
    std::vector<Traced> traced;
    const std::vector<BulletHit> hits = cs::followRound(
        pistol, tracerOf({wallAt(100.0F, 0.0F, 8192.0F)}, traced), madeOf(TextureMaterial::Concrete));
    EXPECT_EQ(hits.size(), 1U);
    EXPECT_EQ(traced.size(), 1U);
}

} // namespace
