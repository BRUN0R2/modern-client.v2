// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Binds: the commands that set them, what config.cfg keeps of them, Counter-Strike 1.6's, and the
// keys running them as they go down and up, as the original runs them.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.console;
import nx.settings;

namespace {

using nx::console::ConsoleError;
using nx::console::KeyId;
using nx::settings::KeyBindings;

/// A console's output, kept.
class RecordingOutput final : public nx::console::ConsoleOutput {
public:
    void print(std::string_view line) override { lines.emplace_back(line); }
    void reportFailure(const nx::Failure<ConsoleError>& failure) override {
        failures.push_back(failure.code);
    }

    std::vector<std::string> lines;
    std::vector<ConsoleError> failures;
};

/// No files.
class NoFiles final : public nx::console::ConfigFiles {
public:
    [[nodiscard]] auto read(std::string_view path)
        -> nx::Result<std::string, nx::console::ConfigFileError> override {
        return nx::fail(nx::console::ConfigFileError::NotFound, std::string{path});
    }
};

/// A console with the bind commands over `bindings`, and a button `jump`.
class Keys {
public:
    Keys() : console{output, files}, jump{console.registerButton({.name = "jump"}).value()} {
        nx::invariant(nx::settings::registerBindCommands(console, bindings).has_value(), "the bind commands");
    }

    /// Runs `script` now.
    void run(std::string_view script) {
        console.addText(script);
        std::ignore = console.execute();
    }

    RecordingOutput output;
    NoFiles files;
    nx::console::Console console;
    KeyBindings bindings;
    nx::console::Button jump;
};

[[nodiscard]] auto named(std::string_view name) -> KeyId {
    return nx::settings::keyNamed(name).value();
}

TEST(BindsTest, BindAndUnbindSetWhatKeysRun) {
    Keys keys;
    keys.run(
        "bind w +forward; bind MOUSE1 \"+attack\"; bind SPACE say hello  world; bind \"F\" \"impulse 100\"");
    EXPECT_EQ(keys.bindings.commandOf(named("w")), "+forward");
    EXPECT_EQ(keys.bindings.commandOf(named("MOUSE1")), "+attack");
    EXPECT_EQ(keys.bindings.commandOf(named("SPACE")), "say hello world") << "the words, joined by spaces";
    EXPECT_EQ(keys.bindings.commandOf(named("f")), "impulse 100");
    EXPECT_TRUE(keys.output.failures.empty());

    keys.run("bind w");
    keys.run("unbind w; bind w");
    EXPECT_EQ(keys.output.lines, (std::vector<std::string>{"\"w\" = \"+forward\"", "\"w\" is not bound"}));
    EXPECT_EQ(keys.bindings.commandOf(named("w")), "");

    keys.run("unbindall");
    for (std::uint32_t number = 0; number < nx::settings::kKeyCount; ++number) {
        EXPECT_EQ(keys.bindings.commandOf(KeyId{number}), "") << number;
    }
}

TEST(BindsTest, AWrongBindChangesNothing) {
    Keys keys;
    keys.run("bind; bind SPACEBAR +jump; bind x say\"quoted\"; unbind; unbind w s; unbind SPACEBAR");
    EXPECT_EQ(keys.output.failures, std::vector<ConsoleError>(6, ConsoleError::InvalidArguments));
    EXPECT_EQ(keys.bindings.commandOf(named("x")), "") << "config.cfg could not read it back";
}

TEST(BindsTest, ConfigCfgGivesBackTheBindsAndNoneOther) {
    Keys keys;
    keys.run(
        "bind SEMICOLON +mlook; bind \\ +attack2; bind MWHEELDOWN \"+jump; wait; -jump\"; bind a +moveleft");
    const std::string text = keys.bindings.configText();
    EXPECT_EQ(text, "unbindall\n"
                    "bind \"SEMICOLON\" \"+mlook\"\n"
                    "bind \"\\\" \"+attack2\"\n"
                    "bind \"a\" \"+moveleft\"\n"
                    "bind \"MWHEELDOWN\" \"+jump; wait; -jump\"\n")
        << "by the keys' numbers";

    Keys next;
    next.bindings = nx::settings::defaultBindings();
    next.run(text);
    for (std::uint32_t number = 0; number < nx::settings::kKeyCount; ++number) {
        EXPECT_EQ(next.bindings.commandOf(KeyId{number}), keys.bindings.commandOf(KeyId{number})) << number;
    }
}

TEST(BindsTest, BindlistListsTheBoundKeys) {
    Keys keys;
    keys.run("bind MOUSE1 +attack; bind e +use; bindlist");
    EXPECT_EQ(keys.output.lines,
              (std::vector<std::string>{"e \"+use\"", "MOUSE1 \"+attack\"", "2 keys bound"}));
}

TEST(BindsTest, CounterStrikesBindsWalkJumpAndUse) {
    const KeyBindings bindings = nx::settings::defaultBindings();
    EXPECT_EQ(bindings.commandOf(named("w")), "+forward");
    EXPECT_EQ(bindings.commandOf(named("s")), "+back");
    EXPECT_EQ(bindings.commandOf(named("a")), "+moveleft");
    EXPECT_EQ(bindings.commandOf(named("d")), "+moveright");
    EXPECT_EQ(bindings.commandOf(named("SPACE")), "+jump");
    EXPECT_EQ(bindings.commandOf(named("CTRL")), "+duck");
    EXPECT_EQ(bindings.commandOf(named("SHIFT")), "+speed");
    EXPECT_EQ(bindings.commandOf(named("e")), "+use");
    EXPECT_EQ(bindings.commandOf(named("MOUSE1")), "+attack");
    EXPECT_EQ(bindings.commandOf(named("1")), "slot1");
    EXPECT_EQ(bindings.commandOf(named("`")), "toggleconsole");
    EXPECT_EQ(bindings.commandOf(named("v")), "") << "left free";
    EXPECT_EQ(std::ranges::count(bindings.configText(), '\n'), 1 + 65) << "unbindall and its 65 binds";
}

TEST(BoundInputTest, AButtonIsHeldWhileItsKeyIsDown) {
    Keys keys;
    keys.run("bind SPACE +jump");
    nx::settings::BoundInput input{keys.console, keys.bindings};

    input.handle(nx::platform::KeyChanged{.key = nx::platform::Key::Space, .isDown = true});
    keys.run("");
    EXPECT_TRUE(keys.jump.isDown());
    input.handle(nx::platform::KeyChanged{.key = nx::platform::Key::Space, .isDown = true, .isRepeat = true});
    input.handle(nx::platform::KeyChanged{.key = nx::platform::Key::Space, .isDown = false});
    keys.run("");
    EXPECT_FALSE(keys.jump.isDown()) << "the repeat held nothing more";
    EXPECT_TRUE(keys.output.failures.empty());

    keys.run("+jump 32"); // as when SPACE went down before the input was read here
    input.release(named("SPACE"));
    keys.run("");
    EXPECT_TRUE(keys.jump.isDown()) << "a key that did not go down here runs nothing coming up";
}

TEST(BoundInputTest, TheKeysNumberFollowsTheWholeCommand) {
    Keys keys;
    keys.run("bind x \"+jump; -jump\"; bind f \"echo pressed\"");
    nx::settings::BoundInput input{keys.console, keys.bindings};

    // Down, `+jump; -jump 120`: the console holds the button, and the key releases nothing it holds.
    input.press(named("x"));
    keys.run("");
    EXPECT_TRUE(keys.jump.isDown());
    // Up, `-jump; -jump 120`: `-jump` without a key lets go of it.
    input.release(named("x"));
    keys.run("");
    EXPECT_FALSE(keys.jump.isDown());

    input.press(named("f"));
    input.release(named("f"));
    keys.run("");
    EXPECT_EQ(keys.output.lines, std::vector<std::string>{"pressed"}) << "once, as it went down";
}

TEST(BoundInputTest, AWheelNotchPressesAndReleasesItsKey) {
    Keys keys;
    keys.run("bind MWHEELDOWN +jump");
    nx::settings::BoundInput input{keys.console, keys.bindings};

    input.handle(nx::platform::MouseWheelMoved{.dy = -0.5F});
    keys.run("");
    EXPECT_FALSE(keys.jump.takeEdges().pressed) << "half a notch";
    input.handle(nx::platform::MouseWheelMoved{.dy = -0.5F});
    keys.run("");
    const nx::console::ButtonEdges edges = keys.jump.takeEdges();
    EXPECT_TRUE(edges.pressed);
    EXPECT_TRUE(edges.released);
    EXPECT_FALSE(keys.jump.isDown());
}

TEST(BoundInputTest, KeysComeUpWhenTheWindowLosesTheKeyboard) {
    Keys keys;
    keys.run("bind MOUSE2 +jump");
    nx::settings::BoundInput input{keys.console, keys.bindings};

    input.handle(
        nx::platform::MouseButtonChanged{.button = nx::platform::MouseButton::Right, .isDown = true});
    keys.run("");
    EXPECT_TRUE(keys.jump.isDown());
    input.handle(nx::platform::FocusChanged{.isFocused = false});
    keys.run("");
    EXPECT_FALSE(keys.jump.isDown());
}

} // namespace
