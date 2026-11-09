// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ui;

import std;
import nx.core;
import nx.render;

namespace nx::ui {

namespace {

/// An inset on each side of the text: left and right, top and bottom.
constexpr std::int32_t kInsetSides = 2;

/// The rows `line` makes `width` wide with `atlas`'s glyphs (wrapText); without an atlas, the
/// line is one row.
[[nodiscard]] auto wrapped(const RichTextLine& line, const GlyphAtlas* atlas, Pixels width)
    -> std::vector<RichTextLine> {
    if (atlas == nullptr) {
        return {line};
    }
    std::vector<RichTextLine> rows;
    for (std::string& row : wrapText(*atlas, line.text, width)) {
        rows.push_back({.text = std::move(row), .color = line.color});
    }
    return rows;
}

} // namespace

RichText::RichText(std::string name, PixelRect bounds, std::size_t capacity)
    : Panel{std::move(name), bounds}, m_capacity{std::max<std::size_t>(capacity, 1)} {}

void RichText::append(RichTextLine line) {
    const Pixels width = bounds().wide - (m_inset * kInsetSides);
    if (m_scrolled > 0) {
        m_scrolled += wrapped(line, m_face.atlas, width).size(); // the rows shown stay put
    }
    m_lines.push_back(std::move(line));
    if (m_lines.size() > m_capacity) {
        m_lines.pop_front();
        scrollBy(0); // within what is left
    }
}

void RichText::clear() noexcept {
    m_lines.clear();
    m_scrolled = 0;
}

auto RichText::rows() const -> std::vector<RichTextLine> {
    const Pixels width = bounds().wide - (m_inset * kInsetSides);
    std::vector<RichTextLine> all;
    for (const RichTextLine& line : m_lines) {
        std::ranges::move(wrapped(line, m_face.atlas, width), std::back_inserter(all));
    }
    return all;
}

auto RichText::visibleRows() const -> std::size_t {
    if (m_face.atlas == nullptr || m_face.atlas->lineHeight() <= Pixels{}) {
        return 0;
    }
    return narrow<std::size_t>(
        std::max(0, (bounds().tall - (m_inset * kInsetSides)) / m_face.atlas->lineHeight()));
}

void RichText::scrollBy(std::int64_t rows) {
    const std::size_t total = this->rows().size();
    const std::size_t furthest = total > visibleRows() ? total - visibleRows() : 0;
    const std::int64_t wanted = narrow<std::int64_t>(m_scrolled) + rows;
    m_scrolled = narrow<std::size_t>(std::clamp<std::int64_t>(wanted, 0, narrow<std::int64_t>(furthest)));
}

void RichText::paint(PanelPainter& painter) const {
    Panel::paint(painter);
    const std::size_t shown = visibleRows();
    if (shown == 0 || m_lines.empty()) {
        return;
    }
    // The newest lines' rows, back to the oldest row shown.
    const Pixels width = bounds().wide - (m_inset * kInsetSides);
    const std::size_t needed = m_scrolled + shown;
    std::vector<RichTextLine> newest; // newest row first
    for (auto line = m_lines.rbegin(); line != m_lines.rend() && newest.size() < needed; ++line) {
        std::vector<RichTextLine> lineRows = wrapped(*line, m_face.atlas, width);
        std::ranges::move(lineRows | std::views::reverse, std::back_inserter(newest));
    }
    const Pixels height = m_face.atlas->lineHeight();
    for (std::size_t row = m_scrolled; row < std::min(needed, newest.size()); ++row) {
        const auto fromBottom = narrow<std::int32_t>(row - m_scrolled + 1);
        std::ignore = painter.text(m_face, newest[row].text,
                                   {.x = m_inset, .y = bounds().tall - m_inset - (height * fromBottom)},
                                   newest[row].color); // where its pen ends is no matter
    }
}

} // namespace nx::ui
