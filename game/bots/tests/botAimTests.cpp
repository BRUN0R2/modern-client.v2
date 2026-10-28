// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// How a bot's view turns and its aim errs (docs/10, section 11): the spring of UpdateLookAngles, the
// error, focus and settling of SetAimOffset and UpdateAimOffset, and Upkeep's lead.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import cs.bots;

namespace {

/// A command of the default tick rate.
constexpr nx::Seconds kCommand{0.01F};
/// Commands enough for a spring to bring a view half a right angle round: two seconds.
constexpr std::uint32_t kSettlingCommands = 200;
/// A range the tests aim at.
constexpr nx::HammerUnits kRange{1000.0F};
/// Draws of an aim's error the tests make.
constexpr std::uint32_t kDraws = 100;

[[nodiscard]] auto repositoryRules() -> cs::BotRules {
    auto rules = cs::readBotRules(
        nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "bots.toml")
            .value_or(std::string{}),
        "bots.toml");
    EXPECT_TRUE(rules.has_value());
    return std::move(rules).value_or(cs::BotRules{});
}

[[nodiscard]] auto at(float seconds) -> nx::GameTime {
    return nx::GameTime{}.after(nx::Seconds{seconds}, nx::kDefaultTickRate);
}

TEST(BotAimTest, AViewTurnsBySpringBoundByItsAccelerationThenSetsItsYawOnceNear) {
    const cs::BotRules rules = repositoryRules();
    cs::BotViewTurn turn{};
    const nx::EulerAngles desired{.yaw = nx::Degrees{45.0F}};
    // 300 a second a second times 45 degrees off, bound to 3000: 30 degrees a second after a command.
    nx::EulerAngles view = cs::turnView({}, desired, turn, rules.look.attacking, rules.look, kCommand);
    EXPECT_FLOAT_EQ(turn.yaw.value(), 30.0F);
    EXPECT_FLOAT_EQ(view.yaw.value(), 0.3F);
    for (std::uint32_t command = 0; command < kSettlingCommands && turn.yaw != nx::DegreesPerSecond{};
         ++command) {
        view = cs::turnView(view, desired, turn, rules.look.attacking, rules.look, kCommand);
    }
    EXPECT_EQ(view.yaw, desired.yaw) << "set once a degree off";
    EXPECT_EQ(turn.yaw, nx::DegreesPerSecond{});
}

TEST(BotAimTest, AViewTurnsTheShortWayRoundAndHoldsItsPitchWithinItsLimit) {
    const cs::BotRules rules = repositoryRules();
    cs::BotViewTurn turn{};
    const nx::EulerAngles turned = cs::turnView({.yaw = nx::Degrees{170.0F}}, {.yaw = nx::Degrees{-170.0F}},
                                                turn, rules.look.roaming, rules.look, kCommand);
    EXPECT_GT(turn.yaw.value(), 0.0F) << "through 180";
    EXPECT_GT(turned.yaw.value(), 170.0F);
    cs::BotViewTurn falling{.pitch = nx::DegreesPerSecond{1000.0F}};
    const nx::EulerAngles low = cs::turnView({.pitch = nx::Degrees{88.0F}}, {.pitch = nx::Degrees{90.0F}},
                                             falling, rules.look.roaming, rules.look, kCommand);
    EXPECT_EQ(low.pitch, rules.look.pitchLimit);
}

TEST(BotAimTest, AViewTurnsFasterThanASpeedAboutEitherAxis) {
    EXPECT_FALSE(
        cs::isTurningFaster({.pitch = nx::DegreesPerSecond{-99.0F}, .yaw = nx::DegreesPerSecond{99.0F}},
                            nx::DegreesPerSecond{100.0F}));
    EXPECT_TRUE(cs::isTurningFaster({.pitch = nx::DegreesPerSecond{-100.0F}}, nx::DegreesPerSecond{100.0F}));
    EXPECT_TRUE(cs::isTurningFaster({.yaw = nx::DegreesPerSecond{150.0F}}, nx::DegreesPerSecond{100.0F}));
}

TEST(BotAimTest, AnAimErrsByTheSkillItLacksAndFocusesAsItsViewHoldsSteady) {
    const cs::BotRules rules = repositoryRules();
    nx::Random random{1, "aim"};
    // The worst bot, turning: a tenth of the range at most on each axis.
    const auto boundOf = [&](cs::BotSkill skill, bool isTurning, nx::GameTime now, cs::BotAimError& error) {
        float most = 0.0F;
        for (std::uint32_t draw = 0; draw < kDraws; ++draw) {
            error.nextGoalAt = now;
            cs::settleAimError(error,
                               {.skill = skill,
                                .range = kRange,
                                .isTurning = isTurning,
                                .now = now,
                                .rate = nx::kDefaultTickRate,
                                .duration = kCommand},
                               rules.aim, random);
            most = std::max({most, std::abs(error.goal.x), std::abs(error.goal.y), std::abs(error.goal.z)});
        }
        return most;
    };
    cs::BotAimError turning{};
    const float worst = boundOf(cs::BotSkill{0.0F}, true, at(1.0F), turning);
    EXPECT_LE(worst, 100.0F);
    EXPECT_GT(worst, 50.0F);
    EXPECT_EQ(turning.steadySince, at(1.0F)) << "its focus starts again";
    // Steady for ten seconds: its focus makes it 0.75 good, a quarter of that error left.
    const float focused = boundOf(cs::BotSkill{0.0F}, false, at(11.0F), turning);
    EXPECT_LE(focused, 25.0F);
    EXPECT_GT(focused, 12.5F);
    cs::BotAimError best{};
    EXPECT_EQ(boundOf(cs::BotSkill{1.0F}, true, at(1.0F), best), 0.0F) << "the best never errs";
}

TEST(BotAimTest, AnAimMovesATenthOfTheWayToItsErrorEachThirtiethOfASecondAndDrawsTheNextLater) {
    const cs::BotRules rules = repositoryRules();
    nx::Random random{2, "aim"};
    cs::BotAimError error{.goal = {.x = 30.0F}, .nextGoalAt = at(5.0F)};
    cs::settleAimError(error,
                       {.skill = cs::BotSkill{0.0F},
                        .range = kRange,
                        .now = at(1.0F),
                        .rate = nx::kDefaultTickRate,
                        .duration = nx::Seconds{1.0F / 30.0F}},
                       rules.aim, random);
    EXPECT_NEAR(error.offset.x, 3.0F, 1e-4F);
    EXPECT_EQ(error.goal, (nx::Vec3{.x = 30.0F})) << "not due yet";
    cs::settleAimError(error,
                       {.skill = cs::BotSkill{0.0F},
                        .range = kRange,
                        .now = at(5.0F),
                        .rate = nx::kDefaultTickRate,
                        .duration = kCommand},
                       rules.aim, random);
    EXPECT_NE(error.goal, (nx::Vec3{.x = 30.0F}));
    EXPECT_GE(error.nextGoalAt, at(5.25F));
    EXPECT_LE(error.nextGoalAt, at(6.0F));
}

TEST(BotAimTest, ABotAboveHalfASkillAimsAheadOfAMovingEnemy) {
    const cs::BotRules rules = repositoryRules();
    const nx::Vec3 spot{.x = 500.0F};
    const nx::Vec3 enemyVelocity{.y = 250.0F};
    const nx::Vec3 velocity{.y = -50.0F};
    EXPECT_EQ(cs::leadSpot(spot, enemyVelocity, velocity, cs::BotSkill{0.5F}, rules.aim), spot);
    const nx::Vec3 led = cs::leadSpot(spot, enemyVelocity, velocity, cs::BotSkill{0.75F}, rules.aim);
    EXPECT_EQ(led.x, spot.x);
    EXPECT_NEAR(led.y, 30.0F, 1e-4F) << "their velocities apart over a tenth of a second";
}

} // namespace
