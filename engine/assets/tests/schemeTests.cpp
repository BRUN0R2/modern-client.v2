// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// VGUI2's schemes: colors, settings, fonts and borders, typed from texts shaped like the
// installation's TrackerScheme.res and ClientScheme.res.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;

namespace {

using nx::assets::BorderSide;
using nx::assets::FontFlag;
using nx::assets::Scheme;
using nx::assets::SchemeColor;
using nx::assets::SchemeError;

constexpr std::string_view kScheme = R"(//
// A scheme shaped like TrackerScheme.res
//
Scheme
{
	Colors
	{
		"BaseText"			"216 222 211 255"	// used in text windows, lists
		"ControlBG"			"76 88 68 255"
		"BorderDark"		"40 46 34 255"
		"BorderBright"		"136 145 128 255"
		"team0"				"125 165 210"
	}
	BaseSettings
	{
		"FgColor"			"BaseText"
		"BgColor"			"ControlBG"
		"Border.Literal"	"1 2 3 4"
		"TitleBarIcon"		"resource/icon_steam"
		Menu
		{
			"FgColor"		"BorderBright"
			"TextInset"		"6"
		}
	}
	Fonts
	{
		"Default"
		{
			"1"
			{
				"name"		"Tahoma"
				"tall"		"16"
				"weight"	"0"
				"antialias"	"1"
				"yres"		"480 599"
			}
			"2"
			{
				"name"		"Tahoma"
				"tall"		"20"
				"weight"	"500"
				"underline"	"1"
				"dropshadow" "0"
				"yres"		"600 6000"
				"range"		"0x0000 0x017F"
				"unknown"	"anything"
			}
		}
		"Marlett" { "1" { "name" "Marlett" "tall" "14" "symbol" "1" } }
	}
	Borders
	{
		BaseBorder		"InsetBorder"
		ButtonBorder	"BaseBorder"
		InsetBorder
		{
			"inset" "0 0 1 1"
			Left
			{
				"1" { "color" "BorderDark" "offset" "0 1" }
			}
			Top
			{
				"1" { "color" "BorderDark" "offset" "0 0" }
				"2" { "color" "BgColor" }
			}
		}
	}
	CustomFontFiles
	{
		"1"		"resource/linux_fonts/DejaVuSans.ttf"
	}
}
)";

[[nodiscard]] auto schemeOf(std::string_view text) -> nx::Result<Scheme, SchemeError> {
    const auto entries = nx::assets::parseKeyValues(text);
    EXPECT_TRUE(entries.has_value());
    return nx::assets::readScheme(entries.value_or(std::vector<nx::assets::KeyValuesEntry>{}));
}

[[nodiscard]] auto errorOf(std::string_view text) -> std::optional<SchemeError> {
    const auto scheme = schemeOf(text);
    return scheme ? std::nullopt : std::optional{scheme.error().code};
}

TEST(SchemeTest, ColorsAreLookedUpAsVgui2LooksThemUp) {
    const auto scheme = schemeOf(kScheme);
    ASSERT_TRUE(scheme.has_value()) << nx::describe(scheme.error());
    ASSERT_EQ(scheme->colors.size(), 5U);
    EXPECT_EQ(scheme->colors[0].name, "BaseText");
    EXPECT_EQ(scheme->color("BaseText"), (SchemeColor{.red = 216, .green = 222, .blue = 211, .alpha = 255}));
    EXPECT_EQ(scheme->color("team0"), (SchemeColor{.red = 125, .green = 165, .blue = 210, .alpha = 255}))
        << "three numbers are opaque";
    EXPECT_EQ(scheme->color("fgcolor"), scheme->color("BaseText")) << "a setting names a color";
    EXPECT_EQ(scheme->color("Menu.FgColor"), scheme->color("BorderBright")) << "a block's setting";
    EXPECT_EQ(scheme->color("Border.Literal"), (SchemeColor{.red = 1, .green = 2, .blue = 3, .alpha = 4}));
    EXPECT_FALSE(scheme->color("TitleBarIcon").has_value()) << "a picture is no color";
    EXPECT_FALSE(scheme->color("Nothing").has_value());
    EXPECT_EQ(scheme->setting("Menu.TextInset"), "6");
    EXPECT_EQ(scheme->setting("TitleBarIcon"), "resource/icon_steam");
    EXPECT_EQ(nx::assets::schemeColorOf("1 2 3 256"), std::nullopt);
    EXPECT_EQ(nx::assets::schemeColorOf("1 2"), std::nullopt);
    EXPECT_EQ(nx::assets::schemeColorOf("1 2 3 4 5"), std::nullopt);
}

TEST(SchemeTest, FontsKeepTheirCandidatesInOrder) {
    const auto scheme = schemeOf(kScheme);
    ASSERT_TRUE(scheme.has_value()) << nx::describe(scheme.error());
    const nx::assets::SchemeFont* font = scheme->font("default");
    ASSERT_NE(font, nullptr);
    ASSERT_EQ(font->candidates.size(), 2U);
    const nx::assets::FontCandidate& small = font->candidates[0];
    EXPECT_EQ(small.face, "Tahoma");
    EXPECT_EQ(small.tall, 16);
    EXPECT_EQ(small.weight, 0);
    EXPECT_TRUE(small.flags.test(FontFlag::Antialias));
    EXPECT_FALSE(small.flags.test(FontFlag::Underline));
    ASSERT_TRUE(small.heights.has_value());
    EXPECT_TRUE(small.heights->contains(480U));
    EXPECT_TRUE(small.heights->contains(599U));
    EXPECT_FALSE(small.heights->contains(600U));
    const nx::assets::FontCandidate& large = font->candidates[1];
    EXPECT_EQ(large.weight, 500);
    EXPECT_TRUE(large.flags.test(FontFlag::Underline));
    EXPECT_FALSE(large.flags.test(FontFlag::DropShadow)) << "\"0\" leaves a flag off";
    ASSERT_TRUE(large.characters.has_value());
    EXPECT_EQ(large.characters->first, U'\0');
    EXPECT_EQ(large.characters->last, U'ſ');
    EXPECT_TRUE(scheme->font("Marlett")->candidates[0].flags.test(FontFlag::Symbol));
    EXPECT_EQ(scheme->font("None"), nullptr);
    ASSERT_EQ(scheme->customFontFiles.size(), 1U);
    EXPECT_EQ(scheme->customFontFiles[0], "resource/linux_fonts/DejaVuSans.ttf");
}

TEST(SchemeTest, BordersFollowTheNamesTheyAreGiven) {
    const auto scheme = schemeOf(kScheme);
    ASSERT_TRUE(scheme.has_value()) << nx::describe(scheme.error());
    const nx::assets::SchemeBorder* button = scheme->border("ButtonBorder");
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->name, "InsetBorder") << "ButtonBorder is BaseBorder, which is InsetBorder";
    EXPECT_EQ(button->inset, (std::array<std::int32_t, 4>{0, 0, 1, 1}));
    ASSERT_EQ(button->sides[BorderSide::Left].size(), 1U);
    EXPECT_EQ(button->sides[BorderSide::Left][0].color, "BorderDark");
    EXPECT_EQ(button->sides[BorderSide::Left][0].startOffset, 0);
    EXPECT_EQ(button->sides[BorderSide::Left][0].endOffset, 1);
    ASSERT_EQ(button->sides[BorderSide::Top].size(), 2U);
    EXPECT_EQ(button->sides[BorderSide::Top][1].color, "BgColor");
    EXPECT_EQ(button->sides[BorderSide::Top][1].endOffset, 0) << "no offset is none";
    EXPECT_TRUE(button->sides[BorderSide::Right].empty());
    EXPECT_EQ(scheme->border("None"), nullptr);
}

TEST(SchemeTest, RefusesWhatCannotBeDrawn) {
    EXPECT_EQ(errorOf("Other { }"), SchemeError::NotAScheme);
    EXPECT_EQ(errorOf(R"(Scheme { Colors { "a" "1 2 x" } })"), SchemeError::BadColor);
    EXPECT_EQ(errorOf(R"(Scheme { Fonts { "f" { "1" { "tall" "big" } } } })"), SchemeError::BadFont);
    EXPECT_EQ(errorOf(R"(Scheme { Fonts { "f" { "1" { "yres" "480" } } } })"), SchemeError::BadFont);
    EXPECT_EQ(errorOf(R"(Scheme { Fonts { "f" { "1" { "range" "0 127" } } } })"), SchemeError::BadFont);
    EXPECT_EQ(errorOf(R"(Scheme { Borders { b { "inset" "1 2 3" } } })"), SchemeError::BadBorder);
    EXPECT_EQ(errorOf(R"(Scheme { Borders { b { Left { "1" { "offset" "0 0" } } } } })"),
              SchemeError::BadBorder);
    EXPECT_EQ(errorOf(R"(Scheme { Borders { a "b" b "a" } })"), SchemeError::BadBorder) << "a loop";
    EXPECT_EQ(errorOf(R"(Scheme { Borders { a "missing" } })"), SchemeError::BadBorder);
    EXPECT_TRUE(schemeOf("Scheme { }").has_value()) << "an empty scheme";
}

} // namespace
