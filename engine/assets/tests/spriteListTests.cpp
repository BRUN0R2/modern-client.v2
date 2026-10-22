// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Sprite lists: hud.txt and the weapons' lists, read as the original reads them.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;

namespace {

using nx::assets::parseSpriteList;
using nx::assets::SpriteListError;
using nx::assets::SpriteRect;

[[nodiscard]] auto errorOf(std::string_view text) -> std::optional<SpriteListError> {
    const auto list = parseSpriteList(text);
    return list ? std::nullopt : std::optional{list.error().code};
}

TEST(SpriteListTest, ReadsTheEntriesOfAWeapon) {
    const auto list = parseSpriteList("10\n"
                                      "weapon\t\t\t320 320hud1\t0\t0\t80\t20\n"
                                      "crosshair\t\t640 crosshairs\t24\t0\t24\t24\r\n");
    ASSERT_TRUE(list.has_value()) << nx::describe(list.error());
    ASSERT_EQ(list->size(), 2U) << "a count above the entries reads those there are";
    EXPECT_EQ((*list)[0].name, "weapon");
    EXPECT_EQ((*list)[0].resolution, 320U);
    EXPECT_EQ((*list)[0].sprite, "320hud1");
    EXPECT_EQ((*list)[0].rect, (SpriteRect{.x = 0, .y = 0, .width = 80, .height = 20}));
    EXPECT_EQ((*list)[1].name, "crosshair");
    EXPECT_EQ((*list)[1].resolution, 640U);
    EXPECT_EQ((*list)[1].rect, (SpriteRect{.x = 24, .y = 0, .width = 24, .height = 24}));
}

TEST(SpriteListTest, ReadsWordsAsTheOriginalSplitsThem) {
    const auto list = parseSpriteList("// the HUD\n"
                                      "1 // one entry\n"
                                      "\"number 0\" 640 \"640hud7\" 0 0 20 24\n"
                                      "ignored 640 640hud7 0 0 1 1\n");
    ASSERT_TRUE(list.has_value()) << nx::describe(list.error());
    ASSERT_EQ(list->size(), 1U) << "a count below the entries reads that many";
    EXPECT_EQ((*list)[0].name, "number 0");
    EXPECT_EQ((*list)[0].sprite, "640hud7");
    EXPECT_TRUE(parseSpriteList("0").value().empty());
}

TEST(SpriteListTest, RejectsWhatIsNotAList) {
    EXPECT_EQ(errorOf(""), SpriteListError::NoCount);
    EXPECT_EQ(errorOf("weapon 320 320hud1 0 0 80 20"), SpriteListError::NoCount);
    EXPECT_EQ(errorOf("-1"), SpriteListError::NoCount);
    EXPECT_EQ(errorOf("1 weapon 320 320hud1 0 0 80"), SpriteListError::BadEntry) << "cut short";
    EXPECT_EQ(errorOf("1 weapon 320 320hud1 0 0 eighty 20"), SpriteListError::BadEntry);
    EXPECT_EQ(errorOf("1 weapon 320 320hud1 0 -4 80 20"), SpriteListError::BadEntry);
    EXPECT_EQ(errorOf("1 weapon 3.5 320hud1 0 0 80 20"), SpriteListError::BadEntry);
}

} // namespace
