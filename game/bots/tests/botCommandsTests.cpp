// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The console's bot commands (docs/10, section 11): a bot added to a team counts in bot_quota, every bot
// kicked sets it to 0, and what cannot act on a game leaves it as it was.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;
import cs.shared;
import cs.bots;

#include "consoleFixtures.hpp"

namespace {

using nx::console::ConsoleError;

class BotCommandsTest : public ::testing::Test {
protected:
    BotCommandsTest() {
        auto registered = cs::BotCvars::registerIn(console);
        EXPECT_TRUE(registered.has_value());
        cvars.emplace(std::move(registered).value());
        const cs::BotActions actions{
            .add = [this](std::optional<cs::Team> team) -> nx::Result<void, cs::BotActionError> {
                if (!isPlayed) {
                    return nx::fail(cs::BotActionError::NoMap);
                }
                added.push_back(team);
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
    std::vector<std::optional<cs::Team>> added;
    std::uint32_t kicks = 0;
};

TEST_F(BotCommandsTest, EachBotAddedJoinsItsTeamAndCountsInTheQuota) {
    run("bot_add; bot_add_t; bot_add_ct");
    EXPECT_TRUE(output.failures.empty());
    EXPECT_EQ(added, (std::vector<std::optional<cs::Team>>{std::nullopt, cs::Team::Terrorists,
                                                           cs::Team::CounterTerrorists}));
    EXPECT_EQ(cvars->quotaCount(), 3U);
}

TEST_F(BotCommandsTest, KickingEveryBotSetsTheQuotaToNoneButNotByAName) {
    run("bot_quota 5; bot_kick");
    EXPECT_EQ(kicks, 1U);
    EXPECT_EQ(cvars->quotaCount(), 0U);
    run("bot_quota 2; bot_kick all");
    EXPECT_EQ(kicks, 2U);
    EXPECT_EQ(cvars->quotaCount(), 0U);
    run("bot_quota 2; bot_kick Gabe");
    EXPECT_EQ(output.failureCodes(), std::vector{ConsoleError::InvalidArguments});
    EXPECT_EQ(kicks, 2U);
    EXPECT_EQ(cvars->quotaCount(), 2U);
}

TEST_F(BotCommandsTest, ABotAddedWithNoMapFailsAndLeavesTheQuota) {
    isPlayed = false;
    run("bot_add_t");
    EXPECT_EQ(output.failureCodes(), std::vector{ConsoleError::CommandFailed});
    EXPECT_EQ(cvars->quotaCount(), 0U);
}

TEST_F(BotCommandsTest, TheQuotaCountsWholeBotsAndNoneBelowOne) {
    for (const auto& [value, count] : {std::pair{"2.9", 2U}, std::pair{"0.5", 0U}, std::pair{"-3", 0U}}) {
        run(std::format("bot_quota {}", value));
        EXPECT_EQ(cvars->quotaCount(), count) << value;
    }
}

} // namespace
