// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;

namespace nx::assets {

namespace {

// Where a BC3 block keeps each part: its alpha endpoints and their indices, 3 bits a pixel; its
// color endpoints and their indices, 2 bits a pixel; a pixel's bits at its place in the block, row by
// row, the lowest first.
constexpr std::size_t kAlphaIndicesOffset = 2;
constexpr std::size_t kAlphaIndicesBytes = 6;
constexpr std::size_t kFirstColorOffset = 8;
constexpr std::size_t kSecondColorOffset = 10;
constexpr std::size_t kColorIndicesOffset = 12;
constexpr std::uint32_t kAlphaIndexBits = 3;
constexpr std::uint32_t kAlphaIndexMask = 0b111;
constexpr std::uint32_t kColorIndexBits = 2;
constexpr std::uint32_t kColorIndexMask = 0b11;
constexpr std::uint32_t kBitsPerByte = 8;
constexpr std::size_t kPixelsPerBlock = std::size_t{kBlockSide} * kBlockSide;
constexpr std::size_t kRgbaBytes = 4;
/// A block's palettes: its alphas, and its colors.
constexpr std::size_t kAlphasPerBlock = 8;
constexpr std::size_t kColorsPerBlock = 4;

// A 5-6-5 color: red the top 5 bits, green the middle 6, blue the low 5.
constexpr std::uint32_t kRedShift = 11;
constexpr std::uint32_t kGreenShift = 5;
constexpr std::uint32_t kFiveBits = 0b11111;
constexpr std::uint32_t kSixBits = 0b111111;

// The colors between the endpoints, in thirds; the alphas, in sevenths, or in fifths with 0 and 255.
constexpr std::uint32_t kColorSteps = 3;
constexpr std::uint32_t kEightAlphaSteps = 7;
constexpr std::uint32_t kSixAlphaSteps = 5;
constexpr std::uint8_t kTransparent = 0;
constexpr std::uint8_t kOpaque = 255;

using Rgb = std::array<std::uint32_t, 3>;

/// A 5- or 6-bit channel widened to 8 bits, its top bits repeated below.
[[nodiscard]] constexpr auto widened(std::uint32_t value, std::uint32_t bits) noexcept -> std::uint32_t {
    return (value << (kBitsPerByte - bits)) | (value >> ((2 * bits) - kBitsPerByte));
}

[[nodiscard]] constexpr auto rgbOf(std::uint16_t color) noexcept -> Rgb {
    constexpr std::uint32_t kRedBits = 5;
    constexpr std::uint32_t kGreenBits = 6;
    return {widened((color >> kRedShift) & kFiveBits, kRedBits),
            widened((color >> kGreenShift) & kSixBits, kGreenBits), widened(color & kFiveBits, kRedBits)};
}

/// `first` and `second` mixed, `share` parts of `steps` of the second.
[[nodiscard]] constexpr auto mixed(std::uint32_t first, std::uint32_t second, std::uint32_t share,
                                   std::uint32_t steps) noexcept -> std::uint32_t {
    return ((first * (steps - share)) + (second * share)) / steps;
}

/// The eight alphas of a block's endpoints `first` and `second`.
[[nodiscard]] auto alphasOf(std::uint32_t first, std::uint32_t second) noexcept
    -> std::array<std::uint8_t, kAlphasPerBlock> {
    std::array<std::uint8_t, kAlphasPerBlock> alphas{static_cast<std::uint8_t>(first),
                                                     static_cast<std::uint8_t>(second)};
    if (first > second) {
        for (std::uint32_t share = 1; share < kEightAlphaSteps; ++share) {
            alphas.at(share + 1) = static_cast<std::uint8_t>(mixed(first, second, share, kEightAlphaSteps));
        }
    } else {
        for (std::uint32_t share = 1; share < kSixAlphaSteps; ++share) {
            alphas.at(share + 1) = static_cast<std::uint8_t>(mixed(first, second, share, kSixAlphaSteps));
        }
        alphas.at(kSixAlphaSteps + 1) = kTransparent;
        alphas.at(kSixAlphaSteps + 2) = kOpaque;
    }
    return alphas;
}

/// The four colors of a block's endpoints: BC3's color is always of four (not BC1's three and black).
[[nodiscard]] auto colorsOf(std::uint16_t first, std::uint16_t second) noexcept
    -> std::array<Rgb, kColorsPerBlock> {
    const Rgb low = rgbOf(first);
    const Rgb high = rgbOf(second);
    std::array<Rgb, kColorsPerBlock> colors{low, high, Rgb{}, Rgb{}};
    for (std::uint32_t share = 1; share < kColorSteps; ++share) {
        for (std::size_t channel = 0; channel < low.size(); ++channel) {
            colors.at(share + 1).at(channel) = mixed(low.at(channel), high.at(channel), share, kColorSteps);
        }
    }
    return colors;
}

/// Writes the pixels of `block`, the block at `blockX`, `blockY`, into `image`, those past its edges
/// left out.
void decodeBlock(std::span<const std::byte> block, std::uint32_t blockX, std::uint32_t blockY,
                 RgbaImage& image) {
    const std::array<std::uint8_t, kAlphasPerBlock> alphas =
        alphasOf(std::to_integer<std::uint32_t>(block[0]), std::to_integer<std::uint32_t>(block[1]));
    std::uint64_t alphaIndices = 0;
    for (std::size_t index = 0; index < kAlphaIndicesBytes; ++index) {
        alphaIndices |= std::to_integer<std::uint64_t>(block[kAlphaIndicesOffset + index])
                        << (kBitsPerByte * index);
    }
    const std::array<Rgb, kColorsPerBlock> colors =
        colorsOf(loadLittleEndian<std::uint16_t>(block.subspan(kFirstColorOffset)),
                 loadLittleEndian<std::uint16_t>(block.subspan(kSecondColorOffset)));
    const auto colorIndices = loadLittleEndian<std::uint32_t>(block.subspan(kColorIndicesOffset));
    for (std::size_t pixel = 0; pixel < kPixelsPerBlock; ++pixel) {
        const std::uint32_t x = (blockX * kBlockSide) + static_cast<std::uint32_t>(pixel % kBlockSide);
        const std::uint32_t y = (blockY * kBlockSide) + static_cast<std::uint32_t>(pixel / kBlockSide);
        if (x >= image.width || y >= image.height) {
            continue;
        }
        const Rgb& color = colors.at((colorIndices >> (kColorIndexBits * pixel)) & kColorIndexMask);
        const std::uint8_t alpha = alphas.at((alphaIndices >> (kAlphaIndexBits * pixel)) & kAlphaIndexMask);
        const std::size_t at = ((std::size_t{y} * image.width) + x) * kRgbaBytes;
        image.pixels[at] = static_cast<std::byte>(color[0]);
        image.pixels[at + 1] = static_cast<std::byte>(color[1]);
        image.pixels[at + 2] = static_cast<std::byte>(color[2]);
        image.pixels[at + 3] = static_cast<std::byte>(alpha);
    }
}

} // namespace

auto decodeBc3(std::span<const std::byte> blocks, std::uint32_t width, std::uint32_t height) -> RgbaImage {
    RgbaImage image{.width = width,
                    .height = height,
                    .pixels = std::vector<std::byte>(std::size_t{width} * height * kRgbaBytes)};
    expects(blocks.size() == bc3Bytes(width, height), "decodeBc3: the blocks of the image");
    if (blocks.size() != bc3Bytes(width, height)) {
        return image; // the handler let execution continue ("observe"): transparent
    }
    const std::uint32_t blocksAcross = divideRoundingUp(width, kBlockSide);
    for (std::size_t block = 0; block < blocks.size() / kBc3BlockBytes; ++block) {
        decodeBlock(blocks.subspan(block * kBc3BlockBytes, kBc3BlockBytes),
                    static_cast<std::uint32_t>(block % blocksAcross),
                    static_cast<std::uint32_t>(block / blocksAcross), image);
    }
    return image;
}

} // namespace nx::assets
