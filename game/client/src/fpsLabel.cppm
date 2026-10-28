// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// cl_showfps's label (docs/12-ui-hud-and-console.md, section 3.2): the frames a second the client
/// shows (nx::platform::FrameRateMeter), in the top left corner, in the font, the colour and the place
/// Counter-Strike 1.6 writes them, to a tenth as its net_graph writes them.
export module cs.client:fpsLabel;

import std;
import nx.core;
import nx.rhi;
import nx.render;
import nx.ui;
import :windowFonts;

export namespace cs {

/// The font of kWindowScheme the label writes in: Verdana, bold, casting a drop shadow, its size the
/// screen's height's.
inline constexpr std::string_view kFpsFont = "EngineFont";
/// The top left of the label's line, in pixels from the screen's, at every resolution.
inline constexpr float kFpsLabelX = 2.0F;
inline constexpr float kFpsLabelY = 2.0F;
/// The label's colour.
inline constexpr nx::render::ScreenColor kFpsLabelColor{.red = 255, .green = 255, .blue = 255, .alpha = 255};

/// What the label says of `framesPerSecond`: the number to the nearest tenth, as the original's net_graph
/// writes it, then " fps" ("520.8 fps"). The original's cl_showfps writes a whole number; the user chose
/// the tenth, which shows the rate as it moves.
[[nodiscard]] auto fpsText(double framesPerSecond) -> std::string;

/// The label: the glyphs it writes, in kFpsFont made for a screen's height, put on the GPU with the
/// screen's pictures.
class FpsLabel {
public:
    /// The label for a screen `screenHeight` pixels tall, in kFpsFont of `fonts`. Errors: those of
    /// WindowFonts::font, and Unusable (its glyphs cannot be drawn).
    [[nodiscard]] static auto open(const WindowFonts& fonts, std::uint32_t screenHeight)
        -> nx::Result<FpsLabel, WindowFontError>;

    /// Puts its glyphs on the GPU with `screen`, its uploads recorded in `commands`. Errors: those of
    /// nx::render::ScreenRenderer::loadImage.
    [[nodiscard]] auto load(nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands)
        -> nx::Result<void, nx::render::ScreenError>;

    /// Appends to `quads` the label of `framesPerSecond` (fpsText), once loaded; nothing before. The
    /// label is laid out again only when its text changes: the quads of the last call are appended again
    /// until then.
    void append(std::vector<nx::render::ScreenQuad>& quads, double framesPerSecond);

private:
    explicit FpsLabel(nx::ui::GlyphAtlas atlas) noexcept;

    nx::ui::GlyphAtlas m_atlas;
    std::optional<nx::render::ScreenImage> m_image; ///< The atlas on the GPU, once loaded.
    nx::render::RetainedQuads<std::string> m_shown; ///< Of the text the label shows.
};

} // namespace cs
