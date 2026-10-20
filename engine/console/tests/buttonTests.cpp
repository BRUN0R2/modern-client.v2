// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;

#include "consoleFixtures.hpp"

namespace {

using nx::console::Button;
using nx::console::ButtonEdges;
using nx::console::Console;
using nx::console::ConsoleError;

class ButtonTest : public testing::Test {
protected:
    ButtonTest() {
        auto registered = console.registerButton({.name = "jump", .description = "Jump"});
        EXPECT_TRUE(registered.has_value());
        if (registered) {
            jump.emplace(*registered);
        }
    }

    void run(std::string_view script) {
        console.addText(script);
        std::ignore = console.execute(); // the scripts here have no wait
    }

    [[nodiscard]] auto edges() -> ButtonEdges { return jump->takeEdges(); }

    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    Console console{output, files};
    std::optional<Button> jump;
};

TEST_F(ButtonTest, RegistersAPlusAndAMinusCommand) {
    EXPECT_TRUE(console.hasCommand("+jump"));
    EXPECT_TRUE(console.hasCommand("-jump"));
    EXPECT_FALSE(jump->isDown());
    EXPECT_EQ(console.registerButton({.name = "jump"}).error().code, ConsoleError::NameTaken);
    EXPECT_EQ(console.registerButton({.name = "two words"}).error().code, ConsoleError::InvalidName);
}

TEST_F(ButtonTest, AKeyHoldsTheButtonUntilItComesUp) {
    run("+jump 32");
    EXPECT_TRUE(jump->isDown());
    const ButtonEdges pressed = edges();
    EXPECT_TRUE(pressed.pressed);
    EXPECT_FALSE(pressed.released);

    run("+jump 32"); // auto-repeat
    EXPECT_FALSE(edges().pressed);

    run("-jump 32");
    EXPECT_FALSE(jump->isDown());
    EXPECT_TRUE(edges().released);
}

TEST_F(ButtonTest, TwoKeysHoldItUntilBothComeUp) {
    run("+jump 32; +jump 200; -jump 32");
    EXPECT_TRUE(jump->isDown());
    run("-jump 7"); // a key that does not hold it
    EXPECT_TRUE(jump->isDown());
    run("-jump 200");
    EXPECT_FALSE(jump->isDown());
}

TEST_F(ButtonTest, APressAndReleaseInOneFrameIsNotLost) {
    // A bind on the mouse wheel runs both commands before the next user command.
    run("+jump 239; -jump 239");
    EXPECT_FALSE(jump->isDown());
    const ButtonEdges both = edges();
    EXPECT_TRUE(both.pressed);
    EXPECT_TRUE(both.released);
    EXPECT_FALSE(edges().pressed) << "edges are taken once";
}

TEST_F(ButtonTest, TypedWithoutAKeyTheConsoleHoldsItAndMinusUnsticksEverything) {
    run("+jump");
    EXPECT_TRUE(jump->isDown());
    run("+jump 32; -jump 32");
    EXPECT_TRUE(jump->isDown()) << "still held by the console";
    run("+jump 32; -jump");
    EXPECT_FALSE(jump->isDown());
}

TEST_F(ButtonTest, AliasesWithPlusAndMinusNames) {
    auto duck = console.registerButton({.name = "duck"});
    ASSERT_TRUE(duck.has_value());
    run(R"(alias +duckJump "+jump; +duck"; alias -duckJump "-jump; -duck")");
    run("+duckJump 32");
    EXPECT_TRUE(jump->isDown());
    EXPECT_TRUE(duck->isDown());
    run("-duckJump 32");
    EXPECT_FALSE(jump->isDown());
    EXPECT_FALSE(duck->isDown());
    EXPECT_TRUE(output.failures.empty());
}

TEST_F(ButtonTest, TheKeyMustBeANumber) {
    run("+jump space; +jump -1; +jump 1 2");
    EXPECT_EQ(output.failures.size(), 3U);
    EXPECT_FALSE(jump->isDown());
}

} // namespace
