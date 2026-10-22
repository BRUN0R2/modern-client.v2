// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ui;

import std;
import nx.core;
import nx.render;

namespace nx::ui {

namespace {

/// The UTF-8 of `characters`.
[[nodiscard]] auto utf8Text(std::u32string_view characters) -> std::string {
    std::string text;
    for (const char32_t character : characters) {
        appendUtf8(text, character);
    }
    return text;
}

/// The rows `line` makes `width` pixels wide with `atlas`'s glyphs: broken at the last space that
/// fits, the space left out, or before the character that does not fit when no space does; one
/// empty row for an empty line. Without room, the line is one row.
[[nodiscard]] auto wrapped(const RichTextLine& line, const GlyphAtlas* atlas, std::int32_t width)
    -> std::vector<RichTextLine> {
    if (atlas == nullptr || width <= 0) {
        return {line};
    }
    const std::u32string characters = decodeUtf8(line.text);
    std::vector<RichTextLine> rows;
    std::size_t start = 0;
    std::optional<std::size_t> space; // the last space of the row
    std::int32_t pen = 0;
    for (std::size_t index = 0; index < characters.size(); ++index) {
        const AtlasGlyph* glyph = atlas->glyph(characters[index]);
        const std::int32_t advance = glyph != nullptr ? glyph->advance : 0;
        if (pen + advance > width && index > start) {
            const std::size_t end = space.value_or(index);
            rows.push_back({.text = utf8Text(std::u32string_view{characters}.substr(start, end - start)),
                            .color = line.color});
            start = space ? end + 1 : end;
            space.reset();
            pen = 0;
            for (std::size_t carried = start; carried < index; ++carried) {
                const AtlasGlyph* earlier = atlas->glyph(characters[carried]);
                pen += earlier != nullptr ? earlier->advance : 0;
            }
        }
        if (characters[index] == U' ') {
            space = index;
        }
        pen += advance;
    }
    rows.push_back({.text = utf8Text(std::u32string_view{characters}.substr(start)), .color = line.color});
    return rows;
}

} // namespace

RichText::RichText(std::string name, PanelBounds bounds, std::size_t capacity)
    : Panel{std::move(name), bounds}, m_capacity{std::max<std::size_t>(capacity, 1)} {}

void RichText::append(RichTextLine line) {
    const std::int32_t width = bounds().wide - (2 * m_inset);
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
    const std::int32_t width = bounds().wide - (2 * m_inset);
    std::vector<RichTextLine> all;
    for (const RichTextLine& line : m_lines) {
        std::ranges::move(wrapped(line, m_face.atlas, width), std::back_inserter(all));
    }
    return all;
}

auto RichText::visibleRows() const -> std::size_t {
    if (m_face.atlas == nullptr || m_face.atlas->metrics().height() <= 0) {
        return 0;
    }
    return narrow<std::size_t>(
        std::max(0, (bounds().tall - (2 * m_inset)) / m_face.atlas->metrics().height()));
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
    const std::int32_t width = bounds().wide - (2 * m_inset);
    const std::size_t needed = m_scrolled + shown;
    std::vector<RichTextLine> newest; // newest row first
    for (auto line = m_lines.rbegin(); line != m_lines.rend() && newest.size() < needed; ++line) {
        std::vector<RichTextLine> lineRows = wrapped(*line, m_face.atlas, width);
        std::ranges::move(lineRows | std::views::reverse, std::back_inserter(newest));
    }
    const std::int32_t height = m_face.atlas->metrics().height();
    for (std::size_t row = m_scrolled; row < std::min(needed, newest.size()); ++row) {
        const auto fromBottom = narrow<std::int32_t>(row - m_scrolled + 1);
        std::ignore = painter.text(m_face, newest[row].text, m_inset,
                                   bounds().tall - m_inset - (fromBottom * height), newest[row].color);
    }
}

} // namespace nx::ui
