// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Fonts: sized as Windows' GDI sizes them for a cell height (every expectation below is what GDI
// gave for Tahoma or Verdana), and their glyphs drawn with and without antialiasing.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.ui;

#include "squareFont.hpp"

namespace {

using nx::test::SquareFont;
using nx::ui::Font;
using nx::ui::FontDeviceHeight;
using nx::ui::FontError;
using nx::ui::FontMetrics;
using nx::ui::FontSmoothing;
using nx::ui::FontVerticalMetrics;

/// Tahoma's heights: its VDMX table is a version 0 one for all its glyphs, which Windows ignores.
constexpr FontVerticalMetrics kTahoma{.unitsPerEm = 2048, .ascent = 2049, .descent = 423};
/// Verdana's heights, and the records of its VDMX table from 8 to 21 pixels per em.
constexpr FontVerticalMetrics kVerdana{.unitsPerEm = 2048, .ascent = 2059, .descent = 430};
constexpr std::array<FontDeviceHeight, 14> kVerdanaDeviceHeights{{
    {.pixelsPerEm = 8, .top = 8, .bottom = -2},
    {.pixelsPerEm = 9, .top = 10, .bottom = -2},
    {.pixelsPerEm = 10, .top = 10, .bottom = -2},
    {.pixelsPerEm = 11, .top = 11, .bottom = -2},
    {.pixelsPerEm = 12, .top = 12, .bottom = -2},
    {.pixelsPerEm = 13, .top = 13, .bottom = -3},
    {.pixelsPerEm = 14, .top = 14, .bottom = -3},
    {.pixelsPerEm = 15, .top = 15, .bottom = -3},
    {.pixelsPerEm = 16, .top = 15, .bottom = -3},
    {.pixelsPerEm = 17, .top = 17, .bottom = -3},
    {.pixelsPerEm = 18, .top = 18, .bottom = -4},
    {.pixelsPerEm = 19, .top = 19, .bottom = -4},
    {.pixelsPerEm = 20, .top = 21, .bottom = -4},
    {.pixelsPerEm = 21, .top = 21, .bottom = -4},
}};

TEST(FontSizeTest, ScaledLinesSizeAsWindowsDoes) {
    const auto sized = [](std::int32_t cellHeight) {
        return nx::ui::fontMetricsFor(kTahoma, {}, cellHeight);
    };
    EXPECT_EQ(sized(16), (FontMetrics{.pixelsPerEm = 13, .ascent = 13, .descent = 3}))
        << "the schemes' Default";
    EXPECT_EQ(sized(13), (FontMetrics{.pixelsPerEm = 11, .ascent = 11, .descent = 2}));
    EXPECT_EQ(sized(9), (FontMetrics{.pixelsPerEm = 7, .ascent = 7, .descent = 2}));
    EXPECT_EQ(sized(20), (FontMetrics{.pixelsPerEm = 16, .ascent = 17, .descent = 3}))
        << "17 pixels per em would make a line of 21";
    EXPECT_EQ(sized(38), (FontMetrics{.pixelsPerEm = 31, .ascent = 31, .descent = 6}))
        << "a line one pixel short of the cell";
    EXPECT_EQ(sized(90), (FontMetrics{.pixelsPerEm = 74, .ascent = 75, .descent = 15}));
}

TEST(FontSizeTest, DeviceHeightsSizeAsWindowsDoes) {
    const auto sized = [](std::int32_t cellHeight) {
        return nx::ui::fontMetricsFor(kVerdana, kVerdanaDeviceHeights, cellHeight);
    };
    EXPECT_EQ(sized(12), (FontMetrics{.pixelsPerEm = 9, .ascent = 10, .descent = 2}))
        << "the first em whose line is exactly the cell";
    EXPECT_EQ(sized(18), (FontMetrics{.pixelsPerEm = 15, .ascent = 15, .descent = 3}));
    EXPECT_EQ(sized(19), (FontMetrics{.pixelsPerEm = 16, .ascent = 15, .descent = 3}))
        << "none exactly 19: the largest em whose line fits";
    EXPECT_EQ(sized(20), (FontMetrics{.pixelsPerEm = 17, .ascent = 17, .descent = 3}));
    EXPECT_EQ(sized(8), (FontMetrics{.pixelsPerEm = 6, .ascent = 7, .descent = 1}))
        << "below the table: the scaled lines";
    EXPECT_EQ(sized(9), (FontMetrics{.pixelsPerEm = 7, .ascent = 7, .descent = 2}));
}

TEST(FontSizeTest, AnEmWithADeviceHeightIsSizedByIt) {
    // Trebuchet MS: its table starts at 8 pixels per em, whose line is already 13 pixels.
    constexpr FontVerticalMetrics kTrebuchet{.unitsPerEm = 2048, .ascent = 1923, .descent = 455};
    constexpr std::array<FontDeviceHeight, 4> kDeviceHeights{{{.pixelsPerEm = 8, .top = 10, .bottom = -3},
                                                              {.pixelsPerEm = 9, .top = 11, .bottom = -4},
                                                              {.pixelsPerEm = 10, .top = 11, .bottom = -4},
                                                              {.pixelsPerEm = 11, .top = 12, .bottom = -4}}};
    EXPECT_EQ(nx::ui::fontMetricsFor(kTrebuchet, kDeviceHeights, 12),
              (FontMetrics{.pixelsPerEm = 7, .ascent = 10, .descent = 2}))
        << "10 pixels per em would scale to a line of 12, but its device height is 15";
    EXPECT_EQ(nx::ui::fontMetricsFor(kTrebuchet, kDeviceHeights, 15),
              (FontMetrics{.pixelsPerEm = 9, .ascent = 11, .descent = 4}));
}

TEST(FontSizeTest, ReadsTheDeviceHeightsWindowsTakes) {
    const auto table = [](std::uint16_t version, std::uint8_t characters, std::array<std::uint8_t, 3> ratio) {
        std::vector<std::byte> bytes;
        for (const std::uint8_t value : {std::uint8_t{0},    static_cast<std::uint8_t>(version),
                                         std::uint8_t{0},    std::uint8_t{1},
                                         std::uint8_t{0},    std::uint8_t{1},
                                         characters,         ratio[0],
                                         ratio[1],           ratio[2],
                                         std::uint8_t{0},    std::uint8_t{12},
                                         std::uint8_t{0},    std::uint8_t{1},
                                         std::uint8_t{16},   std::uint8_t{16},
                                         std::uint8_t{0},    std::uint8_t{16},
                                         std::uint8_t{0},    std::uint8_t{17},
                                         std::uint8_t{0xFF}, std::uint8_t{0xFD}}) {
            bytes.push_back(std::byte{value});
        }
        return bytes;
    };
    const std::vector<FontDeviceHeight> sixteen{{.pixelsPerEm = 16, .top = 17, .bottom = -3}};
    EXPECT_EQ(nx::ui::readDeviceHeights(table(1, 1, {1, 1, 1})), sixteen);
    EXPECT_EQ(nx::ui::readDeviceHeights(table(1, 0, {2, 1, 2})), sixteen) << "1:1 through 2:1";
    EXPECT_EQ(nx::ui::readDeviceHeights(table(0, 1, {0, 0, 0})), sixteen) << "the group for every ratio";
    EXPECT_TRUE(nx::ui::readDeviceHeights(table(0, 0, {1, 1, 1})).empty())
        << "a version 0 group for all the glyphs, which Windows ignores (Tahoma's)";
    EXPECT_TRUE(nx::ui::readDeviceHeights(table(1, 1, {4, 3, 3})).empty()) << "4:3 only";
    auto cut = table(1, 1, {1, 1, 1});
    cut.pop_back();
    EXPECT_TRUE(nx::ui::readDeviceHeights(cut).empty());
    EXPECT_TRUE(nx::ui::readDeviceHeights({}).empty());
}

/// The square font, shared by the fonts the tests open.
[[nodiscard]] auto squareFile(const nx::test::SquareFontDesc& desc = {})
    -> std::shared_ptr<const std::vector<std::byte>> {
    return std::make_shared<const std::vector<std::byte>>(SquareFont::build(desc));
}

/// The square font at 16 pixels per em, as a cell 20 pixels tall makes Tahoma.
[[nodiscard]] auto squareFont(FontSmoothing smoothing) -> Font {
    auto font = Font::open(squareFile(), {.cellHeight = 20, .smoothing = smoothing});
    EXPECT_TRUE(font.has_value()) << (font ? "" : font.error().detail);
    return std::move(font).value();
}

TEST(FontTest, OpensAFaceSizedAsWindowsSizesIt) {
    const Font font = squareFont(FontSmoothing::None);
    EXPECT_EQ(font.family(), "Square");
    EXPECT_EQ(font.style(), "Regular");
    EXPECT_EQ(font.metrics(), (FontMetrics{.pixelsPerEm = 16, .ascent = 17, .descent = 3}));
    EXPECT_EQ(font.metrics().height(), 20);
    EXPECT_EQ(font.smoothing(), FontSmoothing::None);

    auto byDevice = Font::open(squareFile({.deviceHeights = {{15, 17, -3}, {16, 18, -3}, {17, 18, -4}}}),
                               {.cellHeight = 20});
    ASSERT_TRUE(byDevice.has_value()) << byDevice.error().detail;
    EXPECT_EQ(byDevice->metrics(), (FontMetrics{.pixelsPerEm = 15, .ascent = 17, .descent = 3}))
        << "its VDMX table, read from the file";
}

TEST(FontTest, DrawsGlyphsWithoutAntialiasing) {
    const Font font = squareFont(FontSmoothing::None);
    const auto square = font.glyph(U'A');
    ASSERT_TRUE(square.has_value()) << square.error().detail;
    EXPECT_EQ(square->left, 0);
    EXPECT_EQ(square->top, 8);
    EXPECT_EQ(square->width, 8);
    EXPECT_EQ(square->height, 8);
    EXPECT_EQ(square->advance, 10);
    EXPECT_TRUE(std::ranges::all_of(square->coverage, [](std::uint8_t value) { return value == 255; }));

    const auto shifted = font.glyph(U'B');
    ASSERT_TRUE(shifted.has_value()) << shifted.error().detail;
    EXPECT_EQ(shifted->left, 0);
    EXPECT_EQ(shifted->width, 8) << "each pixel in or out by its centre: none in a ninth column";
    EXPECT_EQ(shifted->height, 8);
    EXPECT_TRUE(std::ranges::all_of(shifted->coverage, [](std::uint8_t value) { return value == 255; }));
}

TEST(FontTest, DrawsGlyphsWithAntialiasing) {
    const Font font = squareFont(FontSmoothing::Antialiased);
    const auto square = font.glyph(U'A');
    ASSERT_TRUE(square.has_value()) << square.error().detail;
    EXPECT_EQ(square->width, 8);
    EXPECT_TRUE(std::ranges::all_of(square->coverage, [](std::uint8_t value) { return value == 255; }));

    const auto shifted = font.glyph(U'B');
    ASSERT_TRUE(shifted.has_value()) << shifted.error().detail;
    ASSERT_EQ(shifted->width, 9);
    ASSERT_EQ(shifted->height, 8);
    for (std::size_t row = 0; row < 8; ++row) {
        EXPECT_NEAR(shifted->coverage[row * 9], 0.75 * 255, 1.0) << "three quarters covered";
        EXPECT_EQ(shifted->coverage[(row * 9) + 4], 255);
        EXPECT_NEAR(shifted->coverage[(row * 9) + 8], 0.25 * 255, 1.0) << "a quarter covered";
    }
}

TEST(FontTest, BoldIsSimulatedByStrikingEachGlyphTwice) {
    auto font = Font::open(squareFile(),
                           {.cellHeight = 20, .smoothing = FontSmoothing::Antialiased, .isEmboldened = true});
    ASSERT_TRUE(font.has_value()) << font.error().detail;
    const auto shifted = font->glyph(U'B');
    ASSERT_TRUE(shifted.has_value()) << shifted.error().detail;
    ASSERT_EQ(shifted->width, 10) << "a pixel wider";
    EXPECT_EQ(shifted->advance, 11);
    for (std::size_t row = 0; row < 8; ++row) {
        EXPECT_NEAR(shifted->coverage[row * 10], 0.75 * 255, 1.0) << "its left edge as it was";
        EXPECT_EQ(shifted->coverage[(row * 10) + 8], 255) << "the right edge struck over by the body";
        EXPECT_NEAR(shifted->coverage[(row * 10) + 9], 0.25 * 255, 1.0) << "the right edge a pixel on";
    }
    const auto space = font->glyph(U' ');
    ASSERT_TRUE(space.has_value()) << space.error().detail;
    EXPECT_EQ(space->advance, 5);
}

TEST(FontTest, SmoothedOnlyWhereTheGaspTableSaysSo) {
    // As Tahoma's: fitted but not smoothed up to 16 pixels per em, fitted and smoothed above.
    const auto file = squareFile({.gaspRanges = {{16, 0x0005}, {0xFFFF, 0x000F}}});
    auto sixteen = Font::open(file, {.cellHeight = 20, .smoothing = FontSmoothing::Antialiased});
    ASSERT_TRUE(sixteen.has_value()) << sixteen.error().detail;
    EXPECT_EQ(sixteen->metrics().pixelsPerEm, 16);
    const auto aliased = sixteen->glyph(U'B');
    ASSERT_TRUE(aliased.has_value()) << aliased.error().detail;
    EXPECT_EQ(aliased->width, 8) << "each pixel in or out";
    EXPECT_TRUE(std::ranges::all_of(aliased->coverage, [](std::uint8_t value) { return value == 255; }));

    auto eighteen = Font::open(file, {.cellHeight = 22, .smoothing = FontSmoothing::Antialiased});
    ASSERT_TRUE(eighteen.has_value()) << eighteen.error().detail;
    EXPECT_EQ(eighteen->metrics().pixelsPerEm, 18);
    const auto smoothed = eighteen->glyph(U'B');
    ASSERT_TRUE(smoothed.has_value()) << smoothed.error().detail;
    ASSERT_GT(smoothed->width, 0);
    EXPECT_GT(smoothed->coverage.front(), 0);
    EXPECT_LT(smoothed->coverage.front(), 255) << "its first column partly covered";
}

TEST(FontTest, GlyphsOfManyEmsAreRefused) {
    // Squares of 32000 units, 15 ems: no letter, but a font made to exhaust whoever draws it.
    auto font = Font::open(squareFile({.side = 32000}), {.cellHeight = 20});
    ASSERT_TRUE(font.has_value()) << font.error().detail;
    const auto giant = font->glyph(U'A');
    ASSERT_FALSE(giant.has_value());
    EXPECT_EQ(giant.error().code, FontError::BadGlyph);
    EXPECT_EQ(giant.error().detail, "U+0041: 250 by 250 pixels at 16 pixels per em");
}

TEST(FontTest, GlyphsWithoutOutlinesOnlyAdvance) {
    const auto space = squareFont(FontSmoothing::None).glyph(U' ');
    ASSERT_TRUE(space.has_value()) << space.error().detail;
    EXPECT_EQ(space->width, 0);
    EXPECT_EQ(space->height, 0);
    EXPECT_TRUE(space->coverage.empty());
    EXPECT_EQ(space->advance, 4);
}

TEST(FontTest, CharactersWithoutGlyphsAreRefused) {
    const auto missing = squareFont(FontSmoothing::None).glyph(U'Z');
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, FontError::NoGlyph);
    EXPECT_EQ(missing.error().detail, "U+005A");
}

TEST(FontTest, AMovedFontStillDraws) {
    Font font = squareFont(FontSmoothing::None);
    const Font moved = std::move(font);
    EXPECT_TRUE(moved.glyph(U'A').has_value());
}

TEST(FontTest, RefusesWhatItCannotOpen) {
    const auto notAFont =
        Font::open(std::make_shared<const std::vector<std::byte>>(16, std::byte{'x'}), {.cellHeight = 16});
    ASSERT_FALSE(notAFont.has_value());
    EXPECT_EQ(notAFont.error().code, FontError::NotAFont);

    const auto noSuchFace = Font::open(squareFile(), {.cellHeight = 16, .faceIndex = 1});
    ASSERT_FALSE(noSuchFace.has_value());
    EXPECT_EQ(noSuchFace.error().code, FontError::NotAFont);

    for (const std::int32_t cellHeight : {0, -16, nx::ui::kMaxCellHeight + 1}) {
        const auto badHeight = Font::open(squareFile(), {.cellHeight = cellHeight});
        ASSERT_FALSE(badHeight.has_value());
        EXPECT_EQ(badHeight.error().code, FontError::BadHeight) << cellHeight;
    }
}

} // namespace
