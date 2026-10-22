// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// VGUI2's localized strings: the text of each token, from texts shaped like the installation's
// gameui_english.txt.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;

namespace {

using nx::assets::KeyValuesEscapes;
using nx::assets::LocalizationError;
using nx::assets::LocalizedStrings;

[[nodiscard]] auto stringsOf(std::string_view text) -> nx::Result<LocalizedStrings, LocalizationError> {
    const auto entries = nx::assets::parseKeyValues(text, KeyValuesEscapes::Backslash);
    EXPECT_TRUE(entries.has_value());
    return LocalizedStrings::read(entries.value_or(std::vector<nx::assets::KeyValuesEntry>{}));
}

TEST(LocalizationTest, FindsTheTextOfATokenAsALayoutNamesIt) {
    const auto strings = stringsOf(R"("lang"
{
"Language" "English"
"Tokens"
{
"GameUI_Console"		"Console"
"GameUI_Submit"			"Submit"
"Cstrike_Terror_Label"	"Having established a reputation \nthe Phoenix Faction"
"gameui_console"		"a token written twice"
}
}
)");
    ASSERT_TRUE(strings.has_value()) << nx::describe(strings.error());
    EXPECT_EQ(strings->language(), "English");
    EXPECT_EQ(strings->size(), 3U) << "the first of a token written twice is kept";
    EXPECT_EQ(strings->find("GameUI_Console"), "Console");
    EXPECT_EQ(strings->find("#GameUI_Console"), "Console") << "as a layout writes it";
    EXPECT_EQ(strings->find("#gameui_SUBMIT"), "Submit") << "in any case";
    EXPECT_EQ(strings->find("Cstrike_Terror_Label"), "Having established a reputation \nthe Phoenix Faction");
    EXPECT_EQ(strings->find("GameUI_Missing"), std::nullopt);
}

TEST(LocalizationTest, RefusesAFileWithoutTokens) {
    EXPECT_EQ(stringsOf(R"("lang" { "Language" "English" })").error().code,
              LocalizationError::NotLocalization);
    EXPECT_EQ(stringsOf(R"("other" { "Tokens" { } })").error().code, LocalizationError::NotLocalization);
    const auto empty = stringsOf(R"("lang" { "Tokens" { } })");
    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ(empty->language(), "");
    EXPECT_EQ(empty->size(), 0U);
}

} // namespace
