// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;

namespace {

using nx::assets::BmpError;
using nx::assets::RgbaImage;

using Bytes = std::vector<std::byte>;

template <class Value>
void append(Bytes& bytes, Value value) {
    const auto raw =
        std::bit_cast<std::array<std::byte, sizeof(Value)>>(value); // the tests run on little-endian hosts
    bytes.insert(bytes.end(), raw.begin(), raw.end());
}

void appendBytes(Bytes& bytes, std::initializer_list<int> values) {
    for (const int value : values) {
        bytes.push_back(static_cast<std::byte>(value));
    }
}

/// A BMP of `width` x `height` (negative: rows from the top) at `depth` bits, of `palette` (BGRx
/// entries) and `rows` (each padded by the caller), as the file stores them.
[[nodiscard]] auto bmpOf(std::int32_t width, std::int32_t height, std::uint16_t depth,
                         std::initializer_list<int> palette, std::initializer_list<int> rows) -> Bytes {
    constexpr std::uint32_t kHeaders = 14 + 40;
    const auto paletteBytes = static_cast<std::uint32_t>(palette.size());
    Bytes bytes;
    appendBytes(bytes, {'B', 'M'});
    append(bytes, std::uint32_t{0}); // the size, which readers ignore
    append(bytes, std::uint32_t{0});
    append(bytes, kHeaders + paletteBytes); // the pixels' start
    append(bytes, std::uint32_t{40});
    append(bytes, width);
    append(bytes, height);
    append(bytes, std::uint16_t{1});
    append(bytes, depth);
    append(bytes, std::uint32_t{0}); // BI_RGB
    append(bytes, std::uint32_t{0});
    append(bytes, std::int32_t{0});
    append(bytes, std::int32_t{0});
    append(bytes, paletteBytes / 4); // colors used
    append(bytes, std::uint32_t{0});
    appendBytes(bytes, palette);
    appendBytes(bytes, rows);
    return bytes;
}

[[nodiscard]] auto pixelOf(const RgbaImage& image, std::uint32_t x, std::uint32_t y) -> std::array<int, 4> {
    const std::size_t at = ((std::size_t{y} * image.width) + x) * 4;
    return {std::to_integer<int>(image.pixels[at]), std::to_integer<int>(image.pixels[at + 1]),
            std::to_integer<int>(image.pixels[at + 2]), std::to_integer<int>(image.pixels[at + 3])};
}

TEST(BmpTest, PaletteIndicesFromTheBottomRowUp) {
    // 3x2, indices: bottom row 0 1 2, top row 2 1 0, each row padded to four bytes.
    const Bytes bytes = bmpOf(3, 2, 8, {255, 0, 0, 0, 0, 255, 0, 0, 0, 0, 255, 0}, {0, 1, 2, 0, 2, 1, 0, 0});
    const auto image = nx::assets::readBmp(bytes);
    ASSERT_TRUE(image.has_value()) << nx::describe(image.error());
    EXPECT_EQ(image->width, 3U);
    EXPECT_EQ(image->height, 2U);
    EXPECT_EQ(pixelOf(*image, 0, 0), (std::array{255, 0, 0, 255})) << "the top row first, red";
    EXPECT_EQ(pixelOf(*image, 2, 0), (std::array{0, 0, 255, 255}));
    EXPECT_EQ(pixelOf(*image, 0, 1), (std::array{0, 0, 255, 255})) << "the bottom row, blue";
    EXPECT_EQ(pixelOf(*image, 1, 1), (std::array{0, 255, 0, 255}));
}

TEST(BmpTest, TrueColorFromTheTopDown) {
    // 1x2 at 24 bits, rows from the top: blue, green, red each, padded to four bytes.
    const Bytes bytes = bmpOf(1, -2, 24, {}, {10, 20, 30, 0, 40, 50, 60, 0});
    const auto image = nx::assets::readBmp(bytes);
    ASSERT_TRUE(image.has_value()) << nx::describe(image.error());
    EXPECT_EQ(pixelOf(*image, 0, 0), (std::array{30, 20, 10, 255}));
    EXPECT_EQ(pixelOf(*image, 0, 1), (std::array{60, 50, 40, 255}));
}

TEST(BmpTest, WhatIsNotReadIsATypedFailure) {
    struct Case {
        std::string_view description;
        Bytes bytes;
        BmpError code;
    };
    Bytes truncated = bmpOf(3, 2, 8, {0, 0, 0, 0}, {0, 0, 0, 0});
    Bytes badIndex = bmpOf(1, 1, 8, {0, 0, 0, 0}, {5, 0, 0, 0});
    Bytes compressed = bmpOf(1, 1, 8, {0, 0, 0, 0}, {0, 0, 0, 0});
    compressed[30] = std::byte{1}; // BI_RLE8
    const std::vector<Case> cases{
        {"no headers", Bytes(10, std::byte{0}), BmpError::TooSmall},
        {"not BM", bmpOf(1, 1, 24, {}, {0, 0, 0, 0}) | std::views::drop(1) | std::ranges::to<Bytes>(),
         BmpError::BadValue},
        {"16 bits", bmpOf(1, 1, 16, {}, {0, 0, 0, 0}), BmpError::Unsupported},
        {"run-length", compressed, BmpError::Unsupported},
        {"rows missing", truncated, BmpError::Truncated},
        {"index past the palette", badIndex, BmpError::BadValue},
    };
    for (const Case& problem : cases) {
        SCOPED_TRACE(problem.description);
        const auto image = nx::assets::readBmp(problem.bytes);
        ASSERT_FALSE(image.has_value());
        EXPECT_EQ(image.error().code, problem.code);
    }
}

} // namespace
