// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ui;

import std;
import nx.core;
import nx.platform;
import nx.render;

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

TextEntry::TextEntry(std::string name, PanelBounds bounds) : Panel{std::move(name), bounds} {}

auto TextEntry::text() const -> std::string {
    return utf8Of(m_characters);
}

void TextEntry::setText(std::string_view text) {
    m_characters.clear();
    m_cursor = 0;
    insertText(text);
}

void TextEntry::insertText(std::string_view text) {
    std::u32string typed = decodeUtf8(text);
    std::erase_if(typed, isControl);
    m_characters.insert(m_cursor, typed);
    m_cursor += typed.size();
}

auto TextEntry::pressKey(platform::Key key) -> TextEntryAction {
    switch (key) {
    case platform::Key::Left:
        m_cursor -= m_cursor > 0 ? 1U : 0U;
        return TextEntryAction::Edited;
    case platform::Key::Right:
        m_cursor += m_cursor < m_characters.size() ? 1U : 0U;
        return TextEntryAction::Edited;
    case platform::Key::Home:
        m_cursor = 0;
        return TextEntryAction::Edited;
    case platform::Key::End:
        m_cursor = m_characters.size();
        return TextEntryAction::Edited;
    case platform::Key::Backspace:
        if (m_cursor > 0) {
            m_characters.erase(--m_cursor, 1);
        }
        return TextEntryAction::Edited;
    case platform::Key::Delete:
        if (m_cursor < m_characters.size()) {
            m_characters.erase(m_cursor, 1);
        }
        return TextEntryAction::Edited;
    case platform::Key::Enter:
    case platform::Key::KeypadEnter:
        return TextEntryAction::Submitted;
    default:
        return TextEntryAction::None;
    }
}

auto TextEntry::cursorPen() const -> std::int32_t {
    return m_face.atlas != nullptr
               ? widthOf(*m_face.atlas, utf8Of(std::u32string_view{m_characters}.substr(0, m_cursor)))
               : 0;
}

auto TextEntry::scroll() const -> std::int32_t {
    // The cursor is a pixel wide: it shows when its pixel is within the entry.
    const std::int32_t room = bounds().wide - m_inset - 1;
    return std::max(0, cursorPen() - room);
}

void TextEntry::paint(PanelPainter& painter) const {
    Panel::paint(painter);
    if (m_face.atlas == nullptr) {
        return;
    }
    const std::int32_t height = m_face.atlas->metrics().height();
    const std::int32_t top = (bounds().tall - height) / 2;
    const std::int32_t start = m_inset - scroll();
    std::ignore = painter.text(m_face, text(), start, top, m_textColor);
    painter.fill(start + cursorPen(), top, 1, height, m_cursorColor);
}

} // namespace nx::ui
