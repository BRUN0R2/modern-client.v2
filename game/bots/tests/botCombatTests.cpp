// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// How a bot moves as it fights (docs/10, section 11): AttackState's dodge, drawn as a fight begins by its
// skill, its sliding while its enemy faces it, its first-dodge jump, and the combat range it keeps.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import cs.shared;
import cs.bots;

namespace {

constexpr nx::UnitsPerSecond kRunning{250.0F};
constexpr nx::Seconds kTick{0.01F};

[[nodiscard]] auto repositoryRules() -> cs::BotCombatRules {
    auto rules = cs::readBotRules(
        nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "bots.toml")
            .value_or(std::string{}),
        "bots.toml");
    EXPECT_TRUE(rules.has_value());
    return std::move(rules).value_or(cs::BotRules{}).combat;
}

/// The rules of the repository, the bot always dodging.
[[nodiscard]] auto alwaysDodging() -> cs::BotCombatRules {
    auto rules = repositoryRules();
    rules.dodgeChance = 2.0F;
    return rules;
}

/// A fight of a bot at the origin, its enemy `range` ahead along +x looking back at it (yaw 180).
[[nodiscard]] auto fightAt(float range, cs::BotSkill skill, nx::GameTime now = {}) -> cs::BotFight {
    return {.origin = {},
            .enemyOrigin = {.x = range},
            .enemyView = {.yaw = nx::Degrees{180.0F}},
            .skill = skill,
            .maxSpeed = kRunning,
            .now = now,
            .rate = nx::kDefaultTickRate};
}

[[nodiscard]] auto after(int ticks) -> nx::GameTime {
    return nx::GameTime{}.after(kTick * static_cast<float>(ticks), nx::kDefaultTickRate);
}

TEST(BotCombatTest, ABotSpawnsWithACombatRangeDrawnWithinItsRules) {
    const cs::BotCombatRules rules = repositoryRules();
    nx::Random random{1, "combat tests"};
    for (int spawn = 0; spawn < 64; ++spawn) {
        const cs::BotCombat combat = cs::spawnedCombat(rules, random);
        EXPECT_GE(combat.range, nx::HammerUnits{325.0F});
        EXPECT_LE(combat.range, nx::HammerUnits{425.0F});
        EXPECT_FALSE(combat.isFighting);
    }
}

TEST(BotCombatTest, AnEnemyFacesABotWhenItsViewPointsTowardItRatherThanAway) {
    EXPECT_TRUE(cs::isFacing({.x = 300.0F}, {.yaw = nx::Degrees{180.0F}}, {}));
    EXPECT_TRUE(cs::isFacing({.x = 300.0F}, {.yaw = nx::Degrees{120.0F}}, {})) << "toward it, if not at it";
    EXPECT_FALSE(cs::isFacing({.x = 300.0F}, {}, {})) << "its back to it";
}

TEST(BotCombatTest, ABotOfNoSkillNeverDodgesAndASureOneDodgesSideToSideWhileItsEnemyFacesIt) {
    const cs::BotCombatRules rules = alwaysDodging();
    nx::Random random{1, "combat tests"};
    cs::BotCombat still = cs::spawnedCombat(rules, random);
    for (int tick = 0; tick < 500; ++tick) {
        const cs::BotFightMove move =
            cs::fightMove(still, fightAt(375.0F, cs::BotSkill{0.0F}, after(tick)), repositoryRules(), random);
        ASSERT_EQ(move.side, nx::UnitsPerSecond{}) << "no skill, no dodge";
        ASSERT_FALSE(move.jumps);
    }

    cs::BotCombat combat = cs::spawnedCombat(rules, random);
    combat.range = nx::HammerUnits{375.0F};
    std::optional<cs::BotDodge> last;
    int changes = 0;
    bool slidLeft = false;
    bool slidRight = false;
    for (int tick = 0; tick < 1000; ++tick) {
        const cs::BotFightMove move =
            cs::fightMove(combat, fightAt(375.0F, cs::BotSkill{0.6F}, after(tick)), rules, random);
        slidLeft = slidLeft || move.side < nx::UnitsPerSecond{};
        slidRight = slidRight || move.side > nx::UnitsPerSecond{};
        if (last && combat.dodge != *last) {
            ++changes;
        }
        last = combat.dodge;
    }
    EXPECT_TRUE(slidLeft);
    EXPECT_TRUE(slidRight);
    // A way each 0.3 to 1 second, ten seconds long, each other than the last.
    EXPECT_GE(changes, 9);
    EXPECT_LE(changes, 34);

    const cs::BotFightMove away = cs::fightMove(
        combat,
        {.enemyOrigin = {.x = 375.0F}, .skill = cs::BotSkill{0.6F}, .maxSpeed = kRunning, .now = after(1000)},
        rules, random);
    EXPECT_EQ(combat.dodge, cs::BotDodge::SteadyOn) << "its enemy's back to it";
    EXPECT_EQ(away.side, nx::UnitsPerSecond{});
    std::ignore = cs::fightMove(combat, fightAt(2500.0F, cs::BotSkill{0.6F}, after(1001)), rules, random);
    EXPECT_EQ(combat.dodge, cs::BotDodge::SteadyOn) << "beyond the dodge range";
}

TEST(BotCombatTest, OnlyTheFirstDodgeOfAFightJumpsAndOnlyASkilledBotMoving) {
    auto rules = alwaysDodging();
    rules.jumpChance = 1.0F;
    nx::Random random{3, "combat tests"};
    cs::BotCombat combat = cs::spawnedCombat(rules, random);
    combat.range = nx::HammerUnits{375.0F};
    int firstJumps = 0;
    for (int fight = 0; fight < 64; ++fight) {
        auto moving = fightAt(375.0F, cs::BotSkill{0.8F}, after(fight * 1000));
        moving.velocity = {.y = 100.0F};
        std::ignore = cs::fightMove(combat, moving, rules, random);
        firstJumps += combat.dodge == cs::BotDodge::Jump ? 1 : 0;
        for (int tick = 1; tick < 300; ++tick) {
            moving.now = after((fight * 1000) + tick);
            std::ignore = cs::fightMove(combat, moving, rules, random);
            if (combat.dodge == cs::BotDodge::Jump && tick > 100) {
                ADD_FAILURE() << "a later dodge jumps, fight " << fight << ", tick " << tick;
            }
        }
        cs::endFight(combat);
    }
    EXPECT_GT(firstJumps, 0) << "a quarter of first dodges, of four ways";

    for (int fight = 0; fight < 64; ++fight) {
        std::ignore =
            cs::fightMove(combat, fightAt(375.0F, cs::BotSkill{0.8F}, after(fight * 10)), rules, random);
        EXPECT_NE(combat.dodge, cs::BotDodge::Jump) << "standing still";
        cs::endFight(combat);
        auto unskilled = fightAt(375.0F, cs::BotSkill{0.4F}, after((fight * 10) + 5));
        unskilled.velocity = {.y = 100.0F};
        std::ignore = cs::fightMove(combat, unskilled, rules, random);
        EXPECT_NE(combat.dodge, cs::BotDodge::Jump) << "half a skill or less";
        cs::endFight(combat);
    }
}

TEST(BotCombatTest, ALessSkilledBotKeepsItsCombatRangeAndAMoreSkilledOneHoldsItsGround) {
    const cs::BotCombatRules rules = alwaysDodging();
    nx::Random random{5, "combat tests"};
    cs::BotCombat combat = cs::spawnedCombat(rules, random);
    combat.range = nx::HammerUnits{375.0F};
    EXPECT_EQ(cs::fightMove(combat, fightAt(600.0F, cs::BotSkill{0.5F}), rules, random).forward, kRunning)
        << "farther than 375 and 125: ahead";
    EXPECT_EQ(cs::fightMove(combat, fightAt(200.0F, cs::BotSkill{0.5F}), rules, random).forward, -kRunning)
        << "nearer than 375 less 125: back";
    EXPECT_EQ(cs::fightMove(combat, fightAt(400.0F, cs::BotSkill{0.5F}), rules, random).forward,
              nx::UnitsPerSecond{})
        << "within its slack";
    EXPECT_EQ(cs::fightMove(combat, fightAt(600.0F, cs::BotSkill{0.7F}), rules, random).forward,
              nx::UnitsPerSecond{})
        << "from 0.66 of skill it holds its ground";
}

TEST(BotCombatTest, AFightBegunCrouchesAndHoldsByItsAggressionOnlyWithAClearCrouchedSight) {
    auto rules = alwaysDodging();
    rules.nearCrouchChance = 1.0F;
    rules.farCrouchChance = 1.0F;
    nx::Random random{9, "combat tests"};
    cs::BotCombat combat = cs::spawnedCombat(rules, random);
    combat.range = nx::HammerUnits{375.0F};
    const auto begin = [&](cs::BotFight fight) {
        cs::endFight(combat);
        return cs::fightMove(combat, fight, rules, random);
    };
    auto timid = fightAt(600.0F, cs::BotSkill{0.5F});
    timid.seesCrouched = true;
    const cs::BotFightMove holding = begin(timid);
    EXPECT_TRUE(combat.crouches && holding.crouches) << "no aggression, a clear crouched sight";
    EXPECT_EQ(holding.forward, nx::UnitsPerSecond{}) << "holding, it keeps no range";
    EXPECT_EQ(holding.side, nx::UnitsPerSecond{}) << "nor dodges";
    EXPECT_TRUE(cs::fightMove(combat, timid, rules, random).crouches) << "all fight long";

    timid.seesCrouched = false;
    EXPECT_FALSE(begin(timid).crouches) << "its crouched sight blocked";
    auto bold = fightAt(600.0F, cs::BotSkill{0.5F});
    bold.seesCrouched = true;
    bold.aggression = 1.0F;
    EXPECT_FALSE(begin(bold).crouches) << "all aggression, near";
    auto far = fightAt(900.0F, cs::BotSkill{0.5F});
    far.seesCrouched = true;
    far.aggression = 1.0F;
    EXPECT_TRUE(begin(far).crouches) << "farther than 750, whatever its aggression";
    timid.seesCrouched = true;
    timid.holdsKnife = true;
    EXPECT_FALSE(begin(timid).crouches) << "never with a knife";
}

TEST(BotCombatTest, AKnifeDoublesTheChanceToDodge) {
    auto rules = repositoryRules();
    rules.dodgeChance = 0.5F;
    nx::Random random{7, "combat tests"};
    cs::BotCombat combat = cs::spawnedCombat(rules, random);
    int dodgingWithAGun = 0;
    for (int fight = 0; fight < 64; ++fight) {
        auto knifing = fightAt(375.0F, cs::BotSkill{1.0F}, after(fight));
        knifing.holdsKnife = true;
        std::ignore = cs::fightMove(combat, knifing, rules, random);
        EXPECT_TRUE(combat.dodges) << "half its skill, twice: always";
        cs::endFight(combat);
        std::ignore = cs::fightMove(combat, fightAt(375.0F, cs::BotSkill{1.0F}, after(fight)), rules, random);
        dodgingWithAGun += combat.dodges ? 1 : 0;
        cs::endFight(combat);
    }
    EXPECT_GT(dodgingWithAGun, 16);
    EXPECT_LT(dodgingWithAGun, 48) << "about half with a gun";
}

} // namespace
