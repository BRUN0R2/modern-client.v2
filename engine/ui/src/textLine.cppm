// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// A line of text being typed (ADR-0020): its characters and the cursor among them, as the text typed (the
/// platform's TextEntered) and the keys that edit it change them; and how a line is scrolled so that its
/// cursor shows. What draws it is apart: a text entry (nx.ui:textEntry), or a menu of its own.
export module nx.ui:textLine;

import std;
import nx.core;
import nx.platform;

export namespace nx::ui {

/// What a key did to a line of text.
enum class TextEntryAction : std::uint8_t {
    None,      ///< Nothing a line does: the key is someone else's.
    Edited,    ///< The text or the cursor changed.
    Submitted, ///< Enter: the line is the owner's to take.
};

/// How wide a line's cursor is drawn.
inline constexpr Pixels kTextCursorWidth{1};

/// How far a line is scrolled left so that its cursor, `cursorPen` from the line's start, shows within
/// `room` from it: none while it does.
[[nodiscard]] constexpr auto scrollToShow(Pixels cursorPen, Pixels room) noexcept -> Pixels {
    return std::max(Pixels{}, cursorPen - (room - kTextCursorWidth));
}

/// A line of text the player types: its characters, and its cursor among them.
class TextLine {
public:
    /// The text, UTF-8.
    [[nodiscard]] auto text() const -> std::string;
    /// The characters before the cursor, UTF-8: what the cursor's pen is measured by.
    [[nodiscard]] auto textBeforeCursor() const -> std::string;
    /// Where the cursor is: how many characters are before it.
    [[nodiscard]] inline auto cursor() const noexcept -> std::size_t { return cursorAt; }
    /// Replaces the text with `text` (UTF-8), the cursor after it.
    void setText(std::string_view text);

    /// Inserts `text` (UTF-8, as typed) at the cursor, the cursor after it; control characters left out.
    void insertText(std::string_view text);
    /// What `key` does, pressed: Left and Right move the cursor by a character, Home and End to the ends,
    /// Backspace and Delete remove the character before or after it, Enter (either) submits.
    auto pressKey(platform::Key key) -> TextEntryAction;

private:
    std::u32string characters;
    std::size_t cursorAt = 0;
};

} // namespace nx::ui
