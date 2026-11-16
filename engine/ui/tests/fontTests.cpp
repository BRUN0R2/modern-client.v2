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

using nx::Pixels;
using nx::test::SquareFont;
using nx::ui::Font;
using nx::ui::FontDeviceHeight;
using nx::ui::FontError;
using nx::ui::FontMetrics;
using nx::ui::FontSmoothing;
using nx::ui::FontUnits;
using nx::ui::FontVerticalMetrics;

/// The heights of a face of an em `unitsPerEm` units big, `ascent` above the baseline and `descent` below.
[[nodiscard]] constexpr auto verticalOf(std::int32_t unitsPerEm, std::int32_t ascent,
                                        std::int32_t descent) noexcept -> FontVerticalMetrics {
    return {.unitsPerEm = FontUnits{unitsPerEm}, .ascent = FontUnits{ascent}, .descent = FontUnits{descent}};
}

/// A device height: at `pixelsPerEm`, the rows `top` above the baseline to `bottom`, negative below it.
[[nodiscard]] constexpr auto deviceHeightOf(std::int32_t pixelsPerEm, std::int32_t top,
                                            std::int32_t bottom) noexcept -> FontDeviceHeight {
    return {.pixelsPerEm = Pixels{pixelsPerEm}, .top = Pixels{top}, .bottom = Pixels{bottom}};
}

/// A face's size: `pixelsPerEm`, and a line of `ascent` over `descent` pixels.
[[nodiscard]] constexpr auto metricsOf(std::int32_t pixelsPerEm, std::int32_t ascent,
                                       std::int32_t descent) noexcept -> FontMetrics {
    return {.pixelsPerEm = Pixels{pixelsPerEm}, .ascent = Pixels{ascent}, .descent = Pixels{descent}};
}

/// Tahoma's heights: its VDMX table is a version 0 one for all its glyphs, which Windows ignores.
constexpr FontVerticalMetrics kTahoma = verticalOf(2048, 2049, 423);
/// Verdana's heights, and the records of its VDMX table from 8 to 21 pixels per em.
constexpr FontVerticalMetrics kVerdana = verticalOf(2048, 2059, 430);
constexpr std::array<FontDeviceHeight, 14> kVerdanaDeviceHeights{{
    deviceHeightOf(8, 8, -2),
    deviceHeightOf(9, 10, -2),
    deviceHeightOf(10, 10, -2),
    deviceHeightOf(11, 11, -2),
    deviceHeightOf(12, 12, -2),
    deviceHeightOf(13, 13, -3),
    deviceHeightOf(14, 14, -3),
    deviceHeightOf(15, 15, -3),
    deviceHeightOf(16, 15, -3),
    deviceHeightOf(17, 17, -3),
    deviceHeightOf(18, 18, -4),
    deviceHeightOf(19, 19, -4),
    deviceHeightOf(20, 21, -4),
    deviceHeightOf(21, 21, -4),
}};

TEST(FontSizeTest, ScaledLinesSizeAsWindowsDoes) {
    const auto sized = [](std::int32_t cellHeight) {
        return nx::ui::fontMetricsFor(kTahoma, {}, Pixels{cellHeight});
    };
    EXPECT_EQ(sized(16), metricsOf(13, 13, 3)) << "the schemes' Default";
    EXPECT_EQ(sized(13), metricsOf(11, 11, 2));
    EXPECT_EQ(sized(9), metricsOf(7, 7, 2));
    EXPECT_EQ(sized(20), metricsOf(16, 17, 3)) << "17 pixels per em would make a line of 21";
    EXPECT_EQ(sized(38), metricsOf(31, 31, 6)) << "a line one pixel short of the cell";
    EXPECT_EQ(sized(90), metricsOf(74, 75, 15));
}

TEST(FontSizeTest, DeviceHeightsSizeAsWindowsDoes) {
    const auto sized = [](std::int32_t cellHeight) {
        return nx::ui::fontMetricsFor(kVerdana, kVerdanaDeviceHeights, Pixels{cellHeight});
    };
    EXPECT_EQ(sized(12), metricsOf(9, 10, 2)) << "the first em whose line is exactly the cell";
    EXPECT_EQ(sized(18), metricsOf(15, 15, 3));
    EXPECT_EQ(sized(19), metricsOf(16, 15, 3)) << "none exactly 19: the largest em whose line fits";
    EXPECT_EQ(sized(20), metricsOf(17, 17, 3));
    EXPECT_EQ(sized(8), metricsOf(6, 7, 1)) << "below the table: the scaled lines";
    EXPECT_EQ(sized(9), metricsOf(7, 7, 2));
}

TEST(FontSizeTest, AnEmWithADeviceHeightIsSizedByIt) {
    // Trebuchet MS: its table starts at 8 pixels per em, whose line is already 13 pixels.
    constexpr FontVerticalMetrics kTrebuchet = verticalOf(2048, 1923, 455);
    constexpr std::array<FontDeviceHeight, 4> kDeviceHeights{
        {deviceHeightOf(8, 10, -3), deviceHeightOf(9, 11, -4), deviceHeightOf(10, 11, -4),
         deviceHeightOf(11, 12, -4)}};
    EXPECT_EQ(nx::ui::fontMetricsFor(kTrebuchet, kDeviceHeights, Pixels{12}), metricsOf(7, 10, 2))
        << "10 pixels per em would scale to a line of 12, but its device height is 15";
    EXPECT_EQ(nx::ui::fontMetricsFor(kTrebuchet, kDeviceHeights, Pixels{15}), metricsOf(9, 11, 4));
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
    const std::vector<FontDeviceHeight> sixteen{deviceHeightOf(16, 17, -3)};
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

TEST(FontSizeTest, ADeviceHeightWhoseLineIsLessThanHalfItsEmIsLeftOut) {
    // A version 1 table for every ratio, one group of the records `records` (yPelHeight, yMax, yMin).
    const auto table = [](std::span<const std::array<std::uint16_t, 3>> records) {
        std::vector<std::byte> bytes;
        const auto append = [&bytes](std::uint16_t value) {
            bytes.push_back(static_cast<std::byte>(value >> 8U));
            bytes.push_back(static_cast<std::byte>(value & 0xFFU));
        };
        for (const std::uint16_t value : {std::uint16_t{1}, std::uint16_t{1}, std::uint16_t{1}}) {
            append(value); // version, numRecs, numRatios
        }
        bytes.insert(bytes.end(), {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}}); // every ratio
        append(12); // the group's offset
        append(static_cast<std::uint16_t>(records.size()));
        bytes.insert(bytes.end(), {std::byte{0}, std::byte{0xFF}}); // startsz, endsz
        for (const std::array<std::uint16_t, 3>& record : records) {
            for (const std::uint16_t value : record) {
                append(value);
            }
        }
        return bytes;
    };
    // Lines 20 (17 over -3), 19 (10 over -9) and 21: the last two of ems of 38, 40 and 64768.
    constexpr std::uint16_t kMinusThree = 0xFFFD;
    constexpr std::uint16_t kMinusNine = 0xFFF7;
    const std::array records{std::array<std::uint16_t, 3>{16, 17, kMinusThree},
                             std::array<std::uint16_t, 3>{38, 10, kMinusNine},
                             std::array<std::uint16_t, 3>{40, 10, kMinusNine},
                             std::array<std::uint16_t, 3>{64768, 18, kMinusThree}};
    EXPECT_EQ(nx::ui::readDeviceHeights(table(records)),
              (std::vector<FontDeviceHeight>{deviceHeightOf(16, 17, -3), deviceHeightOf(38, 10, -9)}))
        << "a line of half its em is one, less is none: 40 pixels per em in 19, 64768 in 21";
}

/// The square font, shared by the fonts the tests open.
[[nodiscard]] auto squareFile(const nx::test::SquareFontDesc& desc = {})
    -> std::shared_ptr<const std::vector<std::byte>> {
    return std::make_shared<const std::vector<std::byte>>(SquareFont::build(desc));
}

/// The square font at 16 pixels per em, as a cell 20 pixels tall makes Tahoma.
[[nodiscard]] auto squareFont(FontSmoothing smoothing) -> Font {
    auto font = Font::open(squareFile(), {.cellHeight = Pixels{20}, .smoothing = smoothing});
    EXPECT_TRUE(font.has_value()) << (font ? "" : font.error().detail);
    return std::move(font).value();
}

TEST(FontTest, OpensAFaceSizedAsWindowsSizesIt) {
    const Font font = squareFont(FontSmoothing::None);
    EXPECT_EQ(font.family(), "Square");
    EXPECT_EQ(font.style(), "Regular");
    EXPECT_EQ(font.metrics(), metricsOf(16, 17, 3));
    EXPECT_EQ(font.metrics().height(), Pixels{20});
    EXPECT_EQ(font.smoothing(), FontSmoothing::None);

    auto byDevice = Font::open(squareFile({.deviceHeights = {{15, 17, -3}, {16, 18, -3}, {17, 18, -4}}}),
                               {.cellHeight = Pixels{20}});
    ASSERT_TRUE(byDevice.has_value()) << byDevice.error().detail;
    EXPECT_EQ(byDevice->metrics(), metricsOf(15, 17, 3)) << "its VDMX table, read from the file";
}

TEST(FontTest, DrawsGlyphsWithoutAntialiasing) {
    const Font font = squareFont(FontSmoothing::None);
    const auto square = font.glyph(U'A');
    ASSERT_TRUE(square.has_value()) << square.error().detail;
    EXPECT_EQ(square->left, Pixels{0});
    EXPECT_EQ(square->top, Pixels{8});
    EXPECT_EQ(square->width, Pixels{8});
    EXPECT_EQ(square->height, Pixels{8});
    EXPECT_EQ(square->advance, Pixels{10});
    EXPECT_TRUE(std::ranges::all_of(square->coverage, [](std::uint8_t value) { return value == 255; }));

    const auto shifted = font.glyph(U'B');
    ASSERT_TRUE(shifted.has_value()) << shifted.error().detail;
    EXPECT_EQ(shifted->left, Pixels{0});
    EXPECT_EQ(shifted->width, Pixels{8}) << "each pixel in or out by its centre: none in a ninth column";
    EXPECT_EQ(shifted->height, Pixels{8});
    EXPECT_TRUE(std::ranges::all_of(shifted->coverage, [](std::uint8_t value) { return value == 255; }));
}

TEST(FontTest, DrawsGlyphsWithAntialiasing) {
    const Font font = squareFont(FontSmoothing::Antialiased);
    const auto square = font.glyph(U'A');
    ASSERT_TRUE(square.has_value()) << square.error().detail;
    EXPECT_EQ(square->width, Pixels{8});
    EXPECT_TRUE(std::ranges::all_of(square->coverage, [](std::uint8_t value) { return value == 255; }));

    const auto shifted = font.glyph(U'B');
    ASSERT_TRUE(shifted.has_value()) << shifted.error().detail;
    ASSERT_EQ(shifted->width, Pixels{9});
    ASSERT_EQ(shifted->height, Pixels{8});
    for (std::size_t row = 0; row < 8; ++row) {
        EXPECT_NEAR(shifted->coverage[row * 9], 0.75 * 255, 1.0) << "three quarters covered";
        EXPECT_EQ(shifted->coverage[(row * 9) + 4], 255);
        EXPECT_NEAR(shifted->coverage[(row * 9) + 8], 0.25 * 255, 1.0) << "a quarter covered";
    }
}

TEST(FontTest, BoldIsSimulatedByStrikingEachGlyphTwice) {
    auto font =
        Font::open(squareFile(),
                   {.cellHeight = Pixels{20}, .smoothing = FontSmoothing::Antialiased, .isEmboldened = true});
    ASSERT_TRUE(font.has_value()) << font.error().detail;
    const auto shifted = font->glyph(U'B');
    ASSERT_TRUE(shifted.has_value()) << shifted.error().detail;
    ASSERT_EQ(shifted->width, Pixels{10}) << "a pixel wider";
    EXPECT_EQ(shifted->advance, Pixels{11});
    for (std::size_t row = 0; row < 8; ++row) {
        EXPECT_NEAR(shifted->coverage[row * 10], 0.75 * 255, 1.0) << "its left edge as it was";
        EXPECT_EQ(shifted->coverage[(row * 10) + 8], 255) << "the right edge struck over by the body";
        EXPECT_NEAR(shifted->coverage[(row * 10) + 9], 0.25 * 255, 1.0) << "the right edge a pixel on";
    }
    const auto space = font->glyph(U' ');
    ASSERT_TRUE(space.has_value()) << space.error().detail;
    EXPECT_EQ(space->advance, Pixels{5});
}

TEST(FontTest, SmoothedOnlyWhereTheGaspTableSaysSo) {
    // As Tahoma's: fitted but not smoothed up to 16 pixels per em, fitted and smoothed above.
    const auto file = squareFile({.gaspRanges = {{16, 0x0005}, {0xFFFF, 0x000F}}});
    auto sixteen = Font::open(file, {.cellHeight = Pixels{20}, .smoothing = FontSmoothing::Antialiased});
    ASSERT_TRUE(sixteen.has_value()) << sixteen.error().detail;
    EXPECT_EQ(sixteen->metrics().pixelsPerEm, Pixels{16});
    const auto aliased = sixteen->glyph(U'B');
    ASSERT_TRUE(aliased.has_value()) << aliased.error().detail;
    EXPECT_EQ(aliased->width, Pixels{8}) << "each pixel in or out";
    EXPECT_TRUE(std::ranges::all_of(aliased->coverage, [](std::uint8_t value) { return value == 255; }));

    auto eighteen = Font::open(file, {.cellHeight = Pixels{22}, .smoothing = FontSmoothing::Antialiased});
    ASSERT_TRUE(eighteen.has_value()) << eighteen.error().detail;
    EXPECT_EQ(eighteen->metrics().pixelsPerEm, Pixels{18});
    const auto smoothed = eighteen->glyph(U'B');
    ASSERT_TRUE(smoothed.has_value()) << smoothed.error().detail;
    ASSERT_GT(smoothed->width, Pixels{0});
    EXPECT_GT(smoothed->coverage.front(), 0);
    EXPECT_LT(smoothed->coverage.front(), 255) << "its first column partly covered";
}

TEST(FontTest, ABrokenDeviceHeightCannotSizeAFaceBeyondItsCell) {
    // A record of 30000 pixels per em in a line of 21 would have sized the face to it, its 'A' a
    // square of 15000 pixels a side; left out, the face is sized by its scaled line.
    const auto plain = Font::open(squareFile(), {.cellHeight = Pixels{28}});
    const auto broken =
        Font::open(squareFile({.deviceHeights = {{30000, 18, -3}}}), {.cellHeight = Pixels{28}});
    ASSERT_TRUE(plain.has_value()) << plain.error().detail;
    ASSERT_TRUE(broken.has_value()) << broken.error().detail;
    EXPECT_EQ(broken->metrics(), plain->metrics());
    const auto square = broken->glyph(U'A');
    ASSERT_TRUE(square.has_value()) << square.error().detail;
    EXPECT_LE(square->width, broken->metrics().pixelsPerEm);
}

TEST(FontTest, GlyphsOfManyEmsAreRefused) {
    // Squares of 32000 units, 15 ems: no letter, but a font made to exhaust whoever draws it.
    auto font = Font::open(squareFile({.side = 32000}), {.cellHeight = Pixels{20}});
    ASSERT_TRUE(font.has_value()) << font.error().detail;
    const auto giant = font->glyph(U'A');
    ASSERT_FALSE(giant.has_value());
    EXPECT_EQ(giant.error().code, FontError::BadGlyph);
    EXPECT_EQ(giant.error().detail, "U+0041: 250 by 250 pixels at 16 pixels per em");
}

TEST(FontTest, GlyphsWithoutOutlinesOnlyAdvance) {
    const auto space = squareFont(FontSmoothing::None).glyph(U' ');
    ASSERT_TRUE(space.has_value()) << space.error().detail;
    EXPECT_EQ(space->width, Pixels{0});
    EXPECT_EQ(space->height, Pixels{0});
    EXPECT_TRUE(space->coverage.empty());
    EXPECT_EQ(space->advance, Pixels{4});
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
    const auto notAFont = Font::open(std::make_shared<const std::vector<std::byte>>(16, std::byte{'x'}),
                                     {.cellHeight = Pixels{16}});
    ASSERT_FALSE(notAFont.has_value());
    EXPECT_EQ(notAFont.error().code, FontError::NotAFont);

    const auto noSuchFace = Font::open(squareFile(), {.cellHeight = Pixels{16}, .faceIndex = 1});
    ASSERT_FALSE(noSuchFace.has_value());
    EXPECT_EQ(noSuchFace.error().code, FontError::NotAFont);

    for (const Pixels cellHeight :
         {nx::ui::kMinCellHeight - Pixels{1}, Pixels{-16}, nx::ui::kMaxCellHeight + Pixels{1}}) {
        const auto badHeight = Font::open(squareFile(), {.cellHeight = cellHeight});
        ASSERT_FALSE(badHeight.has_value());
        EXPECT_EQ(badHeight.error().code, FontError::BadHeight) << cellHeight.value();
    }
    for (const Pixels cellHeight : {nx::ui::kMinCellHeight, nx::ui::kMaxCellHeight}) {
        EXPECT_TRUE(Font::open(squareFile(), {.cellHeight = cellHeight}).has_value()) << cellHeight.value();
    }
}

} // namespace
