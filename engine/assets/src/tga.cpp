// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;

namespace nx::assets {

namespace {

// The header, little-endian: the ID length, the color map type and the image type (8 bits each);
// the color map's first index and length (16 bits) and entry size (8); the image's origin, width
// and height (16 bits each), pixel depth and descriptor (8). Then the ID, the color map, the pixels.
constexpr std::size_t kHeaderBytes = 18;
constexpr std::size_t kColorMapTypeOffset = 1;
constexpr std::size_t kImageTypeOffset = 2;
constexpr std::size_t kColorMapFirstOffset = 3;
constexpr std::size_t kColorMapLengthOffset = 5;
constexpr std::size_t kColorMapEntrySizeOffset = 7;
constexpr std::size_t kWidthOffset = 12;
constexpr std::size_t kHeightOffset = 14;
constexpr std::size_t kPixelDepthOffset = 16;
constexpr std::size_t kDescriptorOffset = 17;

constexpr std::uint8_t kNoColorMap = 0;
constexpr std::uint8_t kHasColorMap = 1;

/// The kinds of image, as the file numbers them; each plus kRunLength is the same, run-length
/// encoded.
enum class ImageKind : std::uint8_t {
    None,
    ColorMapped,
    TrueColor,
    Grayscale,
};
constexpr std::uint8_t kRunLength = 8;

// The depths the reader decodes, in bits per pixel: a color map's index; true color without and
// with alpha, which are also the depths of a color map's entries; gray without and with alpha.
constexpr std::uint8_t kIndexDepth = 8;
constexpr std::uint8_t kRgbDepth = 24;
constexpr std::uint8_t kRgbaDepth = 32;
constexpr std::uint8_t kGrayDepth = 8;
constexpr std::uint8_t kGrayAlphaDepth = 16;

// Where each channel of a stored true color is: blue, green, red, then alpha when it has one; and
// the alpha of a gray pixel that has one, after its gray.
constexpr std::size_t kStoredBlue = 0;
constexpr std::size_t kStoredGreen = 1;
constexpr std::size_t kStoredRed = 2;
constexpr std::size_t kStoredAlpha = 3;
constexpr std::size_t kStoredGrayAlpha = 1;

// The descriptor: the alpha depth (bits 0-3), pixels from the right (bit 4), rows from the top
// (bit 5), and an interleaving of the rows (bits 6-7) of 1989, which no writer uses.
constexpr std::uint8_t kFromTheRight = 0x10;
constexpr std::uint8_t kFromTheTop = 0x20;
constexpr std::uint8_t kInterleaving = 0xC0;

// A run-length packet: a byte whose high bit makes it a run (one pixel, repeated) and whose low 7
// bits are its pixel count minus one, then its pixels.
constexpr std::uint8_t kRunPacket = 0x80;
constexpr std::uint8_t kPacketCount = 0x7F;
constexpr std::uint64_t kMostPacketPixels = 128;

constexpr std::size_t kRgbaBytes = 4;
constexpr std::byte kOpaque{255};

using Rgba = std::array<std::byte, kRgbaBytes>;

/// How the pixels of an image are stored.
struct PixelFormat {
    ImageKind kind = ImageKind::TrueColor;
    bool isRunLength = false;
    std::size_t bytesPerPixel = 0;
};

/// The pixel format of image type `type` at `depth` bits per pixel. Errors: BadValue, Unsupported.
[[nodiscard]] auto pixelFormatOf(std::uint8_t type, std::uint8_t depth) -> Result<PixelFormat, TgaError> {
    if (type == std::to_underlying(ImageKind::None)) {
        return fail(TgaError::BadValue, "image type 0: no image");
    }
    const bool isRunLength = type > kRunLength;
    const auto kind = static_cast<ImageKind>(isRunLength ? type - kRunLength : type);
    const bool isDecoded =
        (kind == ImageKind::ColorMapped && depth == kIndexDepth) ||
        (kind == ImageKind::TrueColor && (depth == kRgbDepth || depth == kRgbaDepth)) ||
        (kind == ImageKind::Grayscale && (depth == kGrayDepth || depth == kGrayAlphaDepth));
    if (!isDecoded) {
        return fail(TgaError::Unsupported, std::format("image type {} at {} bits per pixel", type, depth));
    }
    return PixelFormat{.kind = kind, .isRunLength = isRunLength, .bytesPerPixel = bytesForBits(depth)};
}

/// The color of true color pixel `stored`: blue, green, red and, at its full depth, alpha.
[[nodiscard]] auto trueColorOf(std::span<const std::byte> stored) noexcept -> Rgba {
    return Rgba{stored[kStoredRed], stored[kStoredGreen], stored[kStoredBlue],
                stored.size() == bytesForBits(kRgbaDepth) ? stored[kStoredAlpha] : kOpaque};
}

/// The colors of a color map: entries of 24 or 32 bits. Errors: Unsupported.
[[nodiscard]] auto colorsOf(std::span<const std::byte> map, std::uint8_t entrySize)
    -> Result<std::vector<Rgba>, TgaError> {
    if (entrySize != kRgbDepth && entrySize != kRgbaDepth) {
        return fail(TgaError::Unsupported, std::format("color map entries of {} bits", entrySize));
    }
    const std::size_t entryBytes = bytesForBits(entrySize);
    std::vector<Rgba> colors;
    colors.reserve(map.size() / entryBytes);
    for (std::size_t offset = 0; offset < map.size(); offset += entryBytes) {
        colors.push_back(trueColorOf(map.subspan(offset, entryBytes)));
    }
    return colors;
}

/// Decodes the pixels of an image, in the order the file stores them.
class PixelDecoder {
public:
    PixelDecoder(PixelFormat format, std::size_t firstColor, std::vector<Rgba> colors) noexcept
        : m_format{format}, m_firstColor{firstColor}, m_colors{std::move(colors)} {}

    /// Decodes `data` into `pixels`, in the file's order, packet by packet: a raw image is one packet
    /// of every pixel. Errors: BadValue, Truncated.
    [[nodiscard]] auto decode(std::span<const std::byte> data, std::span<std::byte> pixels) const
        -> Result<void, TgaError> {
        const std::size_t count = pixels.size() / kRgbaBytes;
        std::size_t offset = 0;
        for (std::size_t pixel = 0; pixel < count;) {
            std::size_t packetPixels = count;
            bool isRun = false;
            if (m_format.isRunLength) {
                if (offset == data.size()) {
                    return truncatedAt(pixel, count);
                }
                const auto packet = std::to_integer<std::uint8_t>(data[offset++]);
                isRun = (packet & kRunPacket) != 0;
                packetPixels = static_cast<std::size_t>(packet & kPacketCount) + 1;
            }
            packetPixels = std::min(packetPixels, count - pixel); // the rest is past the image
            const std::size_t stored = (isRun ? 1 : packetPixels) * m_format.bytesPerPixel;
            if (stored > data.size() - offset) {
                return truncatedAt(pixel, count);
            }
            for (std::size_t index = 0; index < packetPixels; ++index) {
                const std::size_t source = offset + (isRun ? 0 : index * m_format.bytesPerPixel);
                const auto color = colorOf(data.subspan(source, m_format.bytesPerPixel), pixel + index);
                if (!color) {
                    return std::unexpected{color.error()};
                }
                std::ranges::copy(*color, pixels.subspan((pixel + index) * kRgbaBytes).begin());
            }
            offset += stored;
            pixel += packetPixels;
        }
        return {};
    }

private:
    [[nodiscard]] static auto truncatedAt(std::size_t pixel, std::size_t count)
        -> std::unexpected<Failure<TgaError>> {
        return fail(TgaError::Truncated, std::format("the pixels end at pixel {} of {}", pixel, count));
    }

    /// The color of the pixel stored in `stored`. Errors: BadValue.
    [[nodiscard]] auto colorOf(std::span<const std::byte> stored, std::size_t pixel) const
        -> Result<Rgba, TgaError> {
        switch (m_format.kind) {
        case ImageKind::ColorMapped: {
            const auto index = std::to_integer<std::size_t>(stored[0]);
            if (index < m_firstColor || index - m_firstColor >= m_colors.size()) {
                return fail(TgaError::BadValue,
                            std::format("pixel {} is color {}, outside the map's {} from {}", pixel, index,
                                        m_colors.size(), m_firstColor));
            }
            return m_colors[index - m_firstColor];
        }
        case ImageKind::TrueColor:
            return trueColorOf(stored);
        case ImageKind::Grayscale:
        case ImageKind::None: // pixelFormatOf refuses it
            break;
        }
        const bool hasAlpha = m_format.bytesPerPixel == bytesForBits(kGrayAlphaDepth);
        return Rgba{stored[0], stored[0], stored[0], hasAlpha ? stored[kStoredGrayAlpha] : kOpaque};
    }

    PixelFormat m_format;
    std::size_t m_firstColor;
    std::vector<Rgba> m_colors;
};

/// Puts the pixels of `image`, decoded in the file's order, in rows from the top-left corner.
void orient(RgbaImage& image, std::uint8_t descriptor) {
    const std::size_t rowBytes = std::size_t{image.width} * kRgbaBytes;
    const auto row = [&image, rowBytes](std::size_t index) {
        return std::span{image.pixels}.subspan(index * rowBytes, rowBytes);
    };
    if ((descriptor & kFromTheTop) == 0) {
        for (std::size_t top = 0, bottom = image.height - 1; top < bottom; ++top, --bottom) {
            std::ranges::swap_ranges(row(top), row(bottom));
        }
    }
    if ((descriptor & kFromTheRight) != 0) {
        for (std::size_t index = 0; index < image.height; ++index) {
            const std::span<std::byte> pixels = row(index);
            const auto pixel = [pixels](std::size_t at) {
                return pixels.subspan(at * kRgbaBytes, kRgbaBytes);
            };
            for (std::size_t left = 0, right = image.width - 1; left < right; ++left, --right) {
                std::ranges::swap_ranges(pixel(left), pixel(right));
            }
        }
    }
}

} // namespace

auto readTga(std::span<const std::byte> bytes) -> Result<RgbaImage, TgaError> {
    if (bytes.size() < kHeaderBytes) {
        return fail(TgaError::TooSmall,
                    std::format("{} bytes, less than the {} of the header", bytes.size(), kHeaderBytes));
    }
    const auto idLength = std::to_integer<std::size_t>(bytes[0]);
    const auto colorMapType = std::to_integer<std::uint8_t>(bytes[kColorMapTypeOffset]);
    const auto imageType = std::to_integer<std::uint8_t>(bytes[kImageTypeOffset]);
    const auto firstColor = loadLittleEndian<std::uint16_t>(bytes.subspan(kColorMapFirstOffset));
    const auto colorCount = loadLittleEndian<std::uint16_t>(bytes.subspan(kColorMapLengthOffset));
    const auto entrySize = std::to_integer<std::uint8_t>(bytes[kColorMapEntrySizeOffset]);
    const auto width = loadLittleEndian<std::uint16_t>(bytes.subspan(kWidthOffset));
    const auto height = loadLittleEndian<std::uint16_t>(bytes.subspan(kHeightOffset));
    const auto descriptor = std::to_integer<std::uint8_t>(bytes[kDescriptorOffset]);

    const auto format = pixelFormatOf(imageType, std::to_integer<std::uint8_t>(bytes[kPixelDepthOffset]));
    if (!format) {
        return std::unexpected{format.error()};
    }
    const bool hasColorMap = colorMapType == kHasColorMap;
    if ((colorMapType != kNoColorMap && !hasColorMap) || width == 0 || height == 0 ||
        (format->kind == ImageKind::ColorMapped && (!hasColorMap || colorCount == 0))) {
        return fail(TgaError::BadValue,
                    std::format("color map type {} of {} colors, image type {}, an image of {}x{}",
                                colorMapType, colorCount, imageType, width, height));
    }
    if ((descriptor & kInterleaving) != 0) {
        return fail(TgaError::Unsupported, std::format("interleaved rows (descriptor {:#04x})", descriptor));
    }

    const std::size_t mapOffset = kHeaderBytes + idLength;
    const std::size_t mapBytes = hasColorMap ? std::size_t{colorCount} * bytesForBits(entrySize) : 0;
    if (bytes.size() < mapOffset + mapBytes) {
        return fail(TgaError::TooSmall,
                    std::format("{} bytes, less than the {} of the header, ID and color map", bytes.size(),
                                mapOffset + mapBytes));
    }
    std::vector<Rgba> colors;
    if (format->kind == ImageKind::ColorMapped) {
        auto read = colorsOf(bytes.subspan(mapOffset, mapBytes), entrySize);
        if (!read) {
            return std::unexpected{read.error()};
        }
        colors = std::move(*read);
    }

    // Refused before anything is allocated: fewer bytes than the pixels need even as runs of 128.
    const std::span<const std::byte> data = bytes.subspan(mapOffset + mapBytes);
    const std::uint64_t pixelCount = std::uint64_t{width} * height;
    const std::uint64_t fewestBytes =
        format->isRunLength
            ? ((pixelCount + kMostPacketPixels - 1) / kMostPacketPixels) * (1 + format->bytesPerPixel)
            : pixelCount * format->bytesPerPixel;
    if (data.size() < fewestBytes) {
        return fail(TgaError::Truncated, std::format("{} bytes of pixels, fewer than the {} of a {}x{} image",
                                                     data.size(), fewestBytes, width, height));
    }

    RgbaImage image{.width = width,
                    .height = height,
                    .pixels = std::vector<std::byte>(static_cast<std::size_t>(pixelCount) * kRgbaBytes)};
    const PixelDecoder decoder{*format, firstColor, std::move(colors)};
    if (auto decoded = decoder.decode(data, image.pixels); !decoded) {
        return std::unexpected{std::move(decoded.error())};
    }
    orient(image, descriptor);
    return image;
}

} // namespace nx::assets
