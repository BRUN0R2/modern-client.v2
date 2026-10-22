// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Labels (ADR-0020): a line of text in a panel, as VGUI2's Label shows one, placed in the panel by
/// its alignment (`textAlignment`).
export module nx.ui:label;

import std;
import nx.core;
import nx.render;
import :panel;
import :painter;
import :text;

export namespace nx::ui {

/// Where a label's text sits in it (VGUI2's `textAlignment`).
enum class TextAlignment : std::uint8_t {
    NorthWest,
    North,
    NorthEast,
    West,
    Center,
    East,
    SouthWest,
    South,
    SouthEast,
    Count,
};

/// The alignment `name` names, as VGUI2's layouts name them ("north-west", "center", "east"), in any
/// case; none for another name.
[[nodiscard]] auto textAlignmentNamed(std::string_view name) noexcept -> std::optional<TextAlignment>;

/// A line of text in a panel (VGUI2's Label), in a face and a color, placed in the panel by its
/// alignment: against an edge, or halfway between two.
class Label : public Panel {
public:
    explicit Label(std::string name, PanelBounds bounds = {});

    [[nodiscard]] inline auto text() const noexcept -> std::string_view { return m_text; }
    [[nodiscard]] inline auto alignment() const noexcept -> TextAlignment { return m_alignment; }
    inline void setText(std::string text) noexcept { m_text = std::move(text); }
    inline void setFace(TextFace face) noexcept { m_face = face; }
    inline void setColor(render::ScreenColor color) noexcept { m_color = color; }
    inline void setAlignment(TextAlignment alignment) noexcept { m_alignment = alignment; }

    /// Where the text's line goes in the label: where its pen starts, and its top.
    [[nodiscard]] auto textPosition() const -> std::pair<std::int32_t, std::int32_t>;

protected:
    /// Its background, then its text.
    void paint(PanelPainter& painter) const override;

private:
    std::string m_text;
    TextFace m_face{};
    render::ScreenColor m_color{};
    TextAlignment m_alignment = TextAlignment::West;
};

} // namespace nx::ui
