// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The command line of next: the original's `+` commands, as its stuffcmds reads them, and the options.

#include <gtest/gtest.h>

import std;
import nx.core;
import app.client;

namespace {

[[nodiscard]] auto parse(std::initializer_list<std::string_view> words)
    -> nx::Result<app::ClientOptions, app::CommandLineError> {
    const std::vector<std::string_view> arguments{words};
    return app::parseClientOptions(arguments);
}

/// Why a command line was refused, and the word at fault.
using Refusal = std::pair<app::CommandLineError, std::string>;

/// The refusal of `words`; none when they are valid options.
[[nodiscard]] auto refusalOf(std::initializer_list<std::string_view> words) -> std::optional<Refusal> {
    auto options = parse(words);
    if (options) {
        return std::nullopt;
    }
    return Refusal{options.error().code, std::move(options.error().detail)};
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

TEST(ClientCommandLineTest, WhatIsNotAnOptionIsRefusedWithTheWordAtFault) {
    using app::CommandLineError;
    EXPECT_EQ(refusalOf({"de_dust2"}), (Refusal{CommandLineError::UnknownOption, "de_dust2"}))
        << "a word outside a command";
    EXPECT_EQ(refusalOf({"-game"}), (Refusal{CommandLineError::UnknownOption, "-game"}));
    EXPECT_EQ(refusalOf({"-basedir"}), (Refusal{CommandLineError::MissingValue, "-basedir"}))
        << "without its folder";
    EXPECT_EQ(refusalOf({"--content-config"}), (Refusal{CommandLineError::MissingValue, "--content-config"}))
        << "without its file";
    EXPECT_EQ(refusalOf({"--validation", "--validation"}),
              (Refusal{CommandLineError::RepeatedOption, "--validation"}));
    EXPECT_EQ(refusalOf({"+sensitivity", "-1"}), (Refusal{CommandLineError::UnknownOption, "-1"}))
        << "`-1` ends the command, as in the original";
}

TEST(ClientCommandLineTest, ALocalUserFolderIsExplicitAndDoesNotBecomeABootCommand) {
    const auto options = parse({"--user-folder", "offline test/user", "+map", "de_dust2"});
    ASSERT_TRUE(options.has_value());
    EXPECT_EQ(options->userFolder, std::filesystem::path{"offline test/user"});
    EXPECT_EQ(options->bootCommands, "map de_dust2\n");
    EXPECT_FALSE(parse({})->userFolder.has_value());
    EXPECT_EQ(refusalOf({"--user-folder"}), (Refusal{app::CommandLineError::MissingValue, "--user-folder"}));
    EXPECT_EQ(refusalOf({"--user-folder", "one", "--user-folder", "two"}),
              (Refusal{app::CommandLineError::RepeatedOption, "--user-folder"}));
}

} // namespace
