// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;

namespace {

using nx::assets::SprError;
using nx::assets::SpriteOrientation;
using nx::assets::SpriteRendering;
using nx::assets::SpriteSync;
using nx::assets::SpriteView;

using Bytes = std::vector<std::byte>;

template <class Value>
void append(Bytes& bytes, Value value) {
    const auto raw =
        std::bit_cast<std::array<std::byte, sizeof(Value)>>(value); // the tests run on little-endian hosts
    bytes.insert(bytes.end(), raw.begin(), raw.end());
}

/// An image of `width` by `height`, its pixels counting up from `first`.
void appendImage(Bytes& bytes, std::int32_t width, std::int32_t height, std::uint8_t first) {
    append(bytes, -width / 2); // origin x
    append(bytes, height / 2); // origin y
    append(bytes, width);
    append(bytes, height);
    for (std::int32_t pixel = 0; pixel < width * height; ++pixel) {
        bytes.push_back(static_cast<std::byte>(first + pixel));
    }
}

/// The header of a sprite with `frames` frames, and its palette: color c is (c, c, c).
struct SpriteHeader {
    std::int32_t version = 2;
    std::int32_t orientation = 2;
    std::int32_t rendering = 1;
    float radius = 8.0F;
    std::int32_t maxWidth = 4;
    std::int32_t maxHeight = 4;
    std::int32_t frames = 1;
    float beamLength = 0.0F;
    std::int32_t sync = 0;
    std::uint16_t colors = 256;
};

[[nodiscard]] auto spriteFile(const SpriteHeader& header, const Bytes& frames) -> Bytes {
    Bytes bytes{std::byte{'I'}, std::byte{'D'}, std::byte{'S'}, std::byte{'P'}};
    append(bytes, header.version);
    append(bytes, header.orientation);
    append(bytes, header.rendering);
    append(bytes, header.radius);
    append(bytes, header.maxWidth);
    append(bytes, header.maxHeight);
    append(bytes, header.frames);
    append(bytes, header.beamLength);
    append(bytes, header.sync);
    append(bytes, header.colors);
    for (std::size_t color = 0; color < 256; ++color) {
        bytes.insert(bytes.end(), 3, static_cast<std::byte>(color));
    }
    bytes.insert(bytes.end(), frames.begin(), frames.end());
    return bytes;
}

[[nodiscard]] auto singleFrame(std::int32_t width, std::int32_t height) -> Bytes {
    Bytes frame;
    append(frame, std::int32_t{0});
    appendImage(frame, width, height, 1);
    return frame;
}

/// A group of `intervals.size()` 2x2 images.
[[nodiscard]] auto groupFrame(std::span<const float> intervals) -> Bytes {
    Bytes frame;
    append(frame, std::int32_t{1});
    append(frame, static_cast<std::int32_t>(intervals.size()));
    for (const float interval : intervals) {
        append(frame, interval);
    }
    for (std::size_t image = 0; image < intervals.size(); ++image) {
        appendImage(frame, 2, 2, static_cast<std::uint8_t>(10 * image));
    }
    return frame;
}

[[nodiscard]] auto parseError(const Bytes& file) -> SprError {
    const auto sprite = SpriteView::parse(file);
    EXPECT_FALSE(sprite.has_value());
    return sprite ? SprError{} : sprite.error().code;
}

TEST(SpriteTest, ReadsASpriteOfOneFrame) {
    const Bytes file = spriteFile({}, singleFrame(4, 2));
    const auto sprite = SpriteView::parse(file);
    ASSERT_TRUE(sprite.has_value()) << nx::describe(sprite.error());
    EXPECT_EQ(sprite->orientation, SpriteOrientation::Parallel);
    EXPECT_EQ(sprite->rendering, SpriteRendering::Additive);
    EXPECT_EQ(sprite->sync, SpriteSync::Synchronized);
    EXPECT_EQ(sprite->boundingRadius, 8.0F);
    EXPECT_EQ(sprite->maxWidth, 4U);
    ASSERT_EQ(sprite->palette.size(), nx::assets::kPaletteBytes);
    EXPECT_EQ(sprite->palette[3 * 7], std::byte{7});

    ASSERT_EQ(sprite->frames.size(), 1U);
    EXPECT_TRUE(sprite->frames[0].intervals.empty());
    ASSERT_EQ(sprite->frames[0].images.size(), 1U);
    const auto& image = sprite->frames[0].images[0];
    EXPECT_EQ(image.originX, -2);
    EXPECT_EQ(image.originY, 1);
    EXPECT_EQ(image.width, 4U);
    EXPECT_EQ(image.height, 2U);
    ASSERT_EQ(image.pixels.size(), 8U);
    EXPECT_EQ(image.pixels[7], std::byte{8});
}

TEST(SpriteTest, ReadsTimedGroupsAmongFrames) {
    const std::array intervals{0.1F, 0.25F, 0.5F};
    Bytes frames = singleFrame(2, 2);
    const Bytes group = groupFrame(intervals);
    frames.insert(frames.end(), group.begin(), group.end());

    const Bytes file = spriteFile({.frames = 2}, frames);
    const auto sprite = SpriteView::parse(file);
    ASSERT_TRUE(sprite.has_value()) << nx::describe(sprite.error());
    ASSERT_EQ(sprite->frames.size(), 2U);
    const auto& timed = sprite->frames[1];
    ASSERT_EQ(timed.images.size(), 3U);
    EXPECT_EQ(timed.intervals,
              (std::vector<nx::Seconds>{nx::Seconds{0.1F}, nx::Seconds{0.25F}, nx::Seconds{0.5F}}));
    EXPECT_EQ(timed.images[2].pixels[0], std::byte{20});
}

TEST(SpriteTest, RejectsABrokenHeader) {
    EXPECT_EQ(parseError(Bytes(100)), SprError::TooSmall);
    Bytes idpo = spriteFile({}, singleFrame(2, 2));
    idpo[3] = std::byte{'O'};
    EXPECT_EQ(parseError(idpo), SprError::NotSprite);
    EXPECT_EQ(parseError(spriteFile({.version = 1}, singleFrame(2, 2))), SprError::UnsupportedVersion);

    for (const SpriteHeader& header :
         {SpriteHeader{.orientation = 5}, SpriteHeader{.rendering = 4}, SpriteHeader{.sync = 2},
          SpriteHeader{.radius = std::numeric_limits<float>::quiet_NaN()}, SpriteHeader{.maxWidth = -1},
          SpriteHeader{.frames = 0}, SpriteHeader{.colors = 255}}) {
        EXPECT_EQ(parseError(spriteFile(header, singleFrame(2, 2))), SprError::BadValue);
    }
}

TEST(SpriteTest, RejectsABrokenFrame) {
    Bytes unknownType = singleFrame(2, 2);
    unknownType[0] = std::byte{2};
    EXPECT_EQ(parseError(spriteFile({}, unknownType)), SprError::BadValue);
    EXPECT_EQ(parseError(spriteFile({}, singleFrame(0, 5))), SprError::BadValue) << "an empty image";

    Bytes cut = singleFrame(4, 4);
    cut.pop_back();
    EXPECT_EQ(parseError(spriteFile({}, cut)), SprError::FrameOutOfBounds);
    EXPECT_EQ(parseError(spriteFile({.frames = 2}, singleFrame(2, 2))), SprError::FrameOutOfBounds)
        << "one frame of two";

    for (const auto intervals : {std::to_array({0.5F, 0.25F}), std::to_array({0.0F, 0.25F}),
                                 std::to_array({0.1F, std::numeric_limits<float>::infinity()})}) {
        EXPECT_EQ(parseError(spriteFile({}, groupFrame(intervals))), SprError::BadValue)
            << intervals[0] << " " << intervals[1];
    }
    Bytes emptyGroup;
    append(emptyGroup, std::int32_t{1});
    append(emptyGroup, std::int32_t{0});
    EXPECT_EQ(parseError(spriteFile({}, emptyGroup)), SprError::BadValue);

    Bytes cutGroup = groupFrame(std::to_array({0.1F, 0.2F}));
    cutGroup.resize(cutGroup.size() - 2);
    EXPECT_EQ(parseError(spriteFile({}, cutGroup)), SprError::FrameOutOfBounds);
}

} // namespace
