// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ui;

import std;
import nx.core;
import nx.assets;

namespace nx::ui {

auto SchemeFontSet::open(const assets::SchemeFont& font, std::uint32_t screenHeight, FontCatalog& catalog)
    -> Result<SchemeFontSet, FontError> {
    std::vector<Part> parts;
    std::vector<std::string> tried;
    for (const assets::FontCandidate& candidate : font.candidates) {
        if (candidate.heights && !candidate.heights->contains(screenHeight)) {
            continue; // for other screens
        }
        tried.push_back(std::format("{:?} {}", candidate.face, candidate.tall));
        const std::optional<FontChoice> choice =
            catalog.choose({.family = candidate.face,
                            .weight = candidate.weight,
                            .isItalic = candidate.flags.test(assets::FontFlag::Italic)});
        if (!choice) {
            continue; // no such face: the candidates after it stand in
        }
        auto opened = catalog.open(*choice, {.cellHeight = candidate.tall,
                                             .smoothing = candidate.flags.test(assets::FontFlag::Antialias)
                                                              ? FontSmoothing::Antialiased
                                                              : FontSmoothing::None});
        if (!opened) {
            continue; // it fails to load: the candidates after it stand in
        }
        parts.push_back({.font = std::move(*opened),
                         .characters = candidate.characters,
                         .hasDropShadow = candidate.flags.test(assets::FontFlag::DropShadow)});
    }
    if (parts.empty()) {
        return fail(FontError::NoFace,
                    std::format("{:?} for a screen {} tall: none of [{}] opens", font.name, screenHeight,
                                tried | std::views::join_with(std::string_view{", "}) |
                                    std::ranges::to<std::string>()));
    }
    return SchemeFontSet{std::move(parts)};
}

SchemeFontSet::SchemeFontSet(std::vector<Part> parts) noexcept : m_parts{std::move(parts)} {}

auto SchemeFontSet::metrics() const noexcept -> const FontMetrics& {
    return m_parts.front().font.metrics();
}

auto SchemeFontSet::partFor(char32_t character) const noexcept -> const Part* {
    const auto found = std::ranges::find_if(m_parts, [character](const Part& part) {
        return !part.characters || part.characters->contains(character);
    });
    return found != m_parts.end() ? &*found : nullptr;
}

auto SchemeFontSet::fontFor(char32_t character) const noexcept -> const Font* {
    const Part* part = partFor(character);
    return part != nullptr ? &part->font : nullptr;
}

auto SchemeFontSet::hasDropShadow(char32_t character) const noexcept -> bool {
    const Part* part = partFor(character);
    return part != nullptr && part->hasDropShadow;
}

auto SchemeFontSet::glyph(char32_t character) const -> Result<Glyph, FontError> {
    const Font* font = fontFor(character);
    if (font == nullptr) {
        return fail(FontError::NoGlyph,
                    std::format("U+{:04X}: in no range of the font", std::uint32_t{character}));
    }
    return font->glyph(character);
}

} // namespace nx::ui
