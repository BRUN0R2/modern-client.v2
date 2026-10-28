// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The console's bot commands (docs/10, section 11): a bot added to a team, of a profile by its name or
// of one drawn of bot_difficulty's difficulty, named after bot_prefix, counts in bot_quota; every bot
// kicked sets it to 0, one kicked by its name lowers it; and what cannot act on a game leaves it as it
// was.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;
import cs.shared;
import cs.bots;

#include "consoleFixtures.hpp"

namespace {

using nx::console::ConsoleError;

/// A profile the tests' game has in it already.
constexpr std::string_view kInGame = "Taken";
/// The name of the bot the tests' game has.
constexpr std::string_view kBotName = "Gabe";

class BotCommandsTest : public ::testing::Test {
protected:
    BotCommandsTest() {
        auto registered = cs::BotCvars::registerIn(console);
        EXPECT_TRUE(registered.has_value());
        cvars.emplace(std::move(registered).value());
        const cs::BotActions actions{
            .add = [this](const cs::BotRequest& request) -> nx::Result<void, cs::BotActionError> {
                if (!isPlayed) {
                    return nx::fail(cs::BotActionError::NoMap);
                }
                if (request.profile == kInGame) {
                    return nx::fail(cs::BotActionError::Refused);
                }
                requests.push_back(request);
                return {};
            },
            .kick = [this](std::string_view name) -> nx::Result<void, cs::BotActionError> {
                if (name != kBotName) {
                    return nx::fail(cs::BotActionError::NoSuchBot);
                }
                kicked.emplace_back(name);
                return {};
            },
            .kickAll = [this]() -> nx::Result<void, cs::BotActionError> {
                ++kicks;
                return {};
            }};
        EXPECT_TRUE(cs::registerBotCommands(console, *cvars, actions).has_value());
    }

    void run(std::string_view script) {
        output.clear();
        console.addText(script);
        std::ignore = console.execute();
    }

    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    nx::console::Console console{output, files};
    std::optional<cs::BotCvars> cvars;
    bool isPlayed = true;
    std::vector<cs::BotRequest> requests;
    std::vector<std::string> kicked;
    std::uint32_t kicks = 0;
};

TEST_F(BotCommandsTest, EachBotAddedJoinsItsTeamAndCountsInTheQuota) {
    run("bot_add; bot_add_t; bot_add_ct");
    EXPECT_TRUE(output.failures.empty());
    const std::vector<std::optional<cs::Team>> teams =
        requests | std::views::transform(&cs::BotRequest::team) | std::ranges::to<std::vector>();
    EXPECT_EQ(teams, (std::vector<std::optional<cs::Team>>{std::nullopt, cs::Team::Terrorists,
                                                           cs::Team::CounterTerrorists}));
    EXPECT_TRUE(std::ranges::none_of(requests, [](const cs::BotRequest& request) {
        return request.profile.has_value();
    })) << "each of a profile drawn";
    EXPECT_EQ(cvars->quotaCount(), 3U);
}

TEST_F(BotCommandsTest, ABotAddedByItsProfileIsNamedAfterBotPrefix) {
    run("bot_prefix [NX]; bot_add_t Abner");
    EXPECT_TRUE(output.failures.empty());
    ASSERT_EQ(requests.size(), 1U);
    EXPECT_EQ(requests.front().profile, "Abner");
    EXPECT_EQ(requests.front().team, cs::Team::Terrorists);
    EXPECT_EQ(requests.front().prefix, "[NX]");
    EXPECT_EQ(cvars->quotaCount(), 1U);
    run(std::format("bot_add {}", kInGame));
    EXPECT_EQ(output.failureCodes(), std::vector{ConsoleError::CommandFailed}) << "refused";
    run("bot_add Abner Basil");
    EXPECT_EQ(output.failureCodes(), std::vector{ConsoleError::InvalidArguments});
    EXPECT_EQ(cvars->quotaCount(), 1U) << "what joins no bot counts none";
}

TEST_F(BotCommandsTest, KickingEveryBotSetsTheQuotaToNoneAndOneByItsNameLowersIt) {
    run("bot_quota 5; bot_kick");
    EXPECT_EQ(kicks, 1U);
    EXPECT_EQ(cvars->quotaCount(), 0U);
    run("bot_quota 2; bot_kick all");
    EXPECT_EQ(kicks, 2U);
    EXPECT_EQ(cvars->quotaCount(), 0U);
    run(std::format("bot_quota 2; bot_kick {}", kBotName));
    EXPECT_TRUE(output.failures.empty());
    EXPECT_EQ(kicked, std::vector<std::string>{std::string{kBotName}});
    EXPECT_EQ(cvars->quotaCount(), 1U);
    run("bot_kick Nobody");
    EXPECT_EQ(output.failureCodes(), std::vector{ConsoleError::CommandFailed}) << "no bot of that name";
    run("bot_kick Gabe Abner");
    EXPECT_EQ(output.failureCodes(), std::vector{ConsoleError::InvalidArguments});
    EXPECT_EQ(cvars->quotaCount(), 1U);
}

TEST_F(BotCommandsTest, ABotAddedWithNoMapFailsAndLeavesTheQuota) {
    isPlayed = false;
    run("bot_add_t");
    EXPECT_EQ(output.failureCodes(), std::vector{ConsoleError::CommandFailed});
    EXPECT_EQ(cvars->quotaCount(), 0U);
}

TEST_F(BotCommandsTest, EachBotAddedPlaysAtTheDifficultyBotDifficultyNames) {
    run("bot_add; bot_difficulty 3; bot_add_t");
    const std::vector<cs::BotDifficulty> difficulties =
        requests | std::views::transform(&cs::BotRequest::difficulty) | std::ranges::to<std::vector>();
    EXPECT_EQ(difficulties, (std::vector{cs::BotDifficulty::Easy, cs::BotDifficulty::Expert}))
        << "0 at first";
}

TEST_F(BotCommandsTest, BotDifficultyNamesEachDifficultyFromATenthBelowItsNumber) {
    for (const auto& [value, difficulty] :
         {std::pair{"-1", cs::BotDifficulty::Easy}, std::pair{"0.89", cs::BotDifficulty::Easy},
          std::pair{"0.9", cs::BotDifficulty::Normal}, std::pair{"1.89", cs::BotDifficulty::Normal},
          std::pair{"1.9", cs::BotDifficulty::Hard}, std::pair{"2.9", cs::BotDifficulty::Expert},
          std::pair{"7", cs::BotDifficulty::Expert}}) {
        run(std::format("bot_difficulty {}", value));
        EXPECT_EQ(cvars->difficultyLevel(), difficulty) << value;
    }
}

TEST_F(BotCommandsTest, TheQuotaCountsWholeBotsAndNoneBelowOne) {
    for (const auto& [value, count] : {std::pair{"2.9", 2U}, std::pair{"0.5", 0U}, std::pair{"-3", 0U}}) {
        run(std::format("bot_quota {}", value));
        EXPECT_EQ(cvars->quotaCount(), count) << value;
    }
}

} // namespace
