// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.rhi;
import nx.render;
import nx.ui;
import cs.shared;

namespace cs {

namespace {

constexpr std::int32_t kRegularWeight = 400;
constexpr std::int32_t kStrongWeight = 600;

} // namespace

MenuScale::MenuScale(std::uint32_t screenHeight) noexcept
    : scale{static_cast<float>(screenHeight) / kMenuDesignHeight} {}

auto MenuScale::pixels(std::int32_t units) const noexcept -> nx::Pixels {
    // A part of the design of some size shows, however small the screen.
    constexpr nx::Pixels kLeastShown{1};
    return units > 0
               ? std::max(
                     nx::Pixels{nx::narrow<std::int32_t>(std::lround(static_cast<float>(units) * scale))},
                     kLeastShown)
               : nx::Pixels{};
}

auto makeMenuFace(nx::ui::FontCatalog& fonts, nx::render::ScreenRenderer& screen,
                  nx::rhi::CommandList& commands, const MenuFontFamilies& families, MenuFontSize size,
                  std::uint32_t screenHeight, std::string_view label)
    -> nx::Result<MenuFace, MenuPanelError> {
    const std::string& family = size.isStrong ? families.strong : families.regular;
    const std::optional<nx::ui::FontChoice> choice =
        fonts.choose({.family = family, .weight = size.isStrong ? kStrongWeight : kRegularWeight});
    if (!choice) {
        return nx::fail(MenuPanelError::NoFont, family);
    }
    const auto cellHeight = static_cast<std::int32_t>(
        std::lround(size.units * static_cast<float>(screenHeight) / kMenuDesignHeight));
    auto opened = fonts.open(
        *choice, {.cellHeight = std::max(cellHeight, 1), .smoothing = nx::ui::FontSmoothing::Antialiased});
    if (!opened) {
        return nx::fail(MenuPanelError::Unusable, family, std::move(opened.error()));
    }
    auto atlas = nx::ui::GlyphAtlas::build(*opened, consoleCharacters());
    if (!atlas) {
        return nx::fail(MenuPanelError::Unusable, family, std::move(atlas.error()));
    }
    auto image = screen.loadImage(commands, atlas->texels(), atlas->width(), atlas->height(), label);
    if (!image) {
        return nx::fail(MenuPanelError::GpuFailed, family, std::move(image.error()));
    }
    return MenuFace{.atlas = std::move(*atlas), .image = std::move(*image)};
}

} // namespace cs
