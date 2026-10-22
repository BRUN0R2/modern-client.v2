// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// A scheme's fonts made for a screen (ADR-0020). A font of a scheme's `Fonts` lists candidates,
/// each a face, a height, a weight and flags, for the screens whose height its `yres` holds and the
/// characters its `range` holds. The scheme files say how they combine: "fonts are used in order
/// that they are listed; fonts listed later in the order will only be used if they fulfill a range
/// not already filled; if a font fails to load then the subsequent fonts will replace".
export module nx.ui:schemeFont;

import std;
import nx.core;
import nx.assets;
import :font;
import :fontCatalog;

export namespace nx::ui {

/// A scheme's font made for a screen: its candidates for the screen's height (those whose `yres`
/// holds it, and those without one), in the scheme's order, each opened with the face the catalog
/// chooses for its name, weight and italics. A character is drawn by the first candidate whose
/// `range` holds it (every character without one); a candidate that does not open leaves its
/// characters to those after it.
class SchemeFontSet {
public:
    /// `font` made for a screen `screenHeight` pixels tall, its faces from `catalog`. Errors: NoFace
    /// (no candidate for that height opens; the detail names them).
    [[nodiscard]] static auto open(const assets::SchemeFont& font, std::uint32_t screenHeight,
                                   FontCatalog& catalog) -> Result<SchemeFontSet, FontError>;

    /// The line of the first candidate that opened, which text is laid out by.
    [[nodiscard]] auto metrics() const noexcept -> const FontMetrics&;
    /// The font that draws `character`: the first whose range holds it; null when none does.
    [[nodiscard]] auto fontFor(char32_t character) const noexcept -> const Font*;
    /// The glyph of `character`, from fontFor. Errors: NoGlyph (no font draws it), and those of
    /// Font::glyph.
    [[nodiscard]] auto glyph(char32_t character) const -> Result<Glyph, FontError>;

private:
    /// A candidate that opened, and the characters it draws (all without a range).
    struct Part {
        Font font;
        std::optional<assets::SchemeRange<char32_t>> characters;
    };

    explicit SchemeFontSet(std::vector<Part> parts) noexcept;

    std::vector<Part> m_parts; ///< Never empty.
};

} // namespace nx::ui
