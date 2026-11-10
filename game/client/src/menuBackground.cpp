// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.rhi;
import nx.assets;
import nx.render;

namespace cs {

namespace {

/// A pixel no picture covers: black, opaque.
constexpr std::array<std::byte, nx::assets::kRgbaBytes> kUncovered{std::byte{0}, std::byte{0}, std::byte{0},
                                                                   std::byte{255}};
/// What the room left is divided by to put a thing halfway into it.
constexpr float kMiddleDivisor = 2.0F;

/// The bytes of the file at `path` among those of `vfs`. Errors: Unreadable, Invalid (a path of no file).
[[nodiscard]] auto readFile(const nx::assets::Vfs& vfs, std::string_view path)
    -> nx::Result<std::vector<std::byte>, MenuBackgroundError> {
    const auto file = nx::assets::VfsPath::parseGoldsrc(path);
    if (!file) {
        return nx::fail(MenuBackgroundError::Invalid, std::string{path}, file.error());
    }
    auto bytes = vfs.readAll(*file);
    if (!bytes) {
        return nx::fail(MenuBackgroundError::Unreadable, std::string{path}, std::move(bytes.error()));
    }
    return std::move(*bytes);
}

} // namespace

auto composeBackground(const nx::assets::BackgroundLayout& layout,
                       std::span<const nx::assets::RgbaImage> tiles) -> nx::assets::RgbaImage {
    nx::expects(tiles.size() == layout.tiles.size(),
                "composeBackground: a picture for each the layout places");
    const std::size_t rowBytes = std::size_t{layout.width} * nx::assets::kRgbaBytes;
    nx::assets::RgbaImage whole{.width = layout.width, .height = layout.height};
    whole.pixels.resize(rowBytes * layout.height);
    for (const std::span<std::byte> pixel : whole.pixels | std::views::chunk(nx::assets::kRgbaBytes)) {
        std::ranges::copy(kUncovered, pixel.begin());
    }
    for (const auto& [placed, tile] : std::views::zip(layout.tiles, tiles)) {
        if (placed.x >= layout.width || placed.y >= layout.height) {
            continue;
        }
        const std::uint32_t columns = std::min(tile.width, layout.width - placed.x);
        const std::uint32_t rows = std::min(tile.height, layout.height - placed.y);
        const std::size_t tileRowBytes = std::size_t{tile.width} * nx::assets::kRgbaBytes;
        for (std::uint32_t row = 0; row < rows; ++row) {
            const auto from = std::span{tile.pixels}.subspan(row * tileRowBytes,
                                                             std::size_t{columns} * nx::assets::kRgbaBytes);
            const std::size_t to =
                ((std::size_t{placed.y} + row) * rowBytes) + (std::size_t{placed.x} * nx::assets::kRgbaBytes);
            std::ranges::copy(from, whole.pixels.begin() + nx::narrow<std::ptrdiff_t>(to));
        }
    }
    return whole;
}

auto readMenuBackground(const nx::assets::Vfs& vfs, std::string_view layoutPath)
    -> nx::Result<nx::assets::RgbaImage, MenuBackgroundError> {
    auto text = readFile(vfs, layoutPath);
    if (!text) {
        return std::unexpected{std::move(text.error())};
    }
    const std::string words = *text | std::views::transform([](std::byte value) {
        return static_cast<char>(value);
    }) | std::ranges::to<std::string>();
    auto layout = nx::assets::parseBackgroundLayout(words);
    if (!layout) {
        return nx::fail(MenuBackgroundError::Invalid, std::string{layoutPath}, std::move(layout.error()));
    }
    std::vector<nx::assets::RgbaImage> tiles;
    tiles.reserve(layout->tiles.size());
    for (const nx::assets::BackgroundTile& placed : layout->tiles) {
        auto bytes = readFile(vfs, placed.image);
        if (!bytes) {
            return std::unexpected{std::move(bytes.error())};
        }
        auto tile = nx::assets::readTga(*bytes);
        if (!tile) {
            return nx::fail(MenuBackgroundError::Invalid, placed.image, std::move(tile.error()));
        }
        tiles.push_back(std::move(*tile));
    }
    return composeBackground(*layout, tiles);
}

auto coveringRect(nx::rhi::Extent2d picture, nx::rhi::Extent2d screen) noexcept -> nx::render::ScreenRect {
    const auto pictureWide = static_cast<float>(picture.width);
    const auto pictureTall = static_cast<float>(picture.height);
    const auto screenWide = static_cast<float>(screen.width);
    const auto screenTall = static_cast<float>(screen.height);
    if (picture.width == 0 || picture.height == 0) {
        return {};
    }
    // The side that fills the screen is the screen's own, to the pixel; the other follows the picture's
    // shape.
    const bool fillsAcross = screenWide * pictureTall >= screenTall * pictureWide;
    const float wide = fillsAcross ? screenWide : pictureWide * screenTall / pictureTall;
    const float tall = fillsAcross ? pictureTall * screenWide / pictureWide : screenTall;
    return {.x = (screenWide - wide) / kMiddleDivisor,
            .y = (screenTall - tall) / kMiddleDivisor,
            .width = wide,
            .height = tall};
}

} // namespace cs
