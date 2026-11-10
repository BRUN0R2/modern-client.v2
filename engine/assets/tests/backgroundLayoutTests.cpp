// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Background layouts: the pictures of the original's menu background, read as the original writes them.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;

namespace {

using nx::assets::BackgroundLayoutError;
using nx::assets::BackgroundTile;
using nx::assets::parseBackgroundLayout;

[[nodiscard]] auto errorOf(std::string_view text) -> std::optional<BackgroundLayoutError> {
    const auto layout = parseBackgroundLayout(text);
    return layout ? std::nullopt : std::optional{layout.error().code};
}

TEST(BackgroundLayoutTest, ReadsTheScreenAndItsPicturesInTheirOrder) {
    const auto layout = parseBackgroundLayout("resolution\t800\t600\n"
                                              "\n"
                                              "resource/background/800_1_a_loading.tga\t\tfit\t\t0\t0\n"
                                              "resource/background/800_1_b_loading.tga\t\tfit\t\t256\t0\n"
                                              "// the last row\n"
                                              "\"resource/background/800 3 d.tga\" SCALED 768 512\n"
                                              "\n\n");
    ASSERT_TRUE(layout.has_value()) << (layout ? "" : nx::describe(layout.error()));
    EXPECT_EQ(layout->width, 800U);
    EXPECT_EQ(layout->height, 600U);
    const std::vector<BackgroundTile> expected{
        {.image = "resource/background/800_1_a_loading.tga", .x = 0, .y = 0},
        {.image = "resource/background/800_1_b_loading.tga", .x = 256, .y = 0},
        {.image = "resource/background/800 3 d.tga", .x = 768, .y = 512}};
    EXPECT_EQ(layout->tiles, expected);
}

TEST(BackgroundLayoutTest, AScreenWithoutPicturesIsALayout) {
    const auto layout = parseBackgroundLayout("Resolution 1600 1200");
    ASSERT_TRUE(layout.has_value());
    EXPECT_TRUE(layout->tiles.empty());
}

TEST(BackgroundLayoutTest, ALayoutThatDoesNotStartWithItsScreenIsRefused) {
    EXPECT_EQ(errorOf(""), BackgroundLayoutError::NoResolution);
    EXPECT_EQ(errorOf("a.tga fit 0 0"), BackgroundLayoutError::NoResolution);
    EXPECT_EQ(errorOf("resolution 800"), BackgroundLayoutError::NoResolution);
    EXPECT_EQ(errorOf("resolution 800 tall"), BackgroundLayoutError::NoResolution);
    EXPECT_EQ(errorOf("resolution -800 600"), BackgroundLayoutError::NoResolution);
}

TEST(BackgroundLayoutTest, APictureCutShortOrMisplacedIsRefused) {
    EXPECT_EQ(errorOf("resolution 800 600\na.tga"), BackgroundLayoutError::BadTile);
    EXPECT_EQ(errorOf("resolution 800 600\na.tga fit 0"), BackgroundLayoutError::BadTile);
    EXPECT_EQ(errorOf("resolution 800 600\na.tga stretched 0 0"), BackgroundLayoutError::BadTile);
    EXPECT_EQ(errorOf("resolution 800 600\na.tga fit left 0"), BackgroundLayoutError::BadTile);
    EXPECT_EQ(errorOf("resolution 800 600\n\"\" fit 0 0"), BackgroundLayoutError::BadTile);
}

TEST(BackgroundLayoutTest, WhatAsksForTooMuchIsRefused) {
    EXPECT_EQ(errorOf("resolution 0 600"), BackgroundLayoutError::TooLarge);
    EXPECT_EQ(errorOf("resolution 4097 600"), BackgroundLayoutError::TooLarge);
    EXPECT_EQ(errorOf("resolution 800 4294967295"), BackgroundLayoutError::TooLarge);
    EXPECT_EQ(errorOf("resolution 4096 4096"), std::nullopt) << "the ceiling is inclusive";
    EXPECT_EQ(errorOf("resolution 800 600\na.tga fit 800 0"), BackgroundLayoutError::TooLarge)
        << "a picture that starts off the screen";
    EXPECT_EQ(errorOf("resolution 800 600\na.tga fit 799 599"), std::nullopt);
}

} // namespace
