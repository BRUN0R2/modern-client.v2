// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ui;

import std;
import nx.core;
import nx.assets;
import nx.render;

namespace nx::ui {

namespace {

/// The texels between two glyphs, and between a glyph and the atlas's edge.
constexpr std::uint32_t kGap = 1;
/// The bytes of an RGBA texel.
constexpr std::size_t kTexelBytes = 4;
/// The alpha of a texel: its last byte.
constexpr std::size_t kAlphaByte = 3;

/// The glyphs `source` draws for `characters`, each once, in the order of their characters; those it
/// has none for left out.
template <class Source>
[[nodiscard]] auto drawGlyphs(const Source& source, std::u32string_view characters)
    -> Result<std::vector<std::pair<char32_t, Glyph>>, FontError> {
    std::vector<char32_t> wanted(characters.begin(), characters.end());
    std::ranges::sort(wanted);
    const auto repeated = std::ranges::unique(wanted);
    wanted.erase(repeated.begin(), repeated.end());
    std::vector<std::pair<char32_t, Glyph>> drawn;
    drawn.reserve(wanted.size());
    for (const char32_t character : wanted) {
        auto glyph = source.glyph(character);
        if (!glyph && glyph.error().code == FontError::NoGlyph) {
            continue;
        }
        if (!glyph) {
            return std::unexpected{std::move(glyph.error())};
        }
        drawn.emplace_back(character, std::move(*glyph));
    }
    return drawn;
}

} // namespace

auto GlyphAtlas::build(const Font& font, std::u32string_view characters, std::uint32_t width)
    -> Result<GlyphAtlas, FontError> {
    auto drawn = drawGlyphs(font, characters);
    if (!drawn) {
        return std::unexpected{std::move(drawn.error())};
    }
    return pack(font.metrics(), std::move(*drawn), width);
}

auto GlyphAtlas::build(const SchemeFontSet& fonts, std::u32string_view characters, std::uint32_t width)
    -> Result<GlyphAtlas, FontError> {
    auto drawn = drawGlyphs(fonts, characters);
    if (!drawn) {
        return std::unexpected{std::move(drawn.error())};
    }
    return pack(fonts.metrics(), std::move(*drawn), width);
}

auto GlyphAtlas::pack(const FontMetrics& metrics, std::vector<std::pair<char32_t, Glyph>> drawn,
                      std::uint32_t width) -> GlyphAtlas {
    // Shelves: the tallest glyphs first, each right of the last, a new row under the tallest of a full
    // one.
    GlyphAtlas atlas;
    atlas.m_metrics = metrics;
    std::uint32_t widest = 0;
    for (const auto& [character, glyph] : drawn) {
        widest = std::max(widest, narrow<std::uint32_t>(glyph.width));
    }
    atlas.m_width = std::max(width, widest + (2 * kGap));
    std::vector<std::size_t> order(drawn.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, std::ranges::greater{},
                             [&drawn](std::size_t index) { return drawn[index].second.height; });
    std::vector<assets::SpriteRect> places(drawn.size());
    std::uint32_t x = kGap;
    std::uint32_t y = kGap;
    std::uint32_t rowHeight = 0;
    for (const std::size_t index : order) {
        const auto glyphWidth = narrow<std::uint32_t>(drawn[index].second.width);
        const auto glyphHeight = narrow<std::uint32_t>(drawn[index].second.height);
        if (glyphWidth == 0 || glyphHeight == 0) {
            continue; // nothing to draw: a space
        }
        if (x + glyphWidth + kGap > atlas.m_width) {
            x = kGap;
            y += rowHeight + kGap;
            rowHeight = 0;
        }
        places[index] = {.x = x, .y = y, .width = glyphWidth, .height = glyphHeight};
        x += glyphWidth + kGap;
        rowHeight = std::max(rowHeight, glyphHeight);
    }
    atlas.m_height = y + rowHeight + kGap;

    // White texels everywhere, so that no edge darkens; each glyph's coverage its texels' alpha.
    atlas.m_texels.assign(std::size_t{atlas.m_width} * atlas.m_height * kTexelBytes, std::byte{255});
    for (std::size_t texel = kAlphaByte; texel < atlas.m_texels.size(); texel += kTexelBytes) {
        atlas.m_texels[texel] = std::byte{0};
    }
    atlas.m_glyphs.reserve(drawn.size());
    for (std::size_t index = 0; index < drawn.size(); ++index) {
        const auto& [character, glyph] = drawn[index];
        const assets::SpriteRect& place = places[index];
        for (std::uint32_t row = 0; row < place.height; ++row) {
            for (std::uint32_t column = 0; column < place.width; ++column) {
                const std::size_t texel = ((std::size_t{place.y} + row) * atlas.m_width) + place.x + column;
                atlas.m_texels[(texel * kTexelBytes) + kAlphaByte] =
                    std::byte{glyph.coverage[(std::size_t{row} * place.width) + column]};
            }
        }
        atlas.m_glyphs.emplace_back(
            character,
            AtlasGlyph{.source = place, .left = glyph.left, .top = glyph.top, .advance = glyph.advance});
    }
    return atlas;
}

auto GlyphAtlas::glyph(char32_t character) const noexcept -> const AtlasGlyph* {
    const auto found =
        std::ranges::lower_bound(m_glyphs, character, {}, &std::pair<char32_t, AtlasGlyph>::first);
    return found != m_glyphs.end() && found->first == character ? &found->second : nullptr;
}

auto layoutText(const GlyphAtlas& atlas, std::string_view text, float x, float y)
    -> std::vector<PlacedGlyph> {
    const float baseline = y + static_cast<float>(atlas.metrics().ascent);
    std::vector<PlacedGlyph> placed;
    for (const char32_t character : decodeUtf8(text)) {
        const AtlasGlyph* glyph = atlas.glyph(character);
        if (glyph == nullptr) {
            continue;
        }
        if (glyph->source.width > 0) {
            placed.push_back({.source = glyph->source,
                              .x = x + static_cast<float>(glyph->left),
                              .y = baseline - static_cast<float>(glyph->top)});
        }
        x += static_cast<float>(glyph->advance);
    }
    return placed;
}

auto appendText(std::vector<render::ScreenQuad>& quads, const GlyphAtlas& atlas,
                const render::ScreenImage& image, std::string_view text, float x, float y,
                render::ScreenColor color) -> float {
    for (const PlacedGlyph& glyph : layoutText(atlas, text, x, y)) {
        quads.push_back({.image = &image,
                         .source = glyph.source,
                         .target = {.x = glyph.x,
                                    .y = glyph.y,
                                    .width = static_cast<float>(glyph.source.width),
                                    .height = static_cast<float>(glyph.source.height)},
                         .color = color,
                         .blend = render::ScreenBlend::Translucent,
                         .filter = render::ScreenFilter::Nearest});
    }
    return x + static_cast<float>(widthOf(atlas, text));
}

auto widthOf(const GlyphAtlas& atlas, std::string_view text) -> std::int32_t {
    std::int32_t width = 0;
    for (const char32_t character : decodeUtf8(text)) {
        if (const AtlasGlyph* glyph = atlas.glyph(character); glyph != nullptr) {
            width += glyph->advance;
        }
    }
    return width;
}

} // namespace nx::ui
