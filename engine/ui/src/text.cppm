// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Text on the screen (ADR-0020): a font's glyphs packed into one picture, an atlas, and the
/// rectangles that draw a line of text with it. Each glyph goes where the font's pen puts it, the
/// pen moving by each glyph's advance alone, without kerning, as Windows' GDI wrote VGUI2's text
/// (docs/12-ui-hud-and-console.md, section 2.1).
export module nx.ui:text;

import std;
import nx.core;
import nx.assets;
import nx.render;
import :font;
import :schemeFont;

export namespace nx::ui {

/// How wide an atlas is made, in texels, unless a glyph is wider.
inline constexpr std::uint32_t kAtlasWidth = 256;

/// A glyph in an atlas: where its bitmap is, and where the pen puts it.
struct AtlasGlyph {
    assets::SpriteRect source{}; ///< Its bitmap in the atlas; empty for a glyph without one (a space).
    std::int32_t left = 0;       ///< From the pen to the bitmap's left edge.
    std::int32_t top = 0;        ///< From the baseline up to the bitmap's top edge.
    std::int32_t advance = 0;    ///< How far the pen moves past the glyph.
};

/// The glyphs of a font for a set of characters, packed into one picture of white texels, each as
/// opaque as the glyph covers its pixel.
class GlyphAtlas {
public:
    /// The glyphs of `font` for `characters`, each packed once, in rows of a picture `width` texels
    /// wide (wider when a glyph is), a texel apart so that none is sampled with another. A character
    /// the font has no glyph for is left out. Errors: BadGlyph.
    [[nodiscard]] static auto build(const Font& font, std::u32string_view characters,
                                    std::uint32_t width = kAtlasWidth) -> Result<GlyphAtlas, FontError>;
    /// The same of a scheme's font, each character's glyph from the font of the set that draws it,
    /// the line the set's.
    [[nodiscard]] static auto build(const SchemeFontSet& fonts, std::u32string_view characters,
                                    std::uint32_t width = kAtlasWidth) -> Result<GlyphAtlas, FontError>;

    /// The font's line, which text is laid out by.
    [[nodiscard]] inline auto metrics() const noexcept -> const FontMetrics& { return m_metrics; }
    [[nodiscard]] inline auto width() const noexcept -> std::uint32_t { return m_width; }
    [[nodiscard]] inline auto height() const noexcept -> std::uint32_t { return m_height; }
    /// The picture: RGBA texels, row by row from the top, for ScreenRenderer::loadImage.
    [[nodiscard]] inline auto texels() const noexcept -> std::span<const std::byte> { return m_texels; }
    /// The glyph of `character`; null when the atlas has none.
    [[nodiscard]] auto glyph(char32_t character) const noexcept -> const AtlasGlyph*;

private:
    GlyphAtlas() = default;
    /// The atlas of `drawn`, in rows `width` wide (wider when a glyph is), for a font of `metrics`.
    [[nodiscard]] static auto pack(const FontMetrics& metrics, std::vector<std::pair<char32_t, Glyph>> drawn,
                                   std::uint32_t width) -> GlyphAtlas;

    FontMetrics m_metrics{};
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    std::vector<std::byte> m_texels;
    std::vector<std::pair<char32_t, AtlasGlyph>> m_glyphs; ///< In the order of their characters.
};

/// A glyph of a line of text, placed: its bitmap in the atlas, and the bitmap's top left on the screen.
struct PlacedGlyph {
    assets::SpriteRect source{};
    float x = 0.0F;
    float y = 0.0F;
};

/// The glyphs of the UTF-8 `text` from `atlas`, the line's top at `y` and the pen from `x`: each
/// glyph's bitmap where the pen puts it, the pen moved by its advance. A glyph without a bitmap (a
/// space) only moves the pen; a character the atlas has no glyph for is left out.
[[nodiscard]] auto layoutText(const GlyphAtlas& atlas, std::string_view text, float x, float y)
    -> std::vector<PlacedGlyph>;

/// Appends to `quads` the glyphs of the UTF-8 `text` laid out by layoutText from `atlas`, whose picture
/// on the GPU is `image`, in `color` as opaque as each glyph covers each pixel. Returns where the pen
/// ends.
auto appendText(std::vector<render::ScreenQuad>& quads, const GlyphAtlas& atlas,
                const render::ScreenImage& image, std::string_view text, float x, float y,
                render::ScreenColor color) -> float;

/// How far the pen moves over the UTF-8 `text` with `atlas`'s glyphs.
[[nodiscard]] auto widthOf(const GlyphAtlas& atlas, std::string_view text) -> std::int32_t;

} // namespace nx::ui
