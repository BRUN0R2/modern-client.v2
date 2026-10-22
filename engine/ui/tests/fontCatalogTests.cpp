// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The font catalog: faces described by their tables, and the face a font gets chosen as GDI chooses
// it (every weight below is one where GDI was seen to change its mind), bold simulated as GDI
// simulates it.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.ui;

#include "squareFont.hpp"
#include "temporaryDirectory.hpp"

namespace {

using nx::test::SquareFont;
using nx::test::SquareFontDesc;
using nx::ui::FontCatalog;
using nx::ui::FontError;
using nx::ui::FontFace;

const SquareFontDesc kRegular{};
const SquareFontDesc kBold{.style = "Bold", .weight = 700};
const SquareFontDesc kItalic{.style = "Italic", .isItalic = true};

/// A folder of font files: Square in regular, bold and italic, and Mark, of one face of weight 500 as
/// Marlett; with a text file and a file that is no font.
class CatalogTest : public testing::Test {
protected:
    CatalogTest() {
        write("regular.ttf", SquareFont::build(kRegular));
        write("fonts/bold.TTF", SquareFont::build(kBold));
        write("fonts/italic.ttf", SquareFont::build(kItalic));
        write("mark.ttf", SquareFont::build({.family = "Mark", .style = "Regular", .weight = 500}));
        directory.write("readme.txt", "no font");
        directory.write("broken.ttf", "a font cut short");
    }

    void write(std::string_view name, const std::vector<std::byte>& bytes) const {
        directory.write(name, bytes | std::views::transform([](std::byte value) {
                                  return static_cast<char>(value);
                              }) | std::ranges::to<std::string>());
    }

    nx::test::TemporaryDirectory directory;
};

TEST(FontFacesTest, AFontsTablesDescribeItsFace) {
    const std::vector<FontFace> faces = nx::ui::readFontFaces(SquareFont::build(kBold), "bold.ttf");
    ASSERT_EQ(faces.size(), 1U);
    EXPECT_EQ(faces[0].file, std::filesystem::path{"bold.ttf"});
    EXPECT_EQ(faces[0].faceIndex, 0U);
    EXPECT_EQ(faces[0].families, std::vector<std::string>{"Square"});
    EXPECT_EQ(faces[0].style, "Bold");
    EXPECT_EQ(faces[0].weight, 700);
    EXPECT_FALSE(faces[0].isItalic);
}

TEST(FontFacesTest, ACollectionHasAFaceForEachOfItsFonts) {
    const std::array fonts{kRegular, kItalic};
    const std::vector<FontFace> faces = nx::ui::readFontFaces(SquareFont::collection(fonts));
    ASSERT_EQ(faces.size(), 2U);
    EXPECT_EQ(faces[0].faceIndex, 0U);
    EXPECT_EQ(faces[0].style, "Regular");
    EXPECT_EQ(faces[1].faceIndex, 1U);
    EXPECT_EQ(faces[1].style, "Italic");
    EXPECT_TRUE(faces[1].isItalic);
}

TEST(FontFacesTest, WhatIsNoFontHasNoFaces) {
    EXPECT_TRUE(nx::ui::readFontFaces(std::vector<std::byte>(64, std::byte{'x'})).empty());
    std::vector<std::byte> cut = SquareFont::build();
    cut.resize(40);
    EXPECT_TRUE(nx::ui::readFontFaces(cut).empty()) << "its tables cut off";
    EXPECT_TRUE(nx::ui::readFontFaces({}).empty());
}

TEST_F(CatalogTest, AFoldersFontFilesAreItsFaces) {
    FontCatalog catalog;
    EXPECT_EQ(catalog.addFolder(directory.path()), 4U) << "the text and the broken file left out";
    EXPECT_EQ(catalog.faces().size(), 4U);
}

TEST_F(CatalogTest, AWeightGetsTheFaceGdiGivesIt) {
    FontCatalog catalog;
    std::ignore = catalog.addFolder(directory.path());
    const auto chosen = [&catalog](std::int32_t weight) {
        const auto choice = catalog.choose("SQUARE", weight, false);
        EXPECT_TRUE(choice.has_value()) << weight;
        return choice ? std::pair{choice->face->style, choice->isEmboldened}
                      : std::pair{std::string{}, false};
    };
    EXPECT_EQ(chosen(0), (std::pair{std::string{"Regular"}, false})) << "0 asks for 400";
    EXPECT_EQ(chosen(550), (std::pair{std::string{"Regular"}, false}));
    EXPECT_EQ(chosen(551), (std::pair{std::string{"Regular"}, true}))
        << "more than 150 heavier: bold simulated";
    EXPECT_EQ(chosen(611), (std::pair{std::string{"Regular"}, true}));
    EXPECT_EQ(chosen(612), (std::pair{std::string{"Bold"}, false})) << "the bold face within 88";
    EXPECT_EQ(chosen(1000), (std::pair{std::string{"Bold"}, false})) << "a bold face is not struck twice";

    const auto italic = catalog.choose("Square", 700, true);
    ASSERT_TRUE(italic.has_value());
    EXPECT_EQ(italic->face->style, "Italic") << "the italic faces first";
    EXPECT_TRUE(italic->isEmboldened);

    const auto mark = [&catalog](std::int32_t weight) {
        return catalog.choose("mark", weight, true)->isEmboldened;
    };
    EXPECT_FALSE(mark(650)) << "500 is not 150 lighter than 650";
    EXPECT_TRUE(mark(651));

    EXPECT_FALSE(catalog.choose("Square Bold", 0, false).has_value()) << "no family has that name";
}

TEST_F(CatalogTest, AChosenFaceOpensAsAFont) {
    FontCatalog catalog;
    std::ignore = catalog.addFolder(directory.path());
    const auto choice = catalog.choose("Square", 600, false);
    ASSERT_TRUE(choice.has_value());
    auto font = catalog.open(*choice, {.cellHeight = 20});
    ASSERT_TRUE(font.has_value()) << nx::describe(font.error());
    EXPECT_EQ(font->family(), "Square");
    const auto glyph = font->glyph(U'A');
    ASSERT_TRUE(glyph.has_value()) << glyph.error().detail;
    EXPECT_EQ(glyph->width, 9) << "struck twice, a pixel wider";
    EXPECT_EQ(glyph->advance, 11);
}

TEST_F(CatalogTest, FilesThatCannotBeReadAreRefused) {
    FontCatalog catalog;
    const auto missing = catalog.addFile(directory.path() / "missing.ttf");
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, FontError::Unreadable);
    const auto text = catalog.addFile(directory.path() / "readme.txt");
    ASSERT_FALSE(text.has_value());
    EXPECT_EQ(text.error().code, FontError::NotAFont);
    EXPECT_TRUE(catalog.faces().empty());
}

} // namespace
