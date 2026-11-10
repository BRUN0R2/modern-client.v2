// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The main menu's data: what the repository's file lists, and what a menu could not show is refused.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import cs.shared;

namespace {

using cs::MainMenuAction;
using cs::MainMenuItem;
using cs::MainMenuListing;

constexpr std::string_view kFonts = "font = { regular = \"Barlow\", strong = \"Barlow SemiBold\" }\n"
                                    "background = \"resource/BackgroundLayout.txt\"\n";
constexpr std::string_view kQuit = "[[items]]\naction = \"quit\"\nlisted = \"always\"\n";
constexpr std::string_view kResume = "[[items]]\naction = \"resumeGame\"\nlisted = \"inGame\"\n";

TEST(MainMenuLookTest, TheRepositorysReads) {
    const auto text =
        nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "mainMenu.toml");
    ASSERT_TRUE(text.has_value());
    const auto look = cs::readMainMenuLook(*text, "mainMenu.toml");
    ASSERT_TRUE(look.has_value()) << (look ? "" : nx::describe(look.error()));
    EXPECT_EQ(look->font.regular, "Barlow");
    EXPECT_EQ(look->font.strong, "Barlow SemiBold");
    EXPECT_EQ(look->background, "resource/BackgroundLayout.txt") << "the original's own";
    const std::vector<MainMenuItem> expected{
        {.action = MainMenuAction::ResumeGame, .listed = MainMenuListing::InGame},
        {.action = MainMenuAction::Disconnect, .listed = MainMenuListing::InGame},
        {.action = MainMenuAction::NewGame, .listed = MainMenuListing::Always},
        {.action = MainMenuAction::Options, .listed = MainMenuListing::Always},
        {.action = MainMenuAction::Quit, .listed = MainMenuListing::Always}};
    EXPECT_EQ(look->items, expected) << "in the order the original's menu lists them";
}

TEST(MainMenuLookTest, WhatTheMenuCouldNotShowIsRefused) {
    const auto refused = [](std::string text) {
        const auto look = cs::readMainMenuLook(text, "test");
        return !look.has_value() && look.error().code == cs::MainMenuLookError::DataInvalid;
    };
    const std::string fonts{kFonts};
    const std::string quit{kQuit};
    const std::string resume{kResume};
    EXPECT_FALSE(refused(fonts + resume + quit));
    EXPECT_TRUE(refused("font = { regular = \"Barlow\", strong = \"\" }\nbackground = \"a.txt\"\n" + quit))
        << "no font";
    EXPECT_TRUE(
        refused("font = { regular = \"Barlow\", strong = \"Barlow SemiBold\" }\nbackground = \"\"\n" + quit))
        << "no background";
    EXPECT_TRUE(refused(fonts + quit + quit)) << "one action twice";
    EXPECT_TRUE(refused(fonts + resume)) << "nothing listed without a map";
    EXPECT_TRUE(refused(fonts)) << "no items";
    EXPECT_TRUE(refused(fonts + "[[items]]\naction = \"fly\"\nlisted = \"always\"\n")) << "an unknown action";
}

} // namespace
