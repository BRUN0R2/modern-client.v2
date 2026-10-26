// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The fonts of the installation's windows: the scheme read once, each font made for a screen's height;
// and cl_showfps's label, in the scheme's EngineFont, telling the nearest whole number of frames a second.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;
import nx.ui;
import cs.client;

#include "squareFont.hpp"
#include "temporaryDirectory.hpp"

namespace {

using cs::WindowFontError;

/// A scheme of the windows shaped like the original's, its EngineFont the square font, taller on taller
/// screens.
constexpr std::string_view kScheme = R"("Scheme"
{
	"Fonts"
	{
		"EngineFont"
		{
			"1"
			{
				"name"		"Square"
				"tall"		"13"
				"weight"	"600"
				"yres"	"480 599"
				"dropshadow"	"1"
			}
			"2"
			{
				"name"		"Square"
				"tall"		"20"
				"weight"	"600"
				"yres"	"600 6000"
				"dropshadow"	"1"
			}
		}
	}
}
)";

/// An installation's folder, and a catalog of the square font.
class WindowFontsTest : public testing::Test {
protected:
    WindowFontsTest() {
        fonts.write("square.ttf", nx::test::SquareFont::build() | std::views::transform([](std::byte value) {
                                      return static_cast<char>(value);
                                  }) | std::ranges::to<std::string>());
        std::ignore = catalog.addFolder(fonts.path());
    }

    /// Puts kScheme at cs::kWindowScheme in the installation.
    void writeScheme() const { folder.write(std::string{cs::kWindowScheme}, std::string{kScheme}); }

    /// The installation's files.
    [[nodiscard]] auto installation() const -> nx::assets::Vfs {
        const std::array mounts{folder.path()};
        auto vfs = nx::assets::Vfs::create(mounts);
        EXPECT_TRUE(vfs.has_value()) << (vfs ? "" : nx::describe(vfs.error()));
        return std::move(vfs).value();
    }

    nx::test::TemporaryDirectory folder;
    nx::test::TemporaryDirectory fonts;
    nx::ui::FontCatalog catalog;
};

TEST_F(WindowFontsTest, AFontIsMadeForTheScreensHeight) {
    writeScheme();
    const nx::assets::Vfs vfs = installation();
    const auto windowFonts = cs::WindowFonts::open(vfs, catalog);
    ASSERT_TRUE(windowFonts.has_value()) << nx::describe(windowFonts.error());

    const auto small = windowFonts->font("EngineFont", 480);
    ASSERT_TRUE(small.has_value()) << nx::describe(small.error());
    EXPECT_EQ(small->metrics().height(), 13);
    const auto large = windowFonts->font("EngineFont", 1080);
    ASSERT_TRUE(large.has_value()) << nx::describe(large.error());
    EXPECT_EQ(large->metrics().height(), 20);
    EXPECT_TRUE(large->hasDropShadow(U'A'));

    const auto missing = windowFonts->font("Default", 768);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, WindowFontError::NoSuchFont);
    const auto tiny = windowFonts->font("EngineFont", 240);
    ASSERT_FALSE(tiny.has_value());
    EXPECT_EQ(tiny.error().code, WindowFontError::Unusable) << "no candidate for a screen so short";
}

TEST_F(WindowFontsTest, WithoutTheSchemeThereAreNoFonts) {
    const nx::assets::Vfs vfs = installation();
    const auto windowFonts = cs::WindowFonts::open(vfs, catalog);
    ASSERT_FALSE(windowFonts.has_value());
    EXPECT_EQ(windowFonts.error().code, WindowFontError::SchemeUnreadable);
}

TEST_F(WindowFontsTest, TheFpsLabelWritesInTheEngineFont) {
    writeScheme();
    const nx::assets::Vfs vfs = installation();
    const auto windowFonts = cs::WindowFonts::open(vfs, catalog);
    ASSERT_TRUE(windowFonts.has_value()) << nx::describe(windowFonts.error());
    const auto label = cs::FpsLabel::open(*windowFonts, 768);
    EXPECT_TRUE(label.has_value()) << nx::describe(label.error());
}

TEST(FpsLabelTest, TellsTheNearestWholeNumberOfFramesASecond) {
    EXPECT_EQ(cs::fpsText(520.0), "520 fps");
    EXPECT_EQ(cs::fpsText(519.6), "520 fps");
    EXPECT_EQ(cs::fpsText(100.4), "100 fps");
    EXPECT_EQ(cs::fpsText(99.5), "100 fps");
    EXPECT_EQ(cs::fpsText(0.25), "0 fps") << "a frame that took four seconds";
}

} // namespace
