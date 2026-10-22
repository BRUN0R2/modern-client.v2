// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Text entries (ADR-0020): a line the player types in, as VGUI2's TextEntry is one, the first the
/// console's input line. It takes typed text (the platform's TextEntered) at its cursor and the keys
/// that edit it; what it draws stays within it, scrolled so that the cursor shows.
export module nx.ui:textEntry;

import std;
import nx.core;
import nx.platform;
import nx.render;
import :panel;
import :painter;
import :text;

export namespace nx::ui {

/// What a key did to a text entry.
enum class TextEntryAction : std::uint8_t {
    None,      ///< Nothing a text entry does: the key is someone else's.
    Edited,    ///< The text or the cursor changed.
    Submitted, ///< Enter: the line is the owner's to take.
};

/// A line of text the player types in: its text, its cursor among its characters, and what it shows
/// of them when they are wider than it.
class TextEntry : public Panel {
public:
    explicit TextEntry(std::string name, PanelBounds bounds = {});

    /// The text, UTF-8.
    [[nodiscard]] auto text() const -> std::string;
    /// Where the cursor is: how many characters are before it.
    [[nodiscard]] inline auto cursor() const noexcept -> std::size_t { return m_cursor; }
    /// Replaces the text with `text` (UTF-8), the cursor after it.
    void setText(std::string_view text);

    /// Inserts `text` (UTF-8, as typed) at the cursor, the cursor after it; control characters left
    /// out.
    void insertText(std::string_view text);
    /// What `key` does, pressed: Left and Right move the cursor by a character, Home and End to the
    /// ends, Backspace and Delete remove the character before or after it, Enter (either) submits.
    auto pressKey(platform::Key key) -> TextEntryAction;

    inline void setFace(TextFace face) noexcept { m_face = face; }
    inline void setColors(render::ScreenColor text, render::ScreenColor cursor) noexcept {
        m_textColor = text;
        m_cursorColor = cursor;
    }
    /// The pixels between the entry's left edge and its text.
    inline void setTextInset(std::int32_t inset) noexcept { m_inset = inset; }

    /// How far the text is scrolled left so that the cursor shows, in pixels.
    [[nodiscard]] auto scroll() const -> std::int32_t;

protected:
    /// Its background, its text and its cursor, a pixel wide and a line tall.
    void paint(PanelPainter& painter) const override;

private:
    /// The pen's distance from the text's start to the cursor.
    [[nodiscard]] auto cursorPen() const -> std::int32_t;

    std::u32string m_characters;
    std::size_t m_cursor = 0;
    TextFace m_face{};
    render::ScreenColor m_textColor{};
    render::ScreenColor m_cursorColor{};
    std::int32_t m_inset = 0;
};

} // namespace nx::ui
