// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;

namespace nx::assets {

namespace {

// The file header, little-endian: "BM", the file's size (32 bits), two reserved 16-bit words, and
// where the pixels start (32). Then the info header: its size (32), width and height (signed 32),
// planes and bits a pixel (16 each), compression (32), the image's size, the resolutions (32 each),
// the colors used and the important ones (32 each). Then the palette, then the pixels.
constexpr std::size_t kFileHeaderBytes = 14;
constexpr std::size_t kPixelOffsetOffset = 10;
constexpr std::size_t kInfoHeaderBytes = 40; ///< BITMAPINFOHEADER; later headers are longer.
constexpr std::size_t kInfoSizeOffset = kFileHeaderBytes;
constexpr std::size_t kWidthOffset = kFileHeaderBytes + 4;
constexpr std::size_t kHeightOffset = kFileHeaderBytes + 8;
constexpr std::size_t kPlanesOffset = kFileHeaderBytes + 12;
constexpr std::size_t kDepthOffset = kFileHeaderBytes + 14;
constexpr std::size_t kCompressionOffset = kFileHeaderBytes + 16;
constexpr std::size_t kColorsUsedOffset = kFileHeaderBytes + 32;

constexpr std::array kSignature{std::byte{'B'}, std::byte{'M'}};
constexpr std::uint32_t kUncompressed = 0; ///< BI_RGB.
constexpr std::uint16_t kOnePlane = 1;

// The depths the reader decodes, in bits a pixel.
constexpr std::uint16_t kIndexDepth = 8;
constexpr std::uint16_t kRgbDepth = 24;
constexpr std::uint16_t kRgbxDepth = 32;
constexpr std::uint32_t kMostColors = 256;
/// A palette entry: blue, green, red and a reserved byte.
constexpr std::size_t kPaletteEntryBytes = 4;
/// Rows are padded to a multiple of this many bytes.
constexpr std::size_t kRowAlignment = 4;
/// The largest side a BMP is read at: as large as any GPU's texture.
constexpr std::int32_t kLargestSide = 16384;

} // namespace

auto readBmp(std::span<const std::byte> bytes) -> Result<RgbaImage, BmpError> {
    if (bytes.size() < kFileHeaderBytes + kInfoHeaderBytes) {
        return fail(BmpError::TooSmall, std::format("{} bytes", bytes.size()));
    }
    if (!std::ranges::equal(bytes.first(kSignature.size()), kSignature)) {
        return fail(BmpError::BadValue, "no BM signature");
    }
    const auto infoSize = loadLittleEndian<std::uint32_t>(bytes.subspan(kInfoSizeOffset));
    const auto width = loadLittleEndian<std::int32_t>(bytes.subspan(kWidthOffset));
    const auto height = loadLittleEndian<std::int32_t>(bytes.subspan(kHeightOffset));
    const auto planes = loadLittleEndian<std::uint16_t>(bytes.subspan(kPlanesOffset));
    const auto depth = loadLittleEndian<std::uint16_t>(bytes.subspan(kDepthOffset));
    const auto compression = loadLittleEndian<std::uint32_t>(bytes.subspan(kCompressionOffset));
    const auto colorsUsed = loadLittleEndian<std::uint32_t>(bytes.subspan(kColorsUsedOffset));
    const auto pixelOffset = loadLittleEndian<std::uint32_t>(bytes.subspan(kPixelOffsetOffset));
    if (infoSize < kInfoHeaderBytes || planes != kOnePlane || width <= 0 || height == 0 ||
        width > kLargestSide || height > kLargestSide || height < -kLargestSide) {
        return fail(BmpError::BadValue, std::format("an info header of {} bytes, {} planes, {}x{}", infoSize,
                                                    planes, width, height));
    }
    if (compression != kUncompressed || (depth != kIndexDepth && depth != kRgbDepth && depth != kRgbxDepth)) {
        return fail(BmpError::Unsupported,
                    std::format("compression {} at {} bits a pixel", compression, depth));
    }
    const bool isIndexed = depth == kIndexDepth;
    const std::uint32_t colors = isIndexed ? (colorsUsed == 0 ? kMostColors : colorsUsed) : 0;
    if (colors > kMostColors) {
        return fail(BmpError::BadValue, std::format("{} colors", colors));
    }
    const std::size_t paletteStart = kFileHeaderBytes + infoSize;
    const std::size_t paletteEnd = paletteStart + (std::size_t{colors} * kPaletteEntryBytes);
    if (bytes.size() < paletteEnd || pixelOffset < paletteEnd) {
        return fail(BmpError::TooSmall,
                    std::format("a palette of {} colors ending at {}", colors, paletteEnd));
    }
    const auto columns = narrow<std::size_t>(width);
    const auto rows = narrow<std::size_t>(std::abs(height));
    const std::size_t pixelBytes = depth / std::size_t{8};
    const std::size_t rowBytes = ((columns * pixelBytes) + kRowAlignment - 1) / kRowAlignment * kRowAlignment;
    if (bytes.size() < pixelOffset || (bytes.size() - pixelOffset) / rowBytes < rows) {
        return fail(BmpError::Truncated,
                    std::format("{} rows of {} bytes from {}", rows, rowBytes, pixelOffset));
    }
    const std::span<const std::byte> palette = bytes.subspan(paletteStart, paletteEnd - paletteStart);
    RgbaImage image{.width = narrow<std::uint32_t>(columns), .height = narrow<std::uint32_t>(rows)};
    image.pixels.resize(columns * rows * kRgbaBytes);
    const bool isFromTheTop = height < 0;
    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t stored = isFromTheTop ? row : rows - 1 - row;
        const std::span<const std::byte> source = bytes.subspan(pixelOffset + (stored * rowBytes), rowBytes);
        for (std::size_t column = 0; column < columns; ++column) {
            std::span<const std::byte> blueGreenRed;
            if (isIndexed) {
                const auto index = std::to_integer<std::size_t>(source[column]);
                if (index >= colors) {
                    return fail(BmpError::BadValue, std::format("index {} of {} colors", index, colors));
                }
                blueGreenRed = palette.subspan(index * kPaletteEntryBytes, 3);
            } else {
                blueGreenRed = source.subspan(column * pixelBytes, 3);
            }
            const std::size_t at = ((row * columns) + column) * kRgbaBytes;
            image.pixels[at] = blueGreenRed[2];
            image.pixels[at + 1] = blueGreenRed[1];
            image.pixels[at + 2] = blueGreenRed[0];
            image.pixels[at + kRgbBytes] = kFullChannel;
        }
    }
    return image;
}

} // namespace nx::assets
