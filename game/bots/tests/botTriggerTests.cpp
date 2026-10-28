// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// When a bot's attack is down (docs/10, section 11): FireWeaponAtEnemy's aim, attack delay and pauses,
// weighed at each full think and held until the next, and StartRapidFire's attack up and down.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.platform;
import cs.shared;
import cs.bots;

namespace {

/// Ticks of a full think, a tenth of a second.
constexpr std::uint32_t kThinkTicks = 10;
/// Where the enemy the tests aim at stands, ahead along +x.
constexpr nx::Vec3 kAhead{.x = 1000.0F};
/// Ahead and nearer than a gun sprays at.
constexpr nx::Vec3 kNear{.x = 300.0F};

[[nodiscard]] auto repositoryRules() -> cs::BotRules {
    auto rules = cs::readBotRules(
        nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "bots.toml")
            .value_or(std::string{}),
        "bots.toml");
    EXPECT_TRUE(rules.has_value());
    return std::move(rules).value_or(cs::BotRules{});
}

/// A bot's trigger pulled tick by tick from the game's first, at an enemy.
class BotTriggerTest : public ::testing::Test {
protected:
    BotTriggerTest() : enemy{world.spawn()} {}

    /// Whether its attack is down in the command of tick `tick`, aiming at `spot` with `slot`.
    auto pull(std::uint32_t tick, cs::BotSkill skill, std::optional<nx::Vec3> spot = kAhead,
              std::optional<cs::WeaponSlot> slot = cs::WeaponSlot::Secondary, nx::Seconds attackDelay = {})
        -> bool {
        return cs::pullTrigger(trigger,
                               {.pursued = spot ? std::optional{enemy} : std::nullopt,
                                .aimSpot = spot,
                                .heldSlot = slot,
                                .skill = skill,
                                .attackDelay = attackDelay,
                                .now = nx::GameTime{}.ticksLater(tick),
                                .rate = nx::kDefaultTickRate},
                               rules.fire, random);
    }

    cs::BotRules rules = repositoryRules();
    nx::ecs::World world;
    nx::ecs::Entity enemy;
    cs::BotTrigger trigger{};
    nx::Random random{3, "trigger"};
};

TEST(BotTriggerAimTest, AViewIsAimedWithinTheAngleTheHalfWidthSpansAtTheRange) {
    constexpr nx::HammerUnits kHalfWidth{32.0F};
    EXPECT_TRUE(cs::isAimedAt({}, {}, {.x = 1000.0F}, kHalfWidth));
    EXPECT_TRUE(cs::isAimedAt({}, {}, {.x = 1000.0F, .y = 30.0F}, kHalfWidth));
    EXPECT_FALSE(cs::isAimedAt({}, {}, {.x = 1000.0F, .y = 40.0F}, kHalfWidth));
    EXPECT_TRUE(cs::isAimedAt({}, {}, {.x = 100.0F, .y = 30.0F}, kHalfWidth)) << "wider near";
    EXPECT_FALSE(cs::isAimedAt({}, nx::Degrees{180.0F}, {.x = 1000.0F}, kHalfWidth));
    EXPECT_TRUE(cs::isAimedAt({}, nx::Degrees{90.0F}, {.z = 100.0F}, kHalfWidth)) << "straight above";
}

TEST_F(BotTriggerTest, ARapidPistolIsPressedEveryOtherCommandFromItsFirstFullThink) {
    const cs::BotSkill expert{0.9F};
    std::vector<bool> downs;
    for (std::uint32_t tick = 1; tick <= kThinkTicks + 2; ++tick) {
        downs.push_back(pull(tick, expert));
    }
    EXPECT_TRUE(trigger.isRapid);
    EXPECT_EQ(downs, (std::vector<bool>{true, false, true, false, true, false, true, false, true, false, true,
                                        false}))
        << "pressed at each full think, up and down between";
}

TEST_F(BotTriggerTest, APressIsHeldUntilTheNextFullThinkAndAPistolPausesBetween) {
    const cs::BotSkill normal{0.5F};
    for (std::uint32_t tick = 1; tick <= kThinkTicks; ++tick) {
        EXPECT_TRUE(pull(tick, normal)) << tick;
    }
    EXPECT_FALSE(trigger.isRapid);
    // The pistol's pause, 0.15 to 0.4 seconds, a think less: from 0.05 to 0.3 after the press, which
    // a full think at 0.1 or later weighs.
    EXPECT_GE(trigger.nextPressAt, nx::GameTime{}.ticksLater(1 + 5));
    EXPECT_LE(trigger.nextPressAt, nx::GameTime{}.ticksLater(1 + 30));
}

TEST_F(BotTriggerTest, AGunSpraysNearAndPausesFarAboveHalfASkill) {
    const cs::BotSkill hard{0.75F};
    for (std::uint32_t tick = 1; tick <= 3 * kThinkTicks; ++tick) {
        EXPECT_TRUE(pull(tick, hard, kNear, cs::WeaponSlot::Primary)) << "held through its thinks, " << tick;
    }
    trigger = {};
    EXPECT_TRUE(pull(1, hard, kAhead, cs::WeaponSlot::Primary));
    // Beyond 800 units: 0.3 to 0.7 seconds, a think less.
    EXPECT_GE(trigger.nextPressAt, nx::GameTime{}.ticksLater(1 + 20));
    EXPECT_LE(trigger.nextPressAt, nx::GameTime{}.ticksLater(1 + 60));
}

TEST_F(BotTriggerTest, ABotPressesNoSoonerThanItsAttackDelayAfterItTookItsEnemyOn) {
    const cs::BotSkill normal{0.5F};
    const nx::Seconds attackDelay{0.7F};
    std::optional<std::uint32_t> firstPress;
    for (std::uint32_t tick = 1; tick <= 100 && !firstPress; ++tick) {
        if (pull(tick, normal, kAhead, cs::WeaponSlot::Secondary, attackDelay)) {
            firstPress = tick;
        }
    }
    ASSERT_TRUE(firstPress.has_value());
    EXPECT_EQ(*firstPress, 71U) << "its enemy taken on at tick 1, a full think each 10 ticks";
}

TEST_F(BotTriggerTest, AttackingNoneReleasesTheAttackAndStopsFiringAsFastAsItMay) {
    EXPECT_TRUE(pull(1, cs::BotSkill{1.0F}));
    ASSERT_TRUE(trigger.isRapid);
    EXPECT_FALSE(pull(2, cs::BotSkill{1.0F}, std::nullopt));
    EXPECT_FALSE(trigger.isRapid);
    EXPECT_FALSE(trigger.enemy.has_value());
}

TEST_F(BotTriggerTest, ABotPressesOnlyAimedAtWhereItAims) {
    EXPECT_FALSE(pull(1, cs::BotSkill{1.0F}, nx::Vec3{.y = 1000.0F})) << "off its view by a right angle";
}

} // namespace
