// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;

#include "consoleFixtures.hpp"

namespace {

using nx::Result;
using nx::console::CommandLine;
using nx::console::Console;
using nx::console::ConsoleError;
using nx::console::CvarDesc;
using nx::console::CvarFlag;

using Lines = std::vector<std::string>;
using Codes = std::vector<ConsoleError>;

class ListingTest : public testing::Test {
protected:
    ListingTest() {
        EXPECT_TRUE(console
                        .registerCvar(CvarDesc<float>{.name = "sv_gravity",
                                                      .defaultValue = 800,
                                                      .description = "World gravity",
                                                      .flags = {CvarFlag::Notify, CvarFlag::Archive}})
                        .has_value());
        EXPECT_TRUE(console.registerCvar(CvarDesc<std::string>{.name = "hostname", .defaultValue = "nextds"})
                        .has_value());
        EXPECT_TRUE(console
                        .registerCommand(
                            {.name = "kick", .description = "kick <player>: removes a player"},
                            [](Console&, const CommandLine&) -> Result<void, ConsoleError> { return {}; })
                        .has_value());
    }

    /// Lines printed by `script`.
    auto printed(std::string_view script) -> Lines {
        output.clear();
        console.addText(script);
        std::ignore = console.execute(); // the scripts here have no wait
        return output.lines;
    }

    nx_test::RecordingOutput output;
    nx_test::MemoryFiles files;
    Console console{output, files};
};

TEST_F(ListingTest, CvarListShowsValuesAndFlagsSortedByName) {
    EXPECT_EQ(printed("cvarlist"),
              (Lines{R"(hostname "nextds")", R"(sv_gravity "800" (Archive, Notify))", "2 cvars"}));
}

TEST_F(ListingTest, CvarListFiltersByPrefixIgnoringCase) {
    EXPECT_EQ(printed("cvarlist SV_"),
              (Lines{R"(sv_gravity "800" (Archive, Notify))", R"(1 cvar starting with "SV_")"}));
    EXPECT_EQ(printed("cvarlist mp_"), (Lines{R"(0 cvars starting with "mp_")"}));
}

TEST_F(ListingTest, CommandListShowsDescriptions) {
    const Lines lines = printed("cmdlist k");
    EXPECT_EQ(lines, (Lines{"kick : kick <player>: removes a player", R"(1 command starting with "k")"}));

    const Lines all = printed("cmdlist");
    EXPECT_TRUE(std::ranges::contains(all, "echo : echo <text>: prints the text"));
    EXPECT_TRUE(all.back().ends_with(" commands"));
}

TEST_F(ListingTest, FindSearchesNamesAndDescriptions) {
    EXPECT_EQ(printed("find GRAVITY"),
              (Lines{R"(sv_gravity "800" (Archive, Notify) : World gravity)", R"(1 match for "GRAVITY")"}));
    EXPECT_EQ(printed("find player"),
              (Lines{"kick : kick <player>: removes a player", R"(1 match for "player")"}));
    EXPECT_EQ(printed("find nothing-like-this"), (Lines{R"(0 matches for "nothing-like-this")"}));
}

TEST_F(ListingTest, WrongArgumentsShowTheUsage) {
    std::ignore = printed(R"(cvarlist a b; cmdlist a b; find; find "")");
    EXPECT_EQ(output.failureCodes(), (Codes{ConsoleError::InvalidArguments, ConsoleError::InvalidArguments,
                                            ConsoleError::InvalidArguments, ConsoleError::InvalidArguments}));
    EXPECT_EQ(output.failures.front().detail, "usage: cvarlist [prefix]");
}

} // namespace
