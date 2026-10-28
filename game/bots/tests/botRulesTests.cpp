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
    EXPECT_EQ(rules->look.attacking.stiffness, nx::PerSecondSquared{300.0F}) << "UpdateLookAngles";
    EXPECT_EQ(rules->aim.errorPerRange, nx::Scale{0.1F}) << "SetAimOffset";
    EXPECT_EQ(rules->fire.aimHalfWidth, nx::HammerUnits{32.0F}) << "FireWeaponAtEnemy";
    const nx::agents::Rules brain = cs::brainRulesOf(*rules, nx::Seconds{0.25F});
    EXPECT_EQ(brain.attackDistance, rules->attackDistance);
    EXPECT_EQ(brain.memoryLimit, rules->memoryLimit);
    EXPECT_EQ(brain.reactionTime, nx::Seconds{0.25F});
}

TEST(BotRulesTest, TheLevelsOfSkillAreTheZBotsTemplatesEachOfItsDifficulties) {
    const auto rules = cs::readBotRules(repositoryRules(), "bots.toml");
    ASSERT_TRUE(rules.has_value()) << (rules ? "" : nx::describe(rules.error()));
    ASSERT_EQ(rules->skillLevels.size(), 8U);
    const cs::BotSkillLevel& easy = rules->skillLevels.front();
    EXPECT_EQ(easy.name, "easy");
    EXPECT_EQ(easy.difficulties, std::vector{cs::BotDifficulty::Easy});
    EXPECT_EQ(easy.skill, cs::BotSkill{0.0F});
    EXPECT_EQ(easy.reactionTime, nx::Seconds{0.5F});
    EXPECT_EQ(easy.attackDelay, nx::Seconds{1.5F});
    const cs::BotSkillLevel& elite = rules->skillLevels.back();
    EXPECT_EQ(elite.name, "elite");
    EXPECT_EQ(elite.difficulties, std::vector{cs::BotDifficulty::Expert});
    EXPECT_EQ(elite.skill, cs::BotSkill{1.0F});
    EXPECT_EQ(elite.reactionTime, nx::Seconds{0.2F});
    EXPECT_EQ(elite.attackDelay, nx::Seconds{});
}

TEST(BotRulesTest, RulesBeyondTheirBoundsAreRefused) {
    const std::string text = repositoryRules();
    for (const auto& [line, changed] :
         {std::pair{"fieldOfView = 120.0", "fieldOfView = 361.0"},
          std::pair{"perceptionBudget = 32", "perceptionBudget = 0"},
          std::pair{"quotaInterval = 0.3", "quotaInterval = 0.0"},
          std::pair{R"(difficulties = ["expert"]
skill = 1.0)",
                    R"(difficulties = []
skill = 1.0)"},
          std::pair{"reactionTime = 0.5", "reactionTime = -0.5"}, std::pair{"skill = 0.25", "skill = 1.25"},
          std::pair{"pitchLimit = 89.0", "pitchLimit = 91.0"}, std::pair{"damping = 25.0", "damping = 0.0"},
          std::pair{"latestNewError = 1.0", "latestNewError = 0.1"},
          std::pair{"thinkInterval = 0.1", "thinkInterval = 0.0"},
          std::pair{R"(shortest = 0.3
longest = 0.7)",
                    R"(shortest = 0.8
longest = 0.7)"}}) {
        std::string broken = text;
        const std::size_t at = broken.find(line);
        ASSERT_NE(at, std::string::npos) << line;
        broken.replace(at, std::string_view{line}.size(), changed);
        const auto rules = cs::readBotRules(broken, "bots.toml");
        ASSERT_FALSE(rules.has_value()) << changed;
        EXPECT_EQ(rules.error().code, cs::BotError::InvalidData);
    }
}

TEST(BotRulesTest, ADifficultyNoLevelOfSkillPlaysAtIsRefused) {
    std::string text = repositoryRules();
    constexpr std::string_view kExpert = R"(["expert"])";
    for (std::size_t at = text.find(kExpert); at != std::string::npos; at = text.find(kExpert)) {
        text.replace(at, kExpert.size(), R"(["hard"])");
    }
    const auto rules = cs::readBotRules(text, "bots.toml");
    ASSERT_FALSE(rules.has_value());
    EXPECT_EQ(rules.error().code, cs::BotError::InvalidData);
}

} // namespace
