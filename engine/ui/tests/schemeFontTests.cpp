// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// A scheme's fonts made for a screen: the candidates for its height, the first that opens drawing
// the characters its range holds, those after it what is left; and an atlas of such a font.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;
import nx.ui;

#include "squareFont.hpp"
#include "temporaryDirectory.hpp"

namespace {

using nx::assets::FontCandidate;
using nx::assets::FontFlag;
using nx::assets::SchemeFont;
using nx::assets::SchemeRange;
using nx::ui::FontCatalog;
using nx::ui::FontError;
using nx::ui::SchemeFontSet;

/// A catalog of two families, Square and Mark: the same squares, Mark's 500 heavy.
class SchemeFontTest : public testing::Test {
protected:
    SchemeFontTest() {
        for (const auto& [name, desc] :
             {std::pair{"square.ttf", nx::test::SquareFontDesc{}},
              std::pair{"mark.ttf",
                        nx::test::SquareFontDesc{.family = "Mark", .style = "Regular", .weight = 500}}}) {
            directory.write(name,
                            nx::test::SquareFont::build(desc) | std::views::transform([](std::byte value) {
                                return static_cast<char>(value);
                            }) | std::ranges::to<std::string>());
        }
        std::ignore = catalog.addFolder(directory.path());
    }

    nx::test::TemporaryDirectory directory;
    FontCatalog catalog;
};

TEST_F(SchemeFontTest, ACharacterIsDrawnByTheFirstCandidateWhoseRangeHoldsIt) {
    const SchemeFont font{
        .name = "Default",
        .candidates = {
            FontCandidate{.face = "Missing", .tall = 16},
            FontCandidate{.face = "Square", .tall = 20, .characters = SchemeRange<char32_t>{U'A', U'A'}},
            FontCandidate{.face = "Mark", .tall = 9}, FontCandidate{.face = "Square", .tall = 30}}};
    auto set = SchemeFontSet::open(font, 768, catalog);
    ASSERT_TRUE(set.has_value()) << nx::describe(set.error());
    EXPECT_EQ(set->metrics().height(), 20) << "the first that opened";
    ASSERT_NE(set->fontFor(U'A'), nullptr);
    EXPECT_EQ(set->fontFor(U'A')->family(), "Square");
    ASSERT_NE(set->fontFor(U'B'), nullptr);
    EXPECT_EQ(set->fontFor(U'B')->family(), "Mark") << "the range the first leaves";
    EXPECT_EQ(set->fontFor(U'B')->metrics().height(), 9);

    const auto glyph = set->glyph(U'B');
    ASSERT_TRUE(glyph.has_value()) << glyph.error().detail;
    EXPECT_EQ(glyph->advance, 4) << "Mark's square at 7 pixels per em: 1280 units are 4.4 pixels";
}

TEST_F(SchemeFontTest, OnlyTheCandidatesForTheScreensHeightCount) {
    const SchemeFont font{
        .name = "Title",
        .candidates = {
            FontCandidate{.face = "Square", .tall = 13, .heights = SchemeRange<std::uint32_t>{480, 599}},
            FontCandidate{.face = "Square",
                          .tall = 20,
                          .flags = {FontFlag::Antialias},
                          .heights = SchemeRange<std::uint32_t>{600, 767}}}};
    const auto small = SchemeFontSet::open(font, 480, catalog);
    ASSERT_TRUE(small.has_value()) << nx::describe(small.error());
    EXPECT_EQ(small->metrics().height(), 13);
    const auto large = SchemeFontSet::open(font, 600, catalog);
    ASSERT_TRUE(large.has_value()) << nx::describe(large.error());
    EXPECT_EQ(large->metrics().height(), 20);
    EXPECT_EQ(large->fontFor(U'A')->smoothing(), nx::ui::FontSmoothing::Antialiased);

    const auto none = SchemeFontSet::open(font, 1080, catalog);
    ASSERT_FALSE(none.has_value());
    EXPECT_EQ(none.error().code, FontError::NoFace);
}

TEST_F(SchemeFontTest, AnAtlasOfASchemesFont) {
    const SchemeFont font{
        .name = "Default",
        .candidates = {
            FontCandidate{.face = "Square", .tall = 20, .characters = SchemeRange<char32_t>{U'A', U'A'}},
            FontCandidate{.face = "Mark", .tall = 9}}};
    auto set = SchemeFontSet::open(font, 768, catalog);
    ASSERT_TRUE(set.has_value()) << nx::describe(set.error());
    const auto atlas = nx::ui::GlyphAtlas::build(*set, U"AB");
    ASSERT_TRUE(atlas.has_value()) << atlas.error().detail;
    EXPECT_EQ(atlas->metrics().height(), 20);
    EXPECT_EQ(atlas->glyph(U'A')->advance, 10) << "Square's";
    EXPECT_EQ(atlas->glyph(U'B')->advance, 4) << "Mark's";
}

TEST_F(SchemeFontTest, TheCharactersOfACandidateWithADropShadowCastIt) {
    const SchemeFont font{.name = "EngineFont",
                          .candidates = {FontCandidate{.face = "Square",
                                                       .tall = 20,
                                                       .flags = {FontFlag::DropShadow},
                                                       .characters = SchemeRange<char32_t>{U'A', U'A'}},
                                         FontCandidate{.face = "Mark", .tall = 9}}};
    auto set = SchemeFontSet::open(font, 768, catalog);
    ASSERT_TRUE(set.has_value()) << nx::describe(set.error());
    EXPECT_TRUE(set->hasDropShadow(U'A'));
    EXPECT_FALSE(set->hasDropShadow(U'B')) << "Mark's, which casts none";
    EXPECT_FALSE(set->hasDropShadow(U'\x7F')) << "Mark's too: it has no range";

    const auto atlas = nx::ui::GlyphAtlas::build(*set, U"AB");
    ASSERT_TRUE(atlas.has_value()) << atlas.error().detail;
    EXPECT_TRUE(atlas->glyph(U'A')->hasDropShadow);
    EXPECT_FALSE(atlas->glyph(U'B')->hasDropShadow);
    const std::vector<nx::ui::PlacedGlyph> placed = nx::ui::layoutText(*atlas, "AB", 0.0F, 0.0F);
    ASSERT_EQ(placed.size(), 2U);
    EXPECT_TRUE(placed[0].hasDropShadow);
    EXPECT_FALSE(placed[1].hasDropShadow);
}

} // namespace
