// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ui;

import std;
import nx.core;
import nx.platform;
import nx.render;

namespace nx::ui {

namespace {

/// What the room left is divided by to put the line halfway down the entry.
constexpr std::int32_t kMiddleDivisor = 2;

} // namespace

TextEntry::TextEntry(std::string name, PixelRect bounds) : Panel{std::move(name), bounds} {}

auto TextEntry::cursorPen() const -> Pixels {
    return m_face.atlas != nullptr ? widthOf(*m_face.atlas, line.textBeforeCursor()) : Pixels{};
}

auto TextEntry::scroll() const -> Pixels {
    return scrollToShow(cursorPen(), bounds().wide - textInset);
}

void TextEntry::paint(PanelPainter& painter) const {
    Panel::paint(painter);
    if (m_face.atlas == nullptr) {
        return;
    }
    const Pixels height = m_face.atlas->lineHeight();
    const Pixels top = (bounds().tall - height) / kMiddleDivisor;
    const Pixels start = textInset - scroll();
    std::ignore =
        painter.text(m_face, text(), {.x = start, .y = top}, m_textColor); // the cursor's pen is known
    painter.fill({.x = start + cursorPen(), .y = top, .wide = kTextCursorWidth, .tall = height},
                 m_cursorColor);
}

} // namespace nx::ui
