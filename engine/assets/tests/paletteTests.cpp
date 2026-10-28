// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The engine's palette, gfx/palette.lmp: 256 colors of red, green and blue, by their entry.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;

namespace {

/// A palette whose entry n is (n, 255 - n, n / 2).
[[nodiscard]] auto gradient() -> std::vector<std::byte> {
    std::vector<std::byte> bytes;
    for (std::size_t entry = 0; entry < nx::assets::kPaletteColors; ++entry) {
        bytes.push_back(static_cast<std::byte>(entry));
        bytes.push_back(static_cast<std::byte>(255 - entry));
        bytes.push_back(static_cast<std::byte>(entry / 2));
    }
    return bytes;
}

TEST(PaletteTest, EachEntryIsItsRedGreenAndBlue) {
    const auto palette = nx::assets::Palette::read(gradient());
    ASSERT_TRUE(palette.has_value());
    EXPECT_EQ(palette->color(0), (nx::assets::PaletteColor{.red = 0, .green = 255, .blue = 0}));
    EXPECT_EQ(palette->color(247), (nx::assets::PaletteColor{.red = 247, .green = 8, .blue = 123}));
    EXPECT_EQ(palette->color(255), (nx::assets::PaletteColor{.red = 255, .green = 0, .blue = 127}));
}

TEST(PaletteTest, APaletteOfAnotherSizeIsRefused) {
    std::vector<std::byte> bytes = gradient();
    bytes.pop_back();
    const auto shorter = nx::assets::Palette::read(bytes);
    ASSERT_FALSE(shorter.has_value());
    EXPECT_EQ(shorter.error().code, nx::assets::PaletteError::WrongSize);
    bytes.resize(nx::assets::kPaletteBytes + 1);
    EXPECT_FALSE(nx::assets::Palette::read(bytes).has_value());
}

} // namespace
