// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ui;

import std;
import nx.core;
import nx.platform;

namespace nx::ui {

namespace {

/// Whether `character` is a control character (C0, DEL or C1), which a line of text does not hold.
[[nodiscard]] constexpr auto isControl(char32_t character) noexcept -> bool {
    constexpr char32_t kSpace = 0x20;
    constexpr char32_t kDelete = 0x7F;
    constexpr char32_t kFirstPrintableAfterC1 = 0xA0;
    return character < kSpace || (character >= kDelete && character < kFirstPrintableAfterC1);
}

/// The UTF-8 of `characters`.
[[nodiscard]] auto utf8Of(std::u32string_view characters) -> std::string {
    std::string text;
    for (const char32_t character : characters) {
        appendUtf8(text, character);
    }
    return text;
}

} // namespace

auto TextLine::text() const -> std::string {
    return utf8Of(characters);
}

auto TextLine::textBeforeCursor() const -> std::string {
    return utf8Of(std::u32string_view{characters}.substr(0, cursorAt));
}

void TextLine::setText(std::string_view text) {
    characters.clear();
    cursorAt = 0;
    insertText(text);
}

void TextLine::insertText(std::string_view text) {
    std::u32string typed = decodeUtf8(text);
    std::erase_if(typed, isControl);
    characters.insert(cursorAt, typed);
    cursorAt += typed.size();
}

auto TextLine::pressKey(platform::Key key) -> TextEntryAction {
    switch (key) {
    case platform::Key::Left:
        cursorAt -= cursorAt > 0 ? 1U : 0U;
        return TextEntryAction::Edited;
    case platform::Key::Right:
        cursorAt += cursorAt < characters.size() ? 1U : 0U;
        return TextEntryAction::Edited;
    case platform::Key::Home:
        cursorAt = 0;
        return TextEntryAction::Edited;
    case platform::Key::End:
        cursorAt = characters.size();
        return TextEntryAction::Edited;
    case platform::Key::Backspace:
        if (cursorAt > 0) {
            characters.erase(--cursorAt, 1);
        }
        return TextEntryAction::Edited;
    case platform::Key::Delete:
        if (cursorAt < characters.size()) {
            characters.erase(cursorAt, 1);
        }
        return TextEntryAction::Edited;
    case platform::Key::Enter:
    case platform::Key::KeypadEnter:
        return TextEntryAction::Submitted;
    default:
        return TextEntryAction::None;
    }
}

} // namespace nx::ui
