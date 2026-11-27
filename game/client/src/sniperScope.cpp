// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.rhi;
import nx.assets;
import nx.render;
import nx.ui;

namespace cs {

namespace {

constexpr nx::log::Category kLog{"client"};

/// The quarters' pictures, in SniperScope's order.
constexpr std::array<std::string_view, 4> kQuarters{"sprites/scope_arc_nw.tga", "sprites/scope_arc_ne.tga",
                                                    "sprites/scope_arc_sw.tga", "sprites/scope_arc.tga"};
constexpr nx::render::ScreenColor kBlack{.red = 0, .green = 0, .blue = 0};

} // namespace

void SniperScope::load(const nx::assets::Vfs& vfs, nx::render::ScreenRenderer& screen,
                       nx::rhi::CommandList& commands) {
    isRead = true;
    for (const auto& [quarter, path] : std::views::zip(quarters, kQuarters)) {
        const auto file = nx::assets::VfsPath::parse(path);
        auto bytes = file ? vfs.readAll(*file) : nx::Result<std::vector<std::byte>, nx::assets::VfsError>{};
        auto image = file && bytes ? nx::assets::readTga(*bytes)
                                   : nx::Result<nx::assets::RgbaImage, nx::assets::TgaError>{};
        if (!file || !bytes || !image) {
            nx::log::warning(kLog, "the scope is drawn without {:?}: it cannot be read", path);
            continue;
        }
        auto loaded = screen.loadImage(commands, image->pixels, image->width, image->height, path);
        if (!loaded) {
            nx::log::warning(kLog, "the scope is drawn without {:?}: {}", path, nx::describe(loaded.error()));
            continue;
        }
        quarter.emplace(std::move(*loaded));
    }
}

void SniperScope::append(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size) const {
    const auto wide = static_cast<float>(size.width);
    const auto tall = static_cast<float>(size.height);
    const float side = tall / 2.0F;
    const float middle = wide / 2.0F;
    const float left = middle - side;
    // Each quarter in its corner of a square as tall as the screen, in its middle.
    const std::array<std::array<float, 2>, 4> corners{
        {{left, 0.0F}, {middle, 0.0F}, {left, side}, {middle, side}}};
    for (const auto& [quarter, corner] : std::views::zip(quarters, corners)) {
        if (!quarter) {
            continue;
        }
        quads.push_back({.image = &*quarter,
                         .source = {.width = quarter->width(), .height = quarter->height()},
                         .target = {.x = corner[0], .y = corner[1], .width = side, .height = side},
                         .blend = nx::render::ScreenBlend::Translucent,
                         .filter = nx::render::ScreenFilter::Linear});
    }
    nx::ui::PanelPainter painter{quads, size};
    const nx::Pixels whole{nx::narrow<std::int32_t>(size.width)};
    const nx::Pixels high{nx::narrow<std::int32_t>(size.height)};
    const auto edge = nx::Pixels{static_cast<std::int32_t>(std::lround(left))};
    painter.fill({.x = nx::Pixels{}, .y = nx::Pixels{}, .wide = edge, .tall = high}, kBlack);
    painter.fill({.x = whole - edge, .y = nx::Pixels{}, .wide = edge, .tall = high}, kBlack);
    // Its lines across, one pixel thick, through the middle.
    const auto across = nx::Pixels{static_cast<std::int32_t>(std::lround(middle))};
    const auto down = nx::Pixels{static_cast<std::int32_t>(std::lround(side))};
    painter.fill({.x = nx::Pixels{}, .y = down, .wide = whole, .tall = nx::Pixels{1}}, kBlack);
    painter.fill({.x = across, .y = nx::Pixels{}, .wide = nx::Pixels{1}, .tall = high}, kBlack);
}

} // namespace cs
