// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// How the attacks that hurt an entity's body knock it back: the push of a hit, and how its walk is held
// back, then slowed, and recovers.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import cs.shared;

namespace {

constexpr nx::TickRate kRate{100};
constexpr float kTolerance = 1e-4F;

/// A knockback of 4 units a second for each point of damage, up to 300, its walk held back a tenth of a
/// second, then at half its speed, back to the whole over half a second.
constexpr cs::EntityKnockback kRules{.pushPerDamage = nx::UnitsPerSecond{4.0F},
                                     .mostPushSpeed = nx::UnitsPerSecond{300.0F},
                                     .staggerTime = nx::Seconds{0.1F},
                                     .slowShare = 0.5F,
                                     .slowRecovery = nx::Seconds{0.5F}};

TEST(EntityKnockbackTest, AHitPushesTheBodyAcrossTheWayTheAttackWentByWhatItDealtItsFallKept) {
    // A shot going down and away along x, 30 points dealt: 120 units a second across, along x alone.
    const nx::Vec3 shot = nx::normalized(nx::Vec3{.x = 3.0F, .z = -1.0F});
    const nx::Vec3 pushed = cs::pushedBy(kRules, {.z = -50.0F}, shot, nx::Damage{30.0F});
    EXPECT_NEAR(pushed.x, 120.0F, kTolerance);
    EXPECT_NEAR(pushed.y, 0.0F, kTolerance);
    EXPECT_FLOAT_EQ(pushed.z, -50.0F) << "its fall its own";
    // Walking at it at 100, the push takes 100 of it away first.
    const nx::Vec3 walking = cs::pushedBy(kRules, {.x = -100.0F}, shot, nx::Damage{30.0F});
    EXPECT_NEAR(walking.x, 20.0F, kTolerance);
}

TEST(EntityKnockbackTest, HitsPushNoFasterThanTheMostButNeverSlowDownWhatGoesFaster) {
    const nx::Vec3 away{.y = 1.0F};
    const nx::Vec3 headShot = cs::pushedBy(kRules, {}, away, nx::Damage{128.0F});
    EXPECT_NEAR(headShot.y, 300.0F, kTolerance) << "512 held to the most";
    nx::Vec3 burst{};
    for (int hit = 0; hit < 10; ++hit) {
        burst = cs::pushedBy(kRules, burst, away, nx::Damage{32.0F});
    }
    EXPECT_NEAR(nx::length(burst), 300.0F, kTolerance) << "a burst no more than one hit may";
    const nx::Vec3 falling = cs::pushedBy(kRules, {.x = 500.0F}, away, nx::Damage{32.0F});
    EXPECT_NEAR(nx::length(nx::Vec3{.x = falling.x, .y = falling.y}), 500.0F, kTolerance)
        << "turned, as fast as it went";
    EXPECT_GT(falling.y, 0.0F);
}

TEST(EntityKnockbackTest, AHitStraightDownOrThatDealtNothingPushesNothing) {
    const nx::Vec3 velocity{.x = 10.0F, .y = 20.0F, .z = 30.0F};
    EXPECT_EQ(cs::pushedBy(kRules, velocity, {.z = -1.0F}, nx::Damage{50.0F}), velocity);
    EXPECT_EQ(cs::pushedBy(kRules, velocity, {.x = 1.0F}, nx::Damage{}), velocity);
}

TEST(EntityKnockbackTest, AHitBodyIsHeldBackThenWalksSlowerBackToItsWholeSpeed) {
    const nx::GameTime hit = nx::GameTime{}.after(nx::Seconds{1.0F}, kRate);
    const cs::KnockedBack knocked{.hitAt = hit};
    EXPECT_FLOAT_EQ(cs::walkShareOf(kRules, {}, hit, kRate), 1.0F) << "never hit";
    EXPECT_FLOAT_EQ(cs::walkShareOf(kRules, knocked, hit, kRate), 0.0F) << "held back as it is hit";
    EXPECT_FLOAT_EQ(cs::walkShareOf(kRules, knocked, hit.after(nx::Seconds{0.09F}, kRate), kRate), 0.0F);
    EXPECT_NEAR(cs::walkShareOf(kRules, knocked, hit.after(nx::Seconds{0.1F}, kRate), kRate), 0.5F,
                kTolerance);
    EXPECT_NEAR(cs::walkShareOf(kRules, knocked, hit.after(nx::Seconds{0.35F}, kRate), kRate), 0.75F,
                kTolerance);
    EXPECT_FLOAT_EQ(cs::walkShareOf(kRules, knocked, hit.after(nx::Seconds{0.6F}, kRate), kRate), 1.0F);
    EXPECT_FLOAT_EQ(cs::walkShareOf(kRules, knocked, hit.after(nx::Seconds{5.0F}, kRate), kRate), 1.0F);
    const cs::MoveIntent walked = cs::walkedBy(
        kRules, knocked, {.forward = nx::UnitsPerSecond{200.0F}, .side = nx::UnitsPerSecond{-100.0F}},
        hit.after(nx::Seconds{0.35F}, kRate), kRate);
    EXPECT_NEAR(walked.forward.value(), 150.0F, kTolerance);
    EXPECT_NEAR(walked.side.value(), -75.0F, kTolerance);
}

TEST(EntityKnockbackTest, ABodyOfNoSlowRecoveryWalksWhollyOnceItsStaggerIsOver) {
    cs::EntityKnockback rules = kRules;
    rules.slowRecovery = nx::Seconds{};
    const nx::GameTime hit{};
    const cs::KnockedBack knocked{.hitAt = hit};
    EXPECT_FLOAT_EQ(cs::walkShareOf(rules, knocked, hit, kRate), 0.0F);
    EXPECT_FLOAT_EQ(cs::walkShareOf(rules, knocked, hit.after(nx::Seconds{0.1F}, kRate), kRate), 1.0F);
}

TEST(EntityKnockbackTest, RulesOfANegativeOrNonfiniteValueNoMostPushOrASlowShareOutOfItsRangeAreInvalid) {
    EXPECT_TRUE(cs::isValid(kRules));
    const std::array<cs::EntityKnockback, 6> invalid{
        cs::EntityKnockback{kRules.pushPerDamage * -1.0F, kRules.mostPushSpeed, kRules.staggerTime,
                            kRules.slowShare, kRules.slowRecovery},
        cs::EntityKnockback{kRules.pushPerDamage, nx::UnitsPerSecond{}, kRules.staggerTime, kRules.slowShare,
                            kRules.slowRecovery},
        cs::EntityKnockback{kRules.pushPerDamage, kRules.mostPushSpeed,
                            nx::Seconds{std::numeric_limits<float>::infinity()}, kRules.slowShare,
                            kRules.slowRecovery},
        cs::EntityKnockback{kRules.pushPerDamage, kRules.mostPushSpeed, kRules.staggerTime, 0.0F,
                            kRules.slowRecovery},
        cs::EntityKnockback{kRules.pushPerDamage, kRules.mostPushSpeed, kRules.staggerTime, 1.5F,
                            kRules.slowRecovery},
        cs::EntityKnockback{kRules.pushPerDamage, kRules.mostPushSpeed, kRules.staggerTime, kRules.slowShare,
                            nx::Seconds{-1.0F}},
    };
    for (const cs::EntityKnockback& rules : invalid) {
        EXPECT_FALSE(cs::isValid(rules));
    }
}

} // namespace
