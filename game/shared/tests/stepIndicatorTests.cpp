// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// How the steps of the entities' bodies show: the repository's data/cstrike/stepIndicator.toml, and a look
// that cannot show refused.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import cs.shared;

namespace {

[[nodiscard]] auto repositoryText() -> std::string {
    return nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" /
                                      "stepIndicator.toml")
        .value_or(std::string{});
}

TEST(StepIndicatorLookTest, TheRepositorysMarksAreHeardAsFarAsABotHearsAStep) {
    const auto look = cs::readStepIndicatorLook(repositoryText(), "stepIndicator.toml");
    ASSERT_TRUE(look.has_value()) << (look ? "" : nx::describe(look.error()));
    EXPECT_EQ(look->farDistance, nx::HammerUnits{1100}) << "bots.toml's footstep hearing";
    EXPECT_LT(look->nearDistance, look->farDistance);
    EXPECT_GT(look->ringRadius, look->size) << "the marks about the middle, clear of it";
    EXPECT_EQ(look->mostShown, 12U);
    EXPECT_GE(look->mostHeard, look->mostShown);
}

TEST(StepIndicatorLookTest, ALookThatCannotShowIsRefused) {
    const std::string text = repositoryText();
    for (const auto& [line, broken] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
             {"opacity = 0.9", "opacity = 0.0"},
             {"nearDistance = 128.0", "nearDistance = 2000.0"},
             {"attack = 0.08", "attack = 0.0"},
             {"release = 0.6", "release = 0.0"},
             {"mostShown = 12", "mostShown = 0"},
             {"mostHeard = 64", "mostHeard = 4"}}) {
        std::string changed = text;
        const auto at = changed.find(line);
        ASSERT_NE(at, std::string::npos) << line;
        changed.replace(at, line.size(), broken);
        const auto look = cs::readStepIndicatorLook(changed, "stepIndicator.toml");
        ASSERT_FALSE(look.has_value()) << broken;
        EXPECT_EQ(look.error().code, cs::StepIndicatorError::DataInvalid);
    }
}

} // namespace
