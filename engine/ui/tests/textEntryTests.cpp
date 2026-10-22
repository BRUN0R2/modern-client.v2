// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// A text entry: typed text at its cursor, the keys that edit it, and the scroll that keeps its
// cursor in sight.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.ui;

#include "squareFont.hpp"

namespace {

using nx::platform::Key;
using nx::ui::TextEntry;
using nx::ui::TextEntryAction;

TEST(TextEntryTest, TypedTextGoesAtTheCursor) {
    TextEntry entry{"Input"};
    entry.insertText("abc");
    EXPECT_EQ(entry.text(), "abc");
    EXPECT_EQ(entry.cursor(), 3U);
    EXPECT_EQ(entry.pressKey(Key::Left), TextEntryAction::Edited);
    std::ignore = entry.pressKey(Key::Left);
    entry.insertText("\xC3\xA9");
    EXPECT_EQ(entry.text(), "a\xC3\xA9"
                            "bc")
        << "a character, not a byte";
    EXPECT_EQ(entry.cursor(), 2U);
    entry.insertText("x\ty\n");
    EXPECT_EQ(entry.text(), "a\xC3\xA9xybc") << "control characters left out";
}

TEST(TextEntryTest, KeysMoveTheCursorAndRemoveCharacters) {
    TextEntry entry{"Input"};
    entry.setText("abcd");
    EXPECT_EQ(entry.cursor(), 4U) << "after the text";
    std::ignore = entry.pressKey(Key::Backspace);
    EXPECT_EQ(entry.text(), "abc");
    std::ignore = entry.pressKey(Key::Home);
    EXPECT_EQ(entry.cursor(), 0U);
    std::ignore = entry.pressKey(Key::Backspace);
    EXPECT_EQ(entry.text(), "abc") << "nothing before the cursor";
    std::ignore = entry.pressKey(Key::Delete);
    EXPECT_EQ(entry.text(), "bc");
    std::ignore = entry.pressKey(Key::Right);
    std::ignore = entry.pressKey(Key::Right);
    std::ignore = entry.pressKey(Key::Right);
    EXPECT_EQ(entry.cursor(), 2U) << "no further than the end";
    std::ignore = entry.pressKey(Key::Delete);
    EXPECT_EQ(entry.text(), "bc") << "nothing after the cursor";
    std::ignore = entry.pressKey(Key::End);
    EXPECT_EQ(entry.cursor(), 2U);
}

TEST(TextEntryTest, EnterSubmitsAndOtherKeysAreLeft) {
    TextEntry entry{"Input"};
    entry.setText("map de_dust2");
    EXPECT_EQ(entry.pressKey(Key::Enter), TextEntryAction::Submitted);
    EXPECT_EQ(entry.pressKey(Key::KeypadEnter), TextEntryAction::Submitted);
    EXPECT_EQ(entry.pressKey(Key::A), TextEntryAction::None) << "letters come as typed text";
    EXPECT_EQ(entry.pressKey(Key::Up), TextEntryAction::None) << "the console's history, not the entry's";
    EXPECT_EQ(entry.text(), "map de_dust2");
}

TEST(TextEntryTest, ScrollsSoThatTheCursorShows) {
    auto font = nx::ui::Font::open(
        std::make_shared<const std::vector<std::byte>>(nx::test::SquareFont::build()), {.cellHeight = 20});
    ASSERT_TRUE(font.has_value());
    auto atlas = nx::ui::GlyphAtlas::build(*font, U"A");
    ASSERT_TRUE(atlas.has_value());
    TextEntry entry{"Input", {.wide = 30, .tall = 24}};
    entry.setFace({.atlas = &*atlas});
    entry.setTextInset(2);
    entry.setText("AA");
    EXPECT_EQ(entry.scroll(), 0) << "the cursor at 2 + 20, within 30";
    entry.setText("AAAA");
    EXPECT_EQ(entry.scroll(), 13) << "the cursor's pixel at 2 + 40 - 13, the entry's last";
    std::ignore = entry.pressKey(Key::Home);
    EXPECT_EQ(entry.scroll(), 0);
}

} // namespace
