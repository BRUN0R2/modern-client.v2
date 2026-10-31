// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// How the damage numbers look: the repository's data/cstrike/damageNumbers.toml, and a look that cannot
// show refused.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import cs.shared;

namespace {

[[nodiscard]] auto repositoryText() -> std::string {
    return nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" /
                                      "damageNumbers.toml")
        .value_or(std::string{});
}

TEST(DamageNumberLookTest, TheRepositorysNumbersAreWhiteARedHeadsInAHeavyItalicFace) {
    const auto look = cs::readDamageNumberLook(repositoryText(), "damageNumbers.toml");
    ASSERT_TRUE(look.has_value()) << (look ? "" : nx::describe(look.error()));
    EXPECT_TRUE(look->font.isItalic);
    EXPECT_GE(look->font.weight, 700);
    EXPECT_EQ(look->color.red, 255U);
    EXPECT_EQ(look->color.green, 255U);
    EXPECT_EQ(look->headColor.red, 255U);
    EXPECT_LT(look->headColor.green, 100U) << "a head's red";
    EXPECT_LT(look->startScale, 1.0F) << "it starts small";
    EXPECT_GT(look->peakScale, 1.0F) << "and pops up past its size";
    EXPECT_GT(look->rise, 0.0F);
    EXPECT_GT(look->gap, 0.0F) << "its digits a little over the head";
    EXPECT_LT(look->gap, look->height) << "never far over it";
}

TEST(DamageNumberLookTest, ALookThatCannotShowIsRefused) {
    const std::string text = repositoryText();
    for (const auto& [line, broken] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
             {"family = \"Verdana\"", "family = \"\""},
             {"height = 0.045", "height = 0.0"},
             {"fadeOut = 0.3", "fadeOut = 0.9"},
             {"startScale = 0.3", "startScale = 1.5"},
             {"mostNumbers = 64", "mostNumbers = 0"}}) {
        std::string changed = text;
        const auto at = changed.find(line);
        ASSERT_NE(at, std::string::npos) << line;
        changed.replace(at, line.size(), broken);
        const auto look = cs::readDamageNumberLook(changed, "damageNumbers.toml");
        ASSERT_FALSE(look.has_value()) << broken;
        EXPECT_EQ(look.error().code, cs::DamageNumberError::DataInvalid);
    }
}

} // namespace
