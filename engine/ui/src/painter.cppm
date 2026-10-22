// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What panels draw with (ADR-0020): rectangles of the screen, in pixels from a panel's top left,
/// kept within the panel and every panel it is in, as VGUI2 keeps a panel's drawing within it.
export module nx.ui:painter;

import std;
import nx.core;
import nx.rhi;
import nx.render;
import :text;

export namespace nx::ui {

/// A font ready to draw: its atlas, and the atlas's picture on the GPU (ScreenRenderer::loadImage).
struct TextFace {
    const GlyphAtlas* atlas = nullptr;
    const render::ScreenImage* image = nullptr;
};

/// Draws a panel: appends rectangles of the screen, placed from the panel's top left and clipped to
/// what of it shows.
class PanelPainter {
public:
    /// A painter of the whole screen, `size` pixels, appending to `quads`.
    PanelPainter(std::vector<render::ScreenQuad>& quads, rhi::Extent2d size) noexcept;

    /// Fills the rectangle at `x`, `y`, `wide` by `tall` with `color`, as opaque as its alpha.
    void fill(std::int32_t x, std::int32_t y, std::int32_t wide, std::int32_t tall,
              render::ScreenColor color);
    /// Draws the UTF-8 `text` in `face`, the line's top at `y` and the pen from `x`, in `color`.
    /// Returns where the pen ends.
    auto text(const TextFace& face, std::string_view text, std::int32_t x, std::int32_t y,
              render::ScreenColor color) -> std::int32_t;

    /// The painter of a panel at `x`, `y`, `wide` by `tall` in this one: from its top left, within it.
    [[nodiscard]] auto inside(std::int32_t x, std::int32_t y, std::int32_t wide,
                              std::int32_t tall) const noexcept -> PanelPainter;
    /// Whether nothing it draws would show.
    [[nodiscard]] inline auto isClippedAway() const noexcept -> bool {
        return m_clip.size.width == 0 || m_clip.size.height == 0;
    }
    /// What of the screen shows of what it draws.
    [[nodiscard]] inline auto clip() const noexcept -> const rhi::Rect& { return m_clip; }

private:
    PanelPainter(std::vector<render::ScreenQuad>& quads, std::int32_t x, std::int32_t y,
                 rhi::Rect clip) noexcept;

    std::vector<render::ScreenQuad>* m_quads;
    std::int32_t m_x = 0; ///< The panel's top left on the screen.
    std::int32_t m_y = 0;
    rhi::Rect m_clip{};
};

} // namespace nx::ui
