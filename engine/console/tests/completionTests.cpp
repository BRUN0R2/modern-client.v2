// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// What the console's input line needs of the console: the names a typed prefix completes to, the
// name being typed at the end of a line, and the history of the lines sent.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;

#include "consoleFixtures.hpp"

namespace {

using nx::Result;
using nx::console::CommandLine;
using nx::console::Completion;
using nx::console::Console;
using nx::console::ConsoleError;
using nx::console::CvarDesc;
using nx::console::NameKind;

class CompletionTest : public testing::Test {
protected:
    CompletionTest() {
        EXPECT_TRUE(console
                        .registerCvar(CvarDesc<float>{
                            .name = "sv_gravity", .defaultValue = 800, .description = "World gravity"})
                        .has_value());
        EXPECT_TRUE(console.registerCvar(CvarDesc<bool>{.name = "sv_cheats"}).has_value());
        EXPECT_TRUE(console
                        .registerCommand(
                            {.name = "SV_Restart", .description = "Restarts the game"},
                            [](Console&, const CommandLine&) -> Result<void, ConsoleError> { return {}; })
                        .has_value());
        EXPECT_TRUE(console.registerButton({.name = "attack", .description = "Fires"}).has_value());
        console.addText("alias sv_fast \"sv_gravity 400\"");
        std::ignore = console.execute();
    }

    [[nodiscard]] auto names(std::string_view prefix, std::size_t limit = 100) const
        -> std::vector<std::string> {
        return console.completions(prefix, limit) | std::views::transform(&Completion::name) |
               std::ranges::to<std::vector>();
    }

    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    Console console{output, files};
};

TEST_F(CompletionTest, NamesBeginningWithThePrefixInAnyCase) {
    EXPECT_EQ(names("sv_"), (std::vector<std::string>{"sv_cheats", "sv_fast", "sv_gravity", "SV_Restart"}))
        << "commands, cvars and aliases, in the order of their names without regard to case";
    EXPECT_EQ(names("SV_G"), (std::vector<std::string>{"sv_gravity"}));
    EXPECT_EQ(names("+att"), (std::vector<std::string>{"+attack"}));
    EXPECT_EQ(names("sv_", 2), (std::vector<std::string>{"sv_cheats", "sv_fast"}));
    EXPECT_TRUE(names("zz").empty());
    EXPECT_FALSE(names("").empty()) << "every name begins with nothing";
}

TEST_F(CompletionTest, TellsWhatEachNameIs) {
    const std::vector<Completion> found = console.completions("sv_", 100);
    ASSERT_EQ(found.size(), 4U);
    EXPECT_EQ(found[0].kind, NameKind::Cvar);
    EXPECT_EQ(found[0].value, "0");
    EXPECT_EQ(found[1].kind, NameKind::Alias);
    EXPECT_EQ(found[1].description, "sv_gravity 400") << "an alias's body";
    EXPECT_EQ(found[2].kind, NameKind::Cvar);
    EXPECT_EQ(found[2].description, "World gravity");
    EXPECT_EQ(found[2].value, "800");
    EXPECT_EQ(found[2].expectation, "a number");
    EXPECT_EQ(found[0].expectation, "0 or 1");
    EXPECT_EQ(found[3].kind, NameKind::Command);
    EXPECT_EQ(found[3].description, "Restarts the game");
    EXPECT_TRUE(found[3].value.empty());
}

TEST(CompletionPrefixTest, IsTheNameOfTheLastCommandWhileItIsTyped) {
    using nx::console::completionPrefix;
    const auto at = [](std::string_view line) -> std::optional<std::pair<std::size_t, std::string_view>> {
        const auto found = completionPrefix(line);
        return found ? std::optional{std::pair{found->offset, found->prefix}} : std::nullopt;
    };
    EXPECT_EQ(at("sv_gr"), (std::pair<std::size_t, std::string_view>{0, "sv_gr"}));
    EXPECT_EQ(at("  sv_gr"), (std::pair<std::size_t, std::string_view>{2, "sv_gr"}));
    EXPECT_EQ(at("echo a; sv_gr"), (std::pair<std::size_t, std::string_view>{8, "sv_gr"}));
    EXPECT_EQ(at("echo a;"), (std::pair<std::size_t, std::string_view>{7, ""}))
        << "a new command, nothing typed";
    EXPECT_EQ(at(""), (std::pair<std::size_t, std::string_view>{0, ""}));
    EXPECT_EQ(at("echo \"a; b\" ; sv"), (std::pair<std::size_t, std::string_view>{14, "sv"}))
        << "a ; in quotes ends no command";
    EXPECT_FALSE(at("sv_gravity 4").has_value()) << "the name is typed: its arguments are";
    EXPECT_FALSE(at("echo \"a; b").has_value()) << "still in quotes";
    EXPECT_FALSE(at("sv_gravity ").has_value());
}

TEST(CommandHistoryTest, WalksTheLinesSentFromTheNewest) {
    nx::console::CommandHistory history{3};
    EXPECT_FALSE(history.older().has_value()) << "no lines";
    for (const std::string_view line : {"one", "two", "two", "  ", "three", "four"}) {
        history.add(line);
    }
    EXPECT_EQ(std::vector<std::string>(history.lines().begin(), history.lines().end()),
              (std::vector<std::string>{"two", "three", "four"}))
        << "the newest line again and a blank one are not kept; the oldest goes for room";
    EXPECT_EQ(history.older(), "four");
    EXPECT_EQ(history.older(), "three");
    EXPECT_EQ(history.older(), "two");
    EXPECT_EQ(history.older(), "two") << "the oldest again";
    EXPECT_EQ(history.newer(), "three");
    EXPECT_EQ(history.newer(), "four");
    EXPECT_FALSE(history.newer().has_value()) << "back to the line being typed";
    EXPECT_FALSE(history.newer().has_value());
    EXPECT_EQ(history.older(), "four") << "a new walk";
    history.add("five");
    EXPECT_EQ(history.older(), "five") << "sending starts the walk again";
}

} // namespace
