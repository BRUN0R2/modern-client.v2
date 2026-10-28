// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.agents;
import cs.bots;

namespace {

[[nodiscard]] auto repositoryRules() -> std::string {
    return nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "bots.toml")
        .value_or(std::string{});
}

TEST(BotRulesTest, TheRepositoryRulesAreConditionZerosWhereItHasThem) {
    const auto rules = cs::readBotRules(repositoryRules(), "bots.toml");
    ASSERT_TRUE(rules.has_value()) << (rules ? "" : nx::describe(rules.error()));
    EXPECT_EQ(rules->fieldOfView, nx::Degrees{120.0F}) << "FInViewCone's 0.5";
    EXPECT_EQ(rules->quotaInterval, nx::Seconds{0.3F}) << "MonitorBotCVars";
    EXPECT_EQ(rules->spots.head, nx::HammerUnits{25.0F});
    EXPECT_EQ(rules->spots.standingFeet, nx::HammerUnits{34.0F});
    EXPECT_EQ(rules->spots.duckedFeet, nx::HammerUnits{14.0F});
    EXPECT_EQ(rules->spots.side, nx::HammerUnits{13.0F});
    const nx::agents::Rules brain = cs::brainRulesOf(*rules);
    EXPECT_EQ(brain.attackDistance, rules->attackDistance);
    EXPECT_EQ(brain.memoryLimit, rules->memoryLimit);
}

TEST(BotRulesTest, AConeWiderThanATurnOrABudgetOfNoneIsRefused) {
    const std::string text = repositoryRules();
    for (const auto& [line, changed] : {std::pair{"fieldOfView = 120.0", "fieldOfView = 361.0"},
                                        std::pair{"perceptionBudget = 32", "perceptionBudget = 0"},
                                        std::pair{"quotaInterval = 0.3", "quotaInterval = 0.0"}}) {
        std::string broken = text;
        const std::size_t at = broken.find(line);
        ASSERT_NE(at, std::string::npos) << line;
        broken.replace(at, std::string_view{line}.size(), changed);
        const auto rules = cs::readBotRules(broken, "bots.toml");
        ASSERT_FALSE(rules.has_value()) << changed;
        EXPECT_EQ(rules.error().code, cs::BotError::InvalidData);
    }
}

} // namespace
