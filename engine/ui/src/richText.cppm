// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Rich text (ADR-0020): lines of text, each in its colour, wrapped to the panel's width and
/// scrolled, as VGUI2's RichText shows the console's output.
export module nx.ui:richText;

import std;
import nx.core;
import nx.render;
import :panel;
import :painter;
import :text;

export namespace nx::ui {

/// How many lines rich text keeps unless told otherwise: the oldest go first.
inline constexpr std::size_t kRichTextLines = 1024;

/// A line of rich text: its UTF-8 text and its colour.
struct RichTextLine {
    std::string text{};
    render::ScreenColor color{};
};

/// Lines of text, the newest at the bottom, each wrapped at its last space that fits the panel's
/// width (anywhere when a word is wider), scrolled up from the newest by whole rows.
class RichText : public Panel {
public:
    explicit RichText(std::string name, PanelBounds bounds = {}, std::size_t capacity = kRichTextLines);

    /// Adds `line` at the bottom; the oldest goes when there are more than the capacity. A text
    /// scrolled up stays on the rows it shows.
    void append(RichTextLine line);
    void clear() noexcept;
    [[nodiscard]] inline auto lines() const noexcept -> const std::deque<RichTextLine>& { return m_lines; }

    inline void setFace(TextFace face) noexcept { m_face = face; }
    /// The pixels between the panel's edges and its text, on each side.
    inline void setTextInset(std::int32_t inset) noexcept { m_inset = inset; }

    /// The rows the lines make at the panel's width, oldest first: each a line's text or a part of it.
    [[nodiscard]] auto rows() const -> std::vector<RichTextLine>;
    /// How many rows the panel shows at once.
    [[nodiscard]] auto visibleRows() const -> std::size_t;
    /// Scrolls up by `rows` (down when negative), never past the oldest row nor below the newest.
    void scrollBy(std::int64_t rows);
    /// How many rows up from the newest it is scrolled; 0 shows the newest at the bottom.
    [[nodiscard]] inline auto scrolled() const noexcept -> std::size_t { return m_scrolled; }

protected:
    /// Its background, then the rows it shows, the lowest at its bottom.
    void paint(PanelPainter& painter) const override;

private:
    std::deque<RichTextLine> m_lines;
    std::size_t m_capacity;
    std::size_t m_scrolled = 0;
    TextFace m_face{};
    std::int32_t m_inset = 0;
};

} // namespace nx::ui
