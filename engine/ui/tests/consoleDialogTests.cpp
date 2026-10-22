// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The console's window: lines typed and run, the names offered as a name is typed and Tab's taking
// one, the history, the output in the colour of each kind of line, and what is logged from any
// thread.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;
import nx.platform;
import nx.render;
import nx.ui;

#include "consoleFixtures.hpp"
#include "squareFont.hpp"

namespace {

using nx::console::CommandLine;
using nx::console::Console;
using nx::console::ConsoleError;
using nx::console::CvarDesc;
using nx::platform::Key;
using nx::ui::ConsoleColors;
using nx::ui::ConsoleDialog;

/// A console of three names beginning with sv_, and its window, 400 by 300.
class ConsoleDialogTest : public testing::Test {
protected:
    ConsoleDialogTest() {
        EXPECT_TRUE(
            console.registerCvar(CvarDesc<float>{.name = "sv_gravity", .defaultValue = 800}).has_value());
        EXPECT_TRUE(console.registerCvar(CvarDesc<bool>{.name = "sv_cheats"}).has_value());
        EXPECT_TRUE(console
                        .registerCommand(
                            {.name = "SV_Restart", .description = "Restarts the game"},
                            [](Console&, const CommandLine&) -> nx::Result<void, ConsoleError> { return {}; })
                        .has_value());
    }

    /// Types `line` and sends it.
    void send(std::string_view line) {
        dialog.insertText(line);
        EXPECT_TRUE(dialog.pressKey(Key::Enter));
    }

    [[nodiscard]] auto lastLine() const -> const nx::ui::RichTextLine& {
        return dialog.output().lines().back();
    }

    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    Console console{output, files};
    ConsoleDialog dialog{console, {.wide = 400, .tall = 300}};
    ConsoleColors colors{};
};

TEST_F(ConsoleDialogTest, ALineTypedIsShownAndRunInTheConsolesTurn) {
    send("sv_gravity 400");
    EXPECT_EQ(lastLine().text, "] sv_gravity 400");
    EXPECT_EQ(lastLine().color, colors.typed);
    EXPECT_TRUE(dialog.input().text().empty());
    EXPECT_EQ(console.findCvar("sv_gravity")->text(), "800") << "queued, not run yet";
    std::ignore = console.execute();
    EXPECT_EQ(console.findCvar("sv_gravity")->text(), "400");
}

TEST_F(ConsoleDialogTest, TheNamesBeingTypedCompletesToAreOffered) {
    dialog.insertText("sv_");
    ASSERT_EQ(dialog.offered().size(), 3U);
    EXPECT_EQ(dialog.offered()[1].name, "sv_gravity");
    EXPECT_FALSE(dialog.chosen().has_value());
    EXPECT_TRUE(dialog.pressKey(Key::Down));
    EXPECT_TRUE(dialog.pressKey(Key::Down));
    EXPECT_EQ(dialog.chosen(), 1U);
    EXPECT_TRUE(dialog.pressKey(Key::Up));
    EXPECT_TRUE(dialog.pressKey(Key::Up));
    EXPECT_EQ(dialog.chosen(), 2U) << "from the first back round to the last";
    EXPECT_TRUE(dialog.pressKey(Key::Down));
    EXPECT_TRUE(dialog.pressKey(Key::Down));
    EXPECT_TRUE(dialog.pressKey(Key::Tab));
    EXPECT_EQ(dialog.input().text(), "sv_gravity ") << "the one chosen, and a space for what follows";
    EXPECT_TRUE(dialog.offered().empty());

    dialog.insertText("1; sv_r");
    ASSERT_EQ(dialog.offered().size(), 1U) << "the last command's name";
    EXPECT_TRUE(dialog.pressKey(Key::Tab));
    EXPECT_EQ(dialog.input().text(), "sv_gravity 1; SV_Restart ") << "the first offered, without a choice";
    dialog.insertText("x");
    EXPECT_TRUE(dialog.offered().empty()) << "an argument: no name to complete";
}

TEST_F(ConsoleDialogTest, UpAndDownWalkTheLinesSent) {
    send("echo one");
    send("echo two");
    EXPECT_TRUE(dialog.pressKey(Key::Up));
    EXPECT_EQ(dialog.input().text(), "echo two");
    EXPECT_TRUE(dialog.pressKey(Key::Up));
    EXPECT_EQ(dialog.input().text(), "echo one");
    EXPECT_TRUE(dialog.pressKey(Key::Down));
    EXPECT_EQ(dialog.input().text(), "echo two");
    EXPECT_TRUE(dialog.pressKey(Key::Down));
    EXPECT_TRUE(dialog.input().text().empty()) << "past the newest, back to an empty line";
}

TEST_F(ConsoleDialogTest, EachKindOfLineInItsColour) {
    dialog.print("sv_gravity is \"800\"");
    EXPECT_EQ(lastLine().color, colors.reply);
    dialog.showLogged(nx::log::Level::Warning, "[assets] a texture is missing");
    EXPECT_EQ(lastLine().color, colors.warning);
    dialog.showLogged(nx::log::Level::Debug, "[render] 120 batches");
    EXPECT_EQ(lastLine().color, colors.debug);
    dialog.reportFailure(nx::fail(ConsoleError::UnknownCommand, "sv_gravty").error());
    EXPECT_EQ(lastLine().color, colors.error);
    EXPECT_NE(lastLine().text.find("sv_gravty"), std::string::npos) << lastLine().text;
}

TEST_F(ConsoleDialogTest, LinesLoggedOnAnyThreadShowInTheirOrder) {
    nx::ui::ConsoleLogSink sink;
    std::thread logging{[&sink] {
        for (const std::string_view message : {"first", "second"}) {
            sink.write({.level = nx::log::Level::Info, .category = "net", .message = message});
        }
    }};
    logging.join();
    sink.showIn(dialog);
    ASSERT_EQ(dialog.output().lines().size(), 2U);
    EXPECT_EQ(dialog.output().lines()[0].text, "[net] first");
    EXPECT_EQ(dialog.output().lines()[1].text, "[net] second");
    sink.showIn(dialog);
    EXPECT_EQ(dialog.output().lines().size(), 2U) << "each shown once";
}

TEST_F(ConsoleDialogTest, PageKeysScrollTheOutputAndOtherKeysAreLeft) {
    auto font = nx::ui::Font::open(
        std::make_shared<const std::vector<std::byte>>(nx::test::SquareFont::build()), {.cellHeight = 20});
    ASSERT_TRUE(font.has_value());
    auto atlas = nx::ui::GlyphAtlas::build(*font, U"AB ");
    ASSERT_TRUE(atlas.has_value());
    dialog.setFace({.atlas = &*atlas});
    for (int line = 0; line < 40; ++line) {
        dialog.print("A");
    }
    EXPECT_TRUE(dialog.pressKey(Key::PageUp));
    EXPECT_GT(dialog.output().scrolled(), 0U);
    EXPECT_TRUE(dialog.pressKey(Key::PageDown));
    EXPECT_EQ(dialog.output().scrolled(), 0U);
    send("A");
    EXPECT_EQ(dialog.output().scrolled(), 0U) << "a line sent brings the newest back";

    EXPECT_FALSE(dialog.pressKey(Key::Escape)) << "the owner's: it closes the console";
    EXPECT_FALSE(dialog.pressKey(Key::Grave));
    EXPECT_FALSE(dialog.pressKey(Key::A)) << "letters come as typed text";
}

} // namespace
