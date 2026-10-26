// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The money of the rounds, as the original pays it: the repository's data/cstrike/economy.toml and
// what its reader refuses, a player's money held from none to 16000, what the winners of a round
// get, the bonus for the hostages still held, and the loser bonus as the original's series grows
// it, starts it over and gives it.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import cs.shared;

namespace {

using cs::EconomyRules;
using cs::MapObjective;
using cs::RoundEndReason;
using cs::Team;
using cs::TeamAccounts;

/// The repository's data/cstrike/economy.toml, read.
[[nodiscard]] auto repositoryRules() -> EconomyRules {
    const std::filesystem::path file = std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "economy.toml";
    auto text = nx::platform::readTextFile(file);
    EXPECT_TRUE(text.has_value());
    auto rules = cs::readEconomyRules(text.value_or(std::string{}), "economy.toml");
    EXPECT_TRUE(rules.has_value()) << (rules ? "" : nx::describe(rules.error()));
    return rules.value_or(EconomyRules{});
}

/// What each team is paid, terrorists then counter-terrorists.
[[nodiscard]] auto paid(std::int32_t terrorists, std::int32_t counterTerrorists)
    -> nx::EnumArray<Team, nx::Money> {
    return nx::EnumArray<Team, nx::Money>{
        nx::EnumArray<Team, nx::Money>::Storage{nx::Money{terrorists}, nx::Money{counterTerrorists}}};
}

TEST(EconomyTest, TheRepositorysMoneyIsTheOriginals) {
    const EconomyRules rules = repositoryRules();
    EXPECT_EQ(rules.loserBonus.first, nx::Money{1400});
    EXPECT_EQ(rules.loserBonus.afterStreak, nx::Money{1500});
    EXPECT_EQ(rules.loserBonus.step, nx::Money{500});
    EXPECT_EQ(rules.loserBonus.most, nx::Money{3000});
    EXPECT_EQ(rules.roundEnd(RoundEndReason::TargetSaved).money, nx::Money{3250});
    EXPECT_FALSE(rules.roundEnd(RoundEndReason::TargetSaved).paysLivingLosers);
    EXPECT_EQ(rules.roundEnd(RoundEndReason::HostagesNotRescued).money, nx::Money{3250});
    EXPECT_FALSE(rules.roundEnd(RoundEndReason::HostagesNotRescued).paysLivingLosers);
    EXPECT_EQ(rules.roundEnd(RoundEndReason::TerroristsNotEscaped).money, nx::Money{});
    EXPECT_EQ(rules.roundEnd(RoundEndReason::VipNotEscaped).money, nx::Money{3250});
    EXPECT_TRUE(rules.roundEnd(RoundEndReason::VipNotEscaped).paysLivingLosers);
    // CHostage::TakeDamage: 20 for each point of damage, and 20 times 25 more for a kill.
    EXPECT_EQ(rules.hostageHarm.perDamage, nx::Money{20});
    EXPECT_EQ(rules.hostageHarm.perKill, nx::Money{500});
}

TEST(EconomyTest, TheReaderRefusesAnEndTwiceAndNegativeMoney) {
    constexpr std::string_view kBonuses =
        "[loserBonus]\nfirst = 1400\nafterStreak = 1500\nstep = 500\nmost = 3000\n"
        "[hostageBonus]\nperHostage = 150\nupTo = 2000\n"
        "[hostageHarm]\nperDamage = 20\nperKill = 500\n";
    const auto twice = cs::readEconomyRules(
        std::format("{}{}", kBonuses,
                    "[[roundEnds]]\nreason = \"targetSaved\"\nmoney = 1\npaysLivingLosers = true\n"
                    "[[roundEnds]]\nreason = \"targetSaved\"\nmoney = 2\npaysLivingLosers = true\n"),
        "twice.toml");
    ASSERT_FALSE(twice.has_value());
    EXPECT_EQ(twice.error().code, cs::EconomyError::DataInvalid);
    EXPECT_EQ(twice.error().detail, "twice.toml: the money of TargetSaved twice");
    const auto negative = cs::readEconomyRules(
        std::format("{}{}", kBonuses,
                    "[[roundEnds]]\nreason = \"targetSaved\"\nmoney = -1\npaysLivingLosers = true\n"),
        "negative.toml");
    ASSERT_FALSE(negative.has_value());
    EXPECT_EQ(negative.error().detail, "negative.toml");
    const auto unlisted = cs::readEconomyRules(std::format("roundEnds = []\n{}", kBonuses), "unlisted.toml");
    ASSERT_TRUE(unlisted.has_value());
    EXPECT_EQ(unlisted->roundEnd(RoundEndReason::VipNotEscaped).money, nx::Money{});
    EXPECT_TRUE(unlisted->roundEnd(RoundEndReason::VipNotEscaped).paysLivingLosers);
}

TEST(EconomyTest, APlayersMoneyIsHeldFromNoneTo16000) {
    EXPECT_EQ(cs::addMoney(nx::Money{800}, nx::Money{3250}), nx::Money{4050});
    EXPECT_EQ(cs::addMoney(nx::Money{15000}, nx::Money{3250}), nx::Money{16000});
    EXPECT_EQ(cs::addMoney(nx::Money{300}, nx::Money{-3300}), nx::Money{});
    EXPECT_EQ(cs::addMoney(nx::Money{16000}, nx::Money{std::numeric_limits<std::int32_t>::max()}),
              nx::Money{16000});
}

TEST(EconomyTest, TheWinnersGetTheirEndsMoneyAndTheLosersTheBonus) {
    const EconomyRules rules = repositoryRules();
    TeamAccounts accounts{rules};
    accounts.roundEnded(RoundEndReason::TargetSaved);
    EXPECT_EQ(accounts.roundStarts({MapObjective::BombTarget}, 0), paid(1400, 3250));
    EXPECT_EQ(accounts.roundStarts({MapObjective::BombTarget}, 0), paid(0, 0)) << "paid once";
}

TEST(EconomyTest, TheBonusGrowsWithTheLossesInARowAndStartsOverAfterAStreak) {
    // The original's series: 1400, 1900, 2400, 2900, 3400 and no more; after a streak ends, 1500,
    // 2000, 2500, 3000 and no more.
    const EconomyRules rules = repositoryRules();
    TeamAccounts accounts{rules};
    std::vector<std::int32_t> bonuses;
    for (int round = 0; round < 6; ++round) {
        accounts.roundEnded(RoundEndReason::TargetSaved); // the terrorists lose
        bonuses.push_back(accounts.roundStarts({MapObjective::BombTarget}, 0)[Team::Terrorists].value());
    }
    EXPECT_EQ(bonuses, (std::vector<std::int32_t>{1400, 1900, 2400, 2900, 3400, 3400}));
    EXPECT_EQ(accounts.lossesInARow(Team::Terrorists), 6U);

    bonuses.clear();
    for (int round = 0; round < 5; ++round) {
        accounts.roundEnded(RoundEndReason::HostagesNotRescued); // the counter-terrorists lose
        bonuses.push_back(accounts.roundStarts({MapObjective::Hostages}, 0)[Team::CounterTerrorists].value());
    }
    EXPECT_EQ(bonuses, (std::vector<std::int32_t>{1500, 2000, 2500, 3000, 3000}));
    EXPECT_EQ(accounts.lossesInARow(Team::Terrorists), 0U);
}

TEST(EconomyTest, TheWinnersGetABonusForTheHostagesStillHeld) {
    const EconomyRules rules = repositoryRules();
    TeamAccounts accounts{rules};
    accounts.roundEnded(RoundEndReason::HostagesNotRescued);
    EXPECT_EQ(accounts.roundStarts({MapObjective::Hostages}, 4), paid(3250 + 600, 1400));
    // Counted while the sum is below 2000: 14 of them at most, 2100.
    accounts.roundEnded(RoundEndReason::HostagesNotRescued);
    EXPECT_EQ(accounts.roundStarts({MapObjective::Hostages}, 20)[Team::Terrorists], nx::Money{3250 + 2100});
}

TEST(EconomyTest, ADrawPaysNobodyButAStreakStillRaisesTheBonus) {
    const EconomyRules rules = repositoryRules();
    TeamAccounts accounts{rules};
    accounts.roundEnded(RoundEndReason::RoundDraw);
    EXPECT_EQ(accounts.roundStarts({MapObjective::BombTarget}, 4), paid(0, 0));
    EXPECT_EQ(accounts.loserBonus(), nx::Money{1400});
    // The terrorists lost twice: the draw that follows raises their bonus, which no one gets yet.
    for (int round = 0; round < 2; ++round) {
        accounts.roundEnded(RoundEndReason::TargetSaved);
        std::ignore = accounts.roundStarts({MapObjective::BombTarget}, 0);
    }
    EXPECT_EQ(accounts.loserBonus(), nx::Money{1900});
    accounts.roundEnded(RoundEndReason::RoundDraw);
    EXPECT_EQ(accounts.roundStarts({MapObjective::BombTarget}, 0), paid(0, 0));
    EXPECT_EQ(accounts.loserBonus(), nx::Money{2400});
    EXPECT_EQ(accounts.lossesInARow(Team::Terrorists), 2U);
}

TEST(EconomyTest, TheTerroristsOfAnEscapeMapGetNoBonus) {
    const EconomyRules rules = repositoryRules();
    TeamAccounts accounts{rules};
    accounts.roundEnded(RoundEndReason::TerroristsNotEscaped);
    EXPECT_EQ(accounts.roundStarts({MapObjective::EscapeZone}, 0), paid(0, 0));
}

TEST(EconomyTest, ARestartedGameStartsTheMoneyOver) {
    const EconomyRules rules = repositoryRules();
    TeamAccounts accounts{rules};
    for (int round = 0; round < 3; ++round) {
        accounts.roundEnded(RoundEndReason::TargetSaved);
        std::ignore = accounts.roundStarts({MapObjective::BombTarget}, 0);
    }
    accounts.roundEnded(RoundEndReason::TargetSaved);
    accounts.restartGame();
    EXPECT_EQ(accounts.loserBonus(), nx::Money{1400});
    EXPECT_EQ(accounts.lossesInARow(Team::Terrorists), 0U);
    EXPECT_EQ(accounts.roundStarts({MapObjective::BombTarget}, 0), paid(0, 0));
}

TEST(EconomyTest, TheStartingMoneyIsHeldAsTheGameReadsItAndSetBack) {
    // CheckStartMoney: 20000 gives 16000 and sets the cvar so; 100 gives 800; 1000.7 gives 1000.
    const cs::StartMoney over = cs::readStartMoney(20000.0F);
    EXPECT_EQ(over.money, cs::kMostMoney);
    EXPECT_EQ(over.setTo, std::optional{16000.0F});
    const cs::StartMoney under = cs::readStartMoney(100.0F);
    EXPECT_EQ(under.money, cs::kStartMoney);
    EXPECT_EQ(under.setTo, std::optional{800.0F});
    const cs::StartMoney cut = cs::readStartMoney(1000.7F);
    EXPECT_EQ(cut.money, nx::Money{1000});
    EXPECT_FALSE(cut.setTo.has_value());
}

} // namespace
