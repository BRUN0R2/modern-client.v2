// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The picture behind the main menu: the pictures a background layout places put together into one, read
// from the game's files, and where it goes to cover a screen of any shape.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.rhi;
import nx.assets;
import nx.render;
import cs.client;

#include "temporaryDirectory.hpp"

namespace {

using nx::assets::RgbaImage;

/// A picture of `width` by `height` pixels, each `red` and opaque.
[[nodiscard]] auto pictureOf(std::uint32_t width, std::uint32_t height, std::uint8_t red) -> RgbaImage {
    RgbaImage picture{.width = width, .height = height};
    for (std::uint32_t pixel = 0; pixel < width * height; ++pixel) {
        picture.pixels.insert(picture.pixels.end(),
                              {std::byte{red}, std::byte{0}, std::byte{0}, std::byte{255}});
    }
    return picture;
}

/// How red the pixel at `x`, `y` of `picture` is.
[[nodiscard]] auto redAt(const RgbaImage& picture, std::uint32_t x, std::uint32_t y) -> std::uint8_t {
    return std::to_integer<std::uint8_t>(
        picture.pixels[((std::size_t{y} * picture.width) + x) * nx::assets::kRgbaBytes]);
}

/// A true-color TGA file of `width` by `height` pixels from the top left, each `red` and opaque.
[[nodiscard]] auto tgaOf(std::uint16_t width, std::uint16_t height, std::uint8_t red) -> std::string {
    constexpr char kTrueColor = 2;
    constexpr char kBitsAPixel = 24;
    constexpr char kFromTheTop = 0x20;
    std::string file{'\0', '\0', kTrueColor, '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'};
    for (const std::uint16_t side : {width, height}) {
        file.push_back(static_cast<char>(side & 0xFFU));
        file.push_back(static_cast<char>(side >> 8U));
    }
    file.push_back(kBitsAPixel);
    file.push_back(kFromTheTop);
    for (std::uint32_t pixel = 0; pixel < std::uint32_t{width} * height; ++pixel) {
        file.append({'\0', '\0', static_cast<char>(red)}); // blue, green, red
    }
    return file;
}

TEST(MenuBackgroundTest, ThePicturesArePutTogetherWhereTheLayoutPlacesThem) {
    const nx::assets::BackgroundLayout layout{
        .width = 6,
        .height = 4,
        .tiles = {{.image = "a.tga", .x = 0, .y = 0}, {.image = "b.tga", .x = 4, .y = 2}}};
    const std::array tiles{pictureOf(4, 2, 100), pictureOf(4, 4, 200)};
    const RgbaImage whole = cs::composeBackground(layout, tiles);
    ASSERT_EQ(whole.width, 6U);
    ASSERT_EQ(whole.height, 4U);
    ASSERT_EQ(whole.pixels.size(), 6U * 4U * nx::assets::kRgbaBytes);
    EXPECT_EQ(redAt(whole, 0, 0), 100);
    EXPECT_EQ(redAt(whole, 3, 1), 100);
    EXPECT_EQ(redAt(whole, 4, 1), 0) << "what no picture covers is black";
    EXPECT_EQ(redAt(whole, 0, 2), 0);
    EXPECT_EQ(redAt(whole, 4, 2), 200);
    EXPECT_EQ(redAt(whole, 5, 3), 200) << "what reaches past the screen is left out";
    EXPECT_EQ(std::to_integer<int>(whole.pixels[nx::assets::kRgbaBytes - 1]), 255) << "opaque";
}

TEST(MenuBackgroundTest, ALaterPictureGoesOverAnEarlierOne) {
    const nx::assets::BackgroundLayout layout{
        .width = 2,
        .height = 2,
        .tiles = {{.image = "a.tga", .x = 0, .y = 0}, {.image = "b.tga", .x = 1, .y = 1}}};
    const std::array tiles{pictureOf(2, 2, 100), pictureOf(1, 1, 200)};
    const RgbaImage whole = cs::composeBackground(layout, tiles);
    EXPECT_EQ(redAt(whole, 0, 0), 100);
    EXPECT_EQ(redAt(whole, 1, 1), 200);
}

TEST(MenuBackgroundTest, IsReadFromTheGamesFiles) {
    const nx::test::TemporaryDirectory files;
    files.write("resource/BackgroundLayout.txt", "resolution 4 2\n"
                                                 "resource/background/a.tga fit 0 0\n"
                                                 "resource\\background\\b.tga fit 2 0\n");
    files.write("resource/background/a.tga", tgaOf(2, 2, 100));
    files.write("resource/background/b.tga", tgaOf(2, 2, 200));
    const auto vfs = nx::assets::Vfs::create(std::array{files.path()});
    ASSERT_TRUE(vfs.has_value());
    const auto background = cs::readMenuBackground(*vfs, "resource/BackgroundLayout.txt");
    ASSERT_TRUE(background.has_value()) << (background ? "" : nx::describe(background.error()));
    EXPECT_EQ(background->width, 4U);
    EXPECT_EQ(background->height, 2U);
    EXPECT_EQ(redAt(*background, 1, 1), 100);
    EXPECT_EQ(redAt(*background, 2, 0), 200) << "a path written with backslashes, as the original's may be";
}

TEST(MenuBackgroundTest, WhatCannotBeReadSaysWhy) {
    const nx::test::TemporaryDirectory files;
    files.write("resource/missing.txt", "resolution 4 2\nresource/background/none.tga fit 0 0\n");
    files.write("resource/broken.txt", "resolution 4 2\nresource/background/a.tga fit 9 0\n");
    files.write("resource/notATga.txt", "resolution 4 2\nresource/background/a.tga fit 0 0\n");
    files.write("resource/background/a.tga", "no picture");
    const auto vfs = nx::assets::Vfs::create(std::array{files.path()});
    ASSERT_TRUE(vfs.has_value());
    const auto errorOf = [&](std::string_view layout) -> std::optional<cs::MenuBackgroundError> {
        const auto background = cs::readMenuBackground(*vfs, layout);
        return background ? std::nullopt : std::optional{background.error().code};
    };
    EXPECT_EQ(errorOf("resource/none.txt"), cs::MenuBackgroundError::Unreadable);
    EXPECT_EQ(errorOf("resource/missing.txt"), cs::MenuBackgroundError::Unreadable);
    EXPECT_EQ(errorOf("resource/broken.txt"), cs::MenuBackgroundError::Invalid);
    EXPECT_EQ(errorOf("resource/notATga.txt"), cs::MenuBackgroundError::Invalid);
    EXPECT_EQ(errorOf("../outside.txt"), cs::MenuBackgroundError::Invalid);
}

TEST(MenuBackgroundTest, CoversAScreenOfAnyShapeWithoutChangingItsOwn) {
    constexpr nx::rhi::Extent2d kPicture{.width = 800, .height = 600};
    const nx::render::ScreenRect same = cs::coveringRect(kPicture, {.width = 1600, .height = 1200});
    EXPECT_FLOAT_EQ(same.x, 0.0F);
    EXPECT_FLOAT_EQ(same.y, 0.0F);
    EXPECT_FLOAT_EQ(same.width, 1600.0F);
    EXPECT_FLOAT_EQ(same.height, 1200.0F);

    const nx::render::ScreenRect wide = cs::coveringRect(kPicture, {.width = 1920, .height = 1080});
    EXPECT_FLOAT_EQ(wide.width, 1920.0F);
    EXPECT_FLOAT_EQ(wide.height, 1440.0F) << "its own shape kept";
    EXPECT_FLOAT_EQ(wide.x, 0.0F);
    EXPECT_FLOAT_EQ(wide.y, -180.0F) << "what is too tall left off the top and the foot alike";

    const nx::render::ScreenRect tall = cs::coveringRect(kPicture, {.width = 600, .height = 1200});
    EXPECT_FLOAT_EQ(tall.height, 1200.0F);
    EXPECT_FLOAT_EQ(tall.width, 1600.0F);
    EXPECT_FLOAT_EQ(tall.x, -500.0F);

    const nx::render::ScreenRect none = cs::coveringRect({}, {.width = 600, .height = 1200});
    EXPECT_FLOAT_EQ(none.width, 0.0F) << "a picture of no size covers nothing";
}

} // namespace
