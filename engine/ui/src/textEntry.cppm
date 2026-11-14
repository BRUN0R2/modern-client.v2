// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Text entries (ADR-0020): a line the player types in, as VGUI2's TextEntry is one, the first the
/// console's input line. It takes typed text (the platform's TextEntered) at its cursor and the keys
/// that edit it, as its line (nx.ui:textLine) does; what it draws stays within it, scrolled so that the
/// cursor shows.
export module nx.ui:textEntry;

import std;
import nx.core;
import nx.platform;
import nx.render;
import :geometry;
import :panel;
import :painter;
import :text;
import :textLine;

export namespace nx::ui {

/// A line of text the player types in: its text, its cursor among its characters, and what it shows
/// of them when they are wider than it.
class TextEntry : public Panel {
public:
    explicit TextEntry(std::string name, PixelRect bounds = {});

    /// The text, UTF-8 (TextLine::text).
    [[nodiscard]] inline auto text() const -> std::string { return line.text(); }
    /// Where the cursor is: how many characters are before it.
    [[nodiscard]] inline auto cursor() const noexcept -> std::size_t { return line.cursor(); }
    /// Replaces the text with `text` (UTF-8), the cursor after it.
    inline void setText(std::string_view text) { line.setText(text); }

    /// Inserts `text` at the cursor, as TextLine::insertText does.
    inline void insertText(std::string_view text) { line.insertText(text); }
    /// What `key` does, pressed, as TextLine::pressKey says.
    inline auto pressKey(platform::Key key) -> TextEntryAction { return line.pressKey(key); }

    inline void setFace(TextFace face) noexcept { m_face = face; }
    inline void setColors(render::ScreenColor text, render::ScreenColor cursor) noexcept {
        m_textColor = text;
        m_cursorColor = cursor;
    }
    /// How far the text is from the entry's left edge.
    inline void setTextInset(Pixels inset) noexcept { textInset = inset; }

    /// How far the text is scrolled left so that the cursor shows.
    [[nodiscard]] auto scroll() const -> Pixels;

protected:
    /// Its background, its text and its cursor, a pixel wide and a line tall.
    void paint(PanelPainter& painter) const override;

private:
    /// The pen's distance from the text's start to the cursor.
    [[nodiscard]] auto cursorPen() const -> Pixels;

    TextLine line;
    TextFace m_face{};
    render::ScreenColor m_textColor{};
    render::ScreenColor m_cursorColor{};
    Pixels textInset{};
};

} // namespace nx::ui
