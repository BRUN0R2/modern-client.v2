// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// SPR v2 and v3 sprites (nx.assets:spr). A sprite that parses has every pixel looked up in its
// palette, or its BC3 blocks decoded, as a renderer would.

import std;
import nx.assets;

#include "fuzzTarget.hpp"

void nx::test::fuzz(std::span<const std::byte> input) {
    const auto sprite = nx::assets::SpriteView::parse(input);
    if (!sprite) {
        return;
    }
    constexpr std::size_t kBytesPerColor = nx::assets::kPaletteBytes / nx::assets::kPaletteColors;
    int sum = 0;
    for (const nx::assets::SpriteFrame& frame : sprite->frames) {
        for (const nx::assets::SpriteImage& image : frame.images) {
            if (image.format == nx::assets::SpritePixels::Bc3) {
                const nx::assets::RgbaImage decoded =
                    nx::assets::decodeBc3(image.pixels, image.width, image.height);
                sum += decoded.pixels.empty() ? 0 : std::to_integer<int>(decoded.pixels.back());
                continue;
            }
            for (const std::byte pixel : image.pixels) {
                sum += std::to_integer<int>(
                    sprite->palette[std::to_integer<std::size_t>(pixel) * kBytesPerColor]);
            }
        }
    }
    std::ignore = sum; // the reads are the test
}
