// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;

namespace {

using nx::assets::RgbaImage;
using nx::assets::TgaError;

using Bytes = std::vector<std::byte>;
using Pixel = std::array<int, 4>;

template <class Value>
void append(Bytes& bytes, Value value) {
    const auto raw =
        std::bit_cast<std::array<std::byte, sizeof(Value)>>(value); // the tests run on little-endian hosts
    bytes.insert(bytes.end(), raw.begin(), raw.end());
}

[[nodiscard]] auto bytesOf(std::initializer_list<int> values) -> Bytes {
    return values | std::views::transform([](int value) { return static_cast<std::byte>(value); }) |
           std::ranges::to<Bytes>();
}

struct TgaHeader {
    std::uint8_t idLength = 0;
    std::uint8_t colorMapType = 0;
    std::uint8_t imageType = 2;
    std::uint16_t firstColor = 0;
    std::uint16_t colorCount = 0;
    std::uint8_t entrySize = 0;
    std::uint16_t width = 2;
    std::uint16_t height = 1;
    std::uint8_t depth = 24;
    std::uint8_t descriptor = 0x20; // rows from the top
};

/// A TGA file: `header`, then `rest` (the ID, the color map and the pixels).
[[nodiscard]] auto tgaFile(const TgaHeader& header, const Bytes& rest) -> Bytes {
    Bytes bytes;
    append(bytes, header.idLength);
    append(bytes, header.colorMapType);
    append(bytes, header.imageType);
    append(bytes, header.firstColor);
    append(bytes, header.colorCount);
    append(bytes, header.entrySize);
    append(bytes, std::uint16_t{0}); // x origin
    append(bytes, std::uint16_t{0}); // y origin
    append(bytes, header.width);
    append(bytes, header.height);
    append(bytes, header.depth);
    append(bytes, header.descriptor);
    bytes.insert(bytes.end(), rest.begin(), rest.end());
    return bytes;
}

[[nodiscard]] auto pixelAt(const RgbaImage& image, std::uint32_t x, std::uint32_t y) -> Pixel {
    const std::size_t offset = ((std::size_t{y} * image.width) + x) * 4;
    return {std::to_integer<int>(image.pixels[offset]), std::to_integer<int>(image.pixels[offset + 1]),
            std::to_integer<int>(image.pixels[offset + 2]), std::to_integer<int>(image.pixels[offset + 3])};
}

[[nodiscard]] auto errorOf(const Bytes& file) -> std::optional<TgaError> {
    const auto image = nx::assets::readTga(file);
    return image ? std::nullopt : std::optional{image.error().code};
}

TEST(TgaTest, ReadsTrueColorStoredFromTheBottomLeftAsTheSkiesAre) {
    // Blue-green-red: the bottom row (red, green) comes first.
    const auto image = nx::assets::readTga(
        tgaFile({.height = 2, .descriptor = 0}, bytesOf({0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255})));
    ASSERT_TRUE(image.has_value()) << nx::describe(image.error());
    EXPECT_EQ(image->width, 2U);
    EXPECT_EQ(image->height, 2U);
    EXPECT_EQ(pixelAt(*image, 0, 0), (Pixel{0, 0, 255, 255})) << "top-left: blue";
    EXPECT_EQ(pixelAt(*image, 1, 0), (Pixel{255, 255, 255, 255}));
    EXPECT_EQ(pixelAt(*image, 0, 1), (Pixel{255, 0, 0, 255})) << "bottom-left: red";
    EXPECT_EQ(pixelAt(*image, 1, 1), (Pixel{0, 255, 0, 255}));
}

TEST(TgaTest, TheFourthByteIsAlphaEvenWhenTheDescriptorDeclaresNone) {
    const auto image =
        nx::assets::readTga(tgaFile({.depth = 32, .descriptor = 0x20}, bytesOf({1, 2, 3, 4, 5, 6, 7, 8})));
    ASSERT_TRUE(image.has_value()) << nx::describe(image.error());
    EXPECT_EQ(pixelAt(*image, 0, 0), (Pixel{3, 2, 1, 4}));
    EXPECT_EQ(pixelAt(*image, 1, 0), (Pixel{7, 6, 5, 8}));
}

TEST(TgaTest, ARunLengthPacketMayContinueOnTheNextRow) {
    // A run of 4 of (1, 2, 3), then 2 raw pixels.
    const auto image = nx::assets::readTga(tgaFile({.imageType = 10, .width = 3, .height = 2},
                                                   bytesOf({0x83, 3, 2, 1, 0x01, 6, 5, 4, 9, 8, 7})));
    ASSERT_TRUE(image.has_value()) << nx::describe(image.error());
    for (std::uint32_t x = 0; x < 3; ++x) {
        EXPECT_EQ(pixelAt(*image, x, 0), (Pixel{1, 2, 3, 255}));
    }
    EXPECT_EQ(pixelAt(*image, 0, 1), (Pixel{1, 2, 3, 255}));
    EXPECT_EQ(pixelAt(*image, 1, 1), (Pixel{4, 5, 6, 255}));
    EXPECT_EQ(pixelAt(*image, 2, 1), (Pixel{7, 8, 9, 255}));
}

TEST(TgaTest, WhatAPacketHoldsPastTheLastPixelIsIgnored) {
    const auto run = nx::assets::readTga(tgaFile({.imageType = 10}, bytesOf({0xFF, 3, 2, 1})));
    ASSERT_TRUE(run.has_value()) << nx::describe(run.error());
    EXPECT_EQ(pixelAt(*run, 1, 0), (Pixel{1, 2, 3, 255}));
    // Four raw pixels announced; the image has two, and the file stops there.
    const auto raw = nx::assets::readTga(tgaFile({.imageType = 10}, bytesOf({0x03, 3, 2, 1, 6, 5, 4})));
    ASSERT_TRUE(raw.has_value()) << nx::describe(raw.error());
    EXPECT_EQ(pixelAt(*raw, 1, 0), (Pixel{4, 5, 6, 255}));
}

TEST(TgaTest, ColorMappedPixelsIndexTheirMapFromItsFirstColor) {
    const TgaHeader header{
        .colorMapType = 1, .imageType = 1, .firstColor = 2, .colorCount = 2, .entrySize = 32, .depth = 8};
    const Bytes map = bytesOf({10, 20, 30, 40, 50, 60, 70, 80});
    Bytes rest = map;
    rest.insert(rest.end(), {std::byte{3}, std::byte{2}});
    const auto image = nx::assets::readTga(tgaFile(header, rest));
    ASSERT_TRUE(image.has_value()) << nx::describe(image.error());
    EXPECT_EQ(pixelAt(*image, 0, 0), (Pixel{70, 60, 50, 80}));
    EXPECT_EQ(pixelAt(*image, 1, 0), (Pixel{30, 20, 10, 40}));

    for (const int outside : {1, 4}) {
        Bytes refused = map;
        refused.insert(refused.end(), {std::byte{2}, static_cast<std::byte>(outside)});
        EXPECT_EQ(errorOf(tgaFile(header, refused)), TgaError::BadValue) << "color " << outside;
    }
}

TEST(TgaTest, GrayscaleHasItsAlphaAtSixteenBits) {
    const auto withAlpha =
        nx::assets::readTga(tgaFile({.imageType = 11, .depth = 16}, bytesOf({0x81, 100, 50})));
    ASSERT_TRUE(withAlpha.has_value()) << nx::describe(withAlpha.error());
    EXPECT_EQ(pixelAt(*withAlpha, 1, 0), (Pixel{100, 100, 100, 50}));
    const auto opaque = nx::assets::readTga(tgaFile({.imageType = 3, .depth = 8}, bytesOf({7, 9})));
    ASSERT_TRUE(opaque.has_value()) << nx::describe(opaque.error());
    EXPECT_EQ(pixelAt(*opaque, 1, 0), (Pixel{9, 9, 9, 255}));
}

TEST(TgaTest, PixelsStoredFromTheRightAreMirrored) {
    const auto image = nx::assets::readTga(tgaFile({.descriptor = 0x30}, bytesOf({3, 2, 1, 6, 5, 4})));
    ASSERT_TRUE(image.has_value()) << nx::describe(image.error());
    EXPECT_EQ(pixelAt(*image, 0, 0), (Pixel{4, 5, 6, 255}));
    EXPECT_EQ(pixelAt(*image, 1, 0), (Pixel{1, 2, 3, 255}));
}

TEST(TgaTest, SkipsTheIdAndTheColorMapOfATrueColorImage) {
    const auto image = nx::assets::readTga(
        tgaFile({.idLength = 3, .colorMapType = 1, .colorCount = 2, .entrySize = 24, .width = 1},
                bytesOf({'a', 'b', 'c', 9, 9, 9, 9, 9, 9, 3, 2, 1})));
    ASSERT_TRUE(image.has_value()) << nx::describe(image.error());
    EXPECT_EQ(pixelAt(*image, 0, 0), (Pixel{1, 2, 3, 255}));
}

TEST(TgaTest, RefusesWhatItDoesNotDecode) {
    const Bytes pixels(16, std::byte{1});
    EXPECT_EQ(errorOf(tgaFile({.depth = 16}, pixels)), TgaError::Unsupported) << "16-bit true color";
    EXPECT_EQ(errorOf(tgaFile({.imageType = 32}, pixels)), TgaError::Unsupported) << "Huffman";
    EXPECT_EQ(errorOf(tgaFile(
                  {.colorMapType = 1, .imageType = 1, .colorCount = 1, .entrySize = 16, .depth = 8}, pixels)),
              TgaError::Unsupported)
        << "16-bit colors";
    EXPECT_EQ(errorOf(tgaFile({.descriptor = 0x60}, pixels)), TgaError::Unsupported) << "interleaved rows";
}

TEST(TgaTest, RefusesHeaderValuesOutsideTheirDomain) {
    const Bytes pixels(16, std::byte{1});
    EXPECT_EQ(errorOf(tgaFile({.imageType = 0}, pixels)), TgaError::BadValue) << "no image";
    EXPECT_EQ(errorOf(tgaFile({.width = 0}, pixels)), TgaError::BadValue);
    EXPECT_EQ(errorOf(tgaFile({.colorMapType = 2}, pixels)), TgaError::BadValue);
    EXPECT_EQ(errorOf(tgaFile({.imageType = 1, .depth = 8}, pixels)), TgaError::BadValue) << "no color map";
}

TEST(TgaTest, RefusesTruncatedFiles) {
    EXPECT_EQ(errorOf(Bytes(17, std::byte{0})), TgaError::TooSmall);
    EXPECT_EQ(errorOf(tgaFile({.idLength = 10}, {})), TgaError::TooSmall) << "the ID";
    EXPECT_EQ(errorOf(tgaFile({.height = 2}, Bytes(11, std::byte{1}))), TgaError::Truncated);
    EXPECT_EQ(errorOf(tgaFile({.imageType = 10, .width = 4}, bytesOf({0x81, 3, 2, 1}))), TgaError::Truncated)
        << "the packets end at pixel 2";
    EXPECT_EQ(errorOf(tgaFile({.imageType = 10, .width = 2}, bytesOf({0x01, 3, 2, 1}))), TgaError::Truncated)
        << "a raw packet without its second pixel";
    EXPECT_EQ(errorOf(tgaFile({.imageType = 10, .width = 65535, .height = 65535}, bytesOf({0xFF, 3, 2, 1}))),
              TgaError::Truncated)
        << "refused before 16 GiB are allocated";
}

} // namespace
