// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;

namespace nx::assets {

namespace {

// The header: "IDSP", the version, orientation, rendering, bounding radius, largest frame size,
// frame count, beam length and sync type (32 bits each); then a 16-bit color count and the colors.
constexpr std::array kSpriteMagic{std::byte{'I'}, std::byte{'D'}, std::byte{'S'}, std::byte{'P'}};
constexpr std::size_t kVersionOffset = 4;
constexpr std::size_t kOrientationOffset = 8;
constexpr std::size_t kRenderingOffset = 12;
constexpr std::size_t kRadiusOffset = 16;
constexpr std::size_t kMaxWidthOffset = 20;
constexpr std::size_t kMaxHeightOffset = 24;
constexpr std::size_t kFrameCountOffset = 28;
constexpr std::size_t kBeamLengthOffset = 32;
constexpr std::size_t kSyncOffset = 36;
constexpr std::size_t kPaletteCountOffset = 40;
constexpr std::size_t kPaletteOffset = kPaletteCountOffset + sizeof(std::uint16_t);
constexpr std::size_t kFramesOffset = kPaletteOffset + kPaletteBytes;

// A frame: its type (32 bits), then an image, or a group: a count, the intervals, the images. An
// image: origin x and y, width and height (32 bits each), then the pixels.
constexpr std::int32_t kSingleFrame = 0;
constexpr std::int32_t kGroupFrame = 1;
constexpr std::size_t kImageHeaderBytes = 16;
constexpr std::size_t kImageOriginYOffset = 4;
constexpr std::size_t kImageWidthOffset = 8;
constexpr std::size_t kImageHeightOffset = 12;

/// Reads the sprite from the start, `offset` moving past what each read takes.
class SpriteReader {
public:
    explicit SpriteReader(std::span<const std::byte> bytes) noexcept : m_bytes{bytes} {}

    /// The frames after the palette. Errors: BadValue, FrameOutOfBounds.
    [[nodiscard]] auto readFrames(std::uint32_t count) -> Result<std::vector<SpriteFrame>, SprError> {
        m_offset = kFramesOffset;
        std::vector<SpriteFrame> frames;
        for (std::uint32_t frame = 0; frame < count; ++frame) {
            auto read = readFrame(frame);
            if (!read) {
                return std::unexpected(std::move(read.error()));
            }
            frames.push_back(std::move(*read));
        }
        return frames;
    }

private:
    /// The next `size` bytes. Errors: FrameOutOfBounds.
    [[nodiscard]] auto take(std::uint64_t size, std::uint32_t frame)
        -> Result<std::span<const std::byte>, SprError> {
        if (size > m_bytes.size() - m_offset) {
            return fail(SprError::FrameOutOfBounds,
                        std::format("frame {}: {} bytes at {} are outside the file's {}", frame, size,
                                    m_offset, m_bytes.size()));
        }
        const auto taken = m_bytes.subspan(m_offset, static_cast<std::size_t>(size));
        m_offset += taken.size();
        return taken;
    }

    [[nodiscard]] auto readFrame(std::uint32_t frame) -> Result<SpriteFrame, SprError> {
        const auto type = take(sizeof(std::int32_t), frame);
        if (!type) {
            return std::unexpected(type.error());
        }
        switch (loadLittleEndian<std::int32_t>(*type)) {
        case kSingleFrame:
            return readImage(frame).transform(
                [](SpriteImage image) { return SpriteFrame{.images = {image}, .intervals = {}}; });
        case kGroupFrame:
            return readGroup(frame);
        default:
            return fail(SprError::BadValue,
                        std::format("frame {}: type {}, neither single ({}) nor group ({})", frame,
                                    loadLittleEndian<std::int32_t>(*type), kSingleFrame, kGroupFrame));
        }
    }

    [[nodiscard]] auto readGroup(std::uint32_t frame) -> Result<SpriteFrame, SprError> {
        const auto header = take(sizeof(std::int32_t), frame);
        if (!header) {
            return std::unexpected(header.error());
        }
        const auto count = loadLittleEndian<std::int32_t>(*header);
        if (count < 1) {
            return fail(SprError::BadValue, std::format("frame {}: a group of {} images", frame, count));
        }
        const auto intervals = take(std::uint64_t{static_cast<std::uint32_t>(count)} * sizeof(float), frame);
        if (!intervals) {
            return std::unexpected(intervals.error());
        }
        SpriteFrame group;
        for (std::size_t index = 0; index < static_cast<std::size_t>(count); ++index) {
            const float interval = loadLittleEndian<float>(intervals->subspan(index * sizeof(float)));
            const float previous = group.intervals.empty() ? 0.0F : group.intervals.back().count();
            if (!std::isfinite(interval) || interval <= previous) {
                return fail(SprError::BadValue, std::format("frame {}: interval {} is {}, not after {}",
                                                            frame, index, interval, previous));
            }
            group.intervals.emplace_back(interval);
        }
        for (std::int32_t image = 0; image < count; ++image) {
            auto read = readImage(frame);
            if (!read) {
                return std::unexpected(std::move(read.error()));
            }
            group.images.push_back(*read);
        }
        return group;
    }

    [[nodiscard]] auto readImage(std::uint32_t frame) -> Result<SpriteImage, SprError> {
        const auto header = take(kImageHeaderBytes, frame);
        if (!header) {
            return std::unexpected(header.error());
        }
        const auto width = loadLittleEndian<std::int32_t>(header->subspan(kImageWidthOffset));
        const auto height = loadLittleEndian<std::int32_t>(header->subspan(kImageHeightOffset));
        if (width < 1 || height < 1) {
            return fail(SprError::BadValue, std::format("frame {}: an image of {}x{}", frame, width, height));
        }
        const auto pixels = take(
            std::uint64_t{static_cast<std::uint32_t>(width)} * static_cast<std::uint32_t>(height), frame);
        if (!pixels) {
            return std::unexpected(pixels.error());
        }
        return SpriteImage{.originX = loadLittleEndian<std::int32_t>(*header),
                           .originY = loadLittleEndian<std::int32_t>(header->subspan(kImageOriginYOffset)),
                           .width = static_cast<std::uint32_t>(width),
                           .height = static_cast<std::uint32_t>(height),
                           .pixels = *pixels};
    }

    std::span<const std::byte> m_bytes;
    std::size_t m_offset = 0;
};

/// `value` as a counted enum. Errors: BadValue.
template <CountedEnum Enum>
[[nodiscard]] auto enumOf(std::int32_t value) -> Result<Enum, SprError> {
    if (value < 0 || std::cmp_greater_equal(value, enumCount<Enum>())) {
        return fail(SprError::BadValue, std::format("{} {} is not one of the {}", shortTypeName<Enum>(),
                                                    value, enumCount<Enum>()));
    }
    return static_cast<Enum>(value);
}

} // namespace

auto SpriteView::parse(std::span<const std::byte> bytes) -> Result<SpriteView, SprError> {
    if (bytes.size() < kFramesOffset) {
        return fail(SprError::TooSmall, std::format("{} bytes, less than the {} of the header and palette",
                                                    bytes.size(), kFramesOffset));
    }
    if (!std::ranges::equal(bytes.first(kSpriteMagic.size()), kSpriteMagic)) {
        return fail(SprError::NotSprite, "the file does not start with \"IDSP\"");
    }
    if (const auto version = loadLittleEndian<std::int32_t>(bytes.subspan(kVersionOffset));
        version != kSpriteVersion) {
        return fail(SprError::UnsupportedVersion, std::format("version {}, not {}", version, kSpriteVersion));
    }
    const auto orientation =
        enumOf<SpriteOrientation>(loadLittleEndian<std::int32_t>(bytes.subspan(kOrientationOffset)));
    const auto rendering =
        enumOf<SpriteRendering>(loadLittleEndian<std::int32_t>(bytes.subspan(kRenderingOffset)));
    const auto sync = enumOf<SpriteSync>(loadLittleEndian<std::int32_t>(bytes.subspan(kSyncOffset)));
    if (!orientation || !rendering || !sync) {
        return std::unexpected(!orientation ? orientation.error()
                               : !rendering ? rendering.error()
                                            : sync.error());
    }
    const auto radius = loadLittleEndian<float>(bytes.subspan(kRadiusOffset));
    const auto beamLength = loadLittleEndian<float>(bytes.subspan(kBeamLengthOffset));
    const auto maxWidth = loadLittleEndian<std::int32_t>(bytes.subspan(kMaxWidthOffset));
    const auto maxHeight = loadLittleEndian<std::int32_t>(bytes.subspan(kMaxHeightOffset));
    const auto frameCount = loadLittleEndian<std::int32_t>(bytes.subspan(kFrameCountOffset));
    const auto colors = loadLittleEndian<std::uint16_t>(bytes.subspan(kPaletteCountOffset));
    if (!std::isfinite(radius) || !std::isfinite(beamLength) || maxWidth < 0 || maxHeight < 0 ||
        frameCount < 1 || colors != kPaletteColors) {
        return fail(
            SprError::BadValue,
            std::format(
                "radius {}, beam length {}, largest frame {}x{}, {} frames, {} palette colors (of {})",
                radius, beamLength, maxWidth, maxHeight, frameCount, colors, kPaletteColors));
    }
    auto frames = SpriteReader{bytes}.readFrames(static_cast<std::uint32_t>(frameCount));
    if (!frames) {
        return std::unexpected(std::move(frames.error()));
    }
    return SpriteView{.orientation = *orientation,
                      .rendering = *rendering,
                      .boundingRadius = radius,
                      .maxWidth = static_cast<std::uint32_t>(maxWidth),
                      .maxHeight = static_cast<std::uint32_t>(maxHeight),
                      .beamLength = beamLength,
                      .sync = *sync,
                      .palette = bytes.subspan(kPaletteOffset, kPaletteBytes),
                      .frames = std::move(*frames)};
}

} // namespace nx::assets
