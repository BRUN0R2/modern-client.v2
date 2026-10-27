// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The command line of next: the original's `+` commands, as its stuffcmds reads them, and the options.

#include <gtest/gtest.h>

import std;
import app.client;

namespace {

[[nodiscard]] auto parse(std::initializer_list<std::string_view> words) -> std::optional<app::ClientOptions> {
    const std::vector<std::string_view> arguments{words};
    return app::parseClientOptions(arguments);
}

TEST(ClientCommandLineTest, PlusWordsAreCommandsAsTheOriginalReadsThem) {
    const auto options = parse({"+map", "de_dust2", "+sensitivity", "2.5", "+exec", "my", "config.cfg"});
    ASSERT_TRUE(options.has_value());
    EXPECT_EQ(options->bootCommands, "map de_dust2\nsensitivity 2.5\nexec my config.cfg\n");
    EXPECT_FALSE(options->install.has_value());
    EXPECT_FALSE(options->isValidationEnabled);

    EXPECT_EQ(parse({"+noclip"})->bootCommands, "noclip\n");
    EXPECT_EQ(parse({})->bootCommands, "");
}

TEST(ClientCommandLineTest, OptionsEndTheCommandBeforeThem) {
    const auto options =
        parse({"+map", "cs_italy", "-basedir", "C:/Games/Half-Life", "--validation", "+quit"});
    ASSERT_TRUE(options.has_value());
    EXPECT_EQ(options->bootCommands, "map cs_italy\nquit\n");
    EXPECT_EQ(options->install, std::filesystem::path{"C:/Games/Half-Life"});
    EXPECT_TRUE(options->isValidationEnabled);
    EXPECT_EQ(parse({"--data", "data"})->data, std::filesystem::path{"data"});
    EXPECT_EQ(parse({"--content-config", "mine.toml"})->contentConfig, std::filesystem::path{"mine.toml"});
}

TEST(ClientCommandLineTest, WhatIsNotAnOptionIsRefused) {
    EXPECT_FALSE(parse({"de_dust2"}).has_value()) << "a word outside a command";
    EXPECT_FALSE(parse({"-game"}).has_value());
    EXPECT_FALSE(parse({"-basedir"}).has_value()) << "without its folder";
    EXPECT_FALSE(parse({"--content-config"}).has_value()) << "without its file";
    EXPECT_FALSE(parse({"--validation", "--validation"}).has_value()) << "twice";
    EXPECT_FALSE(parse({"+sensitivity", "-1"}).has_value()) << "`-1` ends the command, as in the original";
}

TEST(ClientCommandLineTest, ALocalUserFolderIsExplicitAndDoesNotBecomeABootCommand) {
    const auto options = parse({"--user-folder", "offline test/user", "+map", "de_dust2"});
    ASSERT_TRUE(options.has_value());
    EXPECT_EQ(options->userFolder, std::filesystem::path{"offline test/user"});
    EXPECT_EQ(options->bootCommands, "map de_dust2\n");
    EXPECT_FALSE(parse({})->userFolder.has_value());
    EXPECT_FALSE(parse({"--user-folder"}).has_value());
    EXPECT_FALSE(parse({"--user-folder", "one", "--user-folder", "two"}).has_value());
}

} // namespace
