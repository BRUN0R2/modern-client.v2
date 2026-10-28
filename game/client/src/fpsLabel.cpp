// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.rhi;
import nx.render;
import nx.ui;

namespace cs {

namespace {

/// The characters the label writes.
constexpr std::u32string_view kFpsCharacters = U"0123456789. fps";

} // namespace

auto fpsText(double framesPerSecond) -> std::string {
    return std::format("{:.1f} fps", framesPerSecond);
}

auto FpsLabel::open(const WindowFonts& fonts, std::uint32_t screenHeight)
    -> nx::Result<FpsLabel, WindowFontError> {
    auto font = fonts.font(kFpsFont, screenHeight);
    if (!font) {
        return std::unexpected{std::move(font.error())};
    }
    auto atlas = nx::ui::GlyphAtlas::build(*font, kFpsCharacters);
    if (!atlas) {
        return nx::fail(WindowFontError::Unusable, std::string{kFpsFont}, std::move(atlas.error()));
    }
    return FpsLabel{std::move(*atlas)};
}

FpsLabel::FpsLabel(nx::ui::GlyphAtlas atlas) noexcept : m_atlas{std::move(atlas)} {}

auto FpsLabel::load(nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands)
    -> nx::Result<void, nx::render::ScreenError> {
    auto image = screen.loadImage(commands, m_atlas.texels(), m_atlas.width(), m_atlas.height(), "fpsLabel");
    if (!image) {
        return std::unexpected{std::move(image.error())};
    }
    m_image.emplace(std::move(*image));
    return {};
}

void FpsLabel::append(std::vector<nx::render::ScreenQuad>& quads, double framesPerSecond) {
    if (!m_image) {
        return;
    }
    const std::string text = fpsText(framesPerSecond);
    m_shown.append(quads, text, [&](std::vector<nx::render::ScreenQuad>& label) {
        nx::ui::appendText(label, m_atlas, *m_image, text, kFpsLabelX, kFpsLabelY, kFpsLabelColor);
    });
}

} // namespace cs
