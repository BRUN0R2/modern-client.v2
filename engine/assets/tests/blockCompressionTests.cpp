// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// BC3 (DXT5) blocks decoded to RGBA: the colors and alphas between their endpoints, each pixel's
// index, and the blocks an image's edges cut.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;

namespace {

using Bytes = std::vector<std::byte>;
using Rgba = std::array<int, 4>;

template <class Value>
void append(Bytes& bytes, Value value) {
    const auto raw =
        std::bit_cast<std::array<std::byte, sizeof(Value)>>(value); // the tests run on little-endian hosts
    bytes.insert(bytes.end(), raw.begin(), raw.end());
}

/// A block: its alpha endpoints and each pixel's alpha index (3 bits), its 5-6-5 color endpoints and
/// each pixel's color index (2 bits), pixels row by row.
[[nodiscard]] auto block(std::uint8_t firstAlpha, std::uint8_t secondAlpha, const std::array<int, 16>& alphas,
                         std::uint16_t firstColor, std::uint16_t secondColor,
                         const std::array<int, 16>& colors) -> Bytes {
    Bytes bytes{std::byte{firstAlpha}, std::byte{secondAlpha}};
    std::uint64_t alphaBits = 0;
    std::uint32_t colorBits = 0;
    for (std::size_t pixel = 0; pixel < alphas.size(); ++pixel) {
        alphaBits |= static_cast<std::uint64_t>(alphas.at(pixel)) << (3 * pixel);
        colorBits |= static_cast<std::uint32_t>(colors.at(pixel)) << (2 * pixel);
    }
    for (std::size_t index = 0; index < 6; ++index) {
        bytes.push_back(static_cast<std::byte>((alphaBits >> (8 * index)) & 0xFF));
    }
    append(bytes, firstColor);
    append(bytes, secondColor);
    append(bytes, colorBits);
    return bytes;
}

[[nodiscard]] auto pixelAt(const nx::assets::RgbaImage& image, std::uint32_t x, std::uint32_t y) -> Rgba {
    const std::size_t at = ((std::size_t{y} * image.width) + x) * 4;
    return {std::to_integer<int>(image.pixels[at]), std::to_integer<int>(image.pixels[at + 1]),
            std::to_integer<int>(image.pixels[at + 2]), std::to_integer<int>(image.pixels[at + 3])};
}

constexpr std::uint16_t kRed = 0xF800;  // 255, 0, 0
constexpr std::uint16_t kBlue = 0x001F; // 0, 0, 255
constexpr std::array<int, 16> kEach{0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7};

TEST(BlockCompressionTest, TheBytesOfAnImageAreItsBlocksTheEdgesCut) {
    EXPECT_EQ(nx::assets::bc3Bytes(4, 4), 16U);
    EXPECT_EQ(nx::assets::bc3Bytes(256, 256), 65536U);
    EXPECT_EQ(nx::assets::bc3Bytes(5, 3), 32U) << "two blocks across, one down";
    EXPECT_EQ(nx::assets::bc3Bytes(1, 1), 16U);
}

TEST(BlockCompressionTest, ColorsAreTheEndpointsAndTheTwoBetween) {
    const Bytes blocks = block(255, 255, {}, kRed, kBlue, {0, 1, 2, 3});
    const nx::assets::RgbaImage image = nx::assets::decodeBc3(blocks, 4, 4);
    ASSERT_EQ(image.pixels.size(), 4U * 4U * 4U);
    EXPECT_EQ(pixelAt(image, 0, 0), (Rgba{255, 0, 0, 255}));
    EXPECT_EQ(pixelAt(image, 1, 0), (Rgba{0, 0, 255, 255}));
    EXPECT_EQ(pixelAt(image, 2, 0), (Rgba{170, 0, 85, 255})) << "two thirds of the first";
    EXPECT_EQ(pixelAt(image, 3, 0), (Rgba{85, 0, 170, 255})) << "a third of the first";
    // A 5-6-5 channel widens with its top bits: 16 of 31 is 132, 32 of 63 is 130.
    const nx::assets::RgbaImage gray = nx::assets::decodeBc3(block(255, 255, {}, 0x8410, 0x8410, {}), 4, 4);
    EXPECT_EQ(pixelAt(gray, 3, 3), (Rgba{132, 130, 132, 255}));
}

TEST(BlockCompressionTest, AlphasAreSevenStepsOrFiveWithTransparentAndOpaque) {
    // The first above the second: the endpoints and six between, in sevenths.
    const nx::assets::RgbaImage eight = nx::assets::decodeBc3(block(210, 0, kEach, kRed, kRed, {}), 4, 4);
    const std::array<int, 8> sevenths{210, 0, 180, 150, 120, 90, 60, 30};
    for (std::uint32_t pixel = 0; pixel < 8; ++pixel) {
        EXPECT_EQ(pixelAt(eight, pixel % 4, pixel / 4)[3], sevenths.at(pixel)) << pixel;
    }
    // Not above: four between, in fifths, then 0 and 255.
    const nx::assets::RgbaImage six = nx::assets::decodeBc3(block(0, 200, kEach, kRed, kRed, {}), 4, 4);
    const std::array<int, 8> fifths{0, 200, 40, 80, 120, 160, 0, 255};
    for (std::uint32_t pixel = 0; pixel < 8; ++pixel) {
        EXPECT_EQ(pixelAt(six, pixel % 4, pixel / 4)[3], fifths.at(pixel)) << pixel;
    }
}

TEST(BlockCompressionTest, BlocksAreRowByRowAndTheEdgesCutTheirPixels) {
    Bytes blocks = block(255, 255, {}, kRed, kRed, {});
    const Bytes second = block(255, 255, {}, kBlue, kBlue, {});
    blocks.insert(blocks.end(), second.begin(), second.end());
    const nx::assets::RgbaImage image = nx::assets::decodeBc3(blocks, 5, 3);
    ASSERT_EQ(image.width, 5U);
    ASSERT_EQ(image.height, 3U);
    ASSERT_EQ(image.pixels.size(), 5U * 3U * 4U);
    EXPECT_EQ(pixelAt(image, 3, 2), (Rgba{255, 0, 0, 255})) << "the first block";
    EXPECT_EQ(pixelAt(image, 4, 0), (Rgba{0, 0, 255, 255})) << "the second, cut to one column";
}

} // namespace
