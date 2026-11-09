// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What panels draw with (ADR-0020): rectangles of the screen, in pixels from a panel's top left,
/// kept within the panel and every panel it is in, as VGUI2 keeps a panel's drawing within it.
export module nx.ui:painter;

import std;
import nx.core;
import nx.rhi;
import nx.render;
import :geometry;
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

    /// Fills `rect` with `color`, as opaque as its alpha.
    void fill(const PixelRect& rect, render::ScreenColor color);
    /// Draws the UTF-8 `text` in `face`, the line's top at `pen`'s and the pen from its left, in
    /// `color`. Returns where the pen ends, from the panel's left.
    auto text(const TextFace& face, std::string_view text, PixelPoint pen, render::ScreenColor color)
        -> Pixels;

    /// The painter of a panel at `rect` in this one: from its top left, within it.
    [[nodiscard]] auto inside(const PixelRect& rect) const noexcept -> PanelPainter;
    /// Whether nothing it draws would show.
    [[nodiscard]] inline auto isClippedAway() const noexcept -> bool {
        return m_clip.size.width == 0 || m_clip.size.height == 0;
    }
    /// What of the screen shows of what it draws.
    [[nodiscard]] inline auto clip() const noexcept -> const rhi::Rect& { return m_clip; }

private:
    PanelPainter(std::vector<render::ScreenQuad>& quads, PixelPoint topLeft, rhi::Rect clip) noexcept;

    std::vector<render::ScreenQuad>* m_quads;
    PixelPoint origin{}; ///< The panel's top left on the screen.
    rhi::Rect m_clip{};
};

} // namespace nx::ui
