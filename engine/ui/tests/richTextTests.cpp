// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Rich text: lines wrapped at the panel's width, the oldest dropped past its capacity, scrolled up
// from the newest, and drawn up from its bottom.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.rhi;
import nx.rhi.vulkan;
import nx.render;
import nx.ui;

#include "gpuScreen.hpp"

namespace {

using nx::render::ScreenColor;
using nx::ui::RichText;

constexpr ScreenColor kWhite{};
constexpr ScreenColor kRed{.red = 255, .green = 0, .blue = 0};

[[nodiscard]] auto textsOf(const std::vector<nx::ui::RichTextLine>& rows) -> std::vector<std::string> {
    return rows | std::views::transform(&nx::ui::RichTextLine::text) | std::ranges::to<std::vector>();
}

/// Rich text 50 wide and 60 tall, three rows of the square font: five A's to a row.
class RichTextTest : public testing::Test {
protected:
    RichTextTest() { text.setFace({.atlas = &atlas}); }

    nx::ui::GlyphAtlas atlas = nx::test::squareAtlas();
    RichText text{"Output", {.wide = 50, .tall = 60}};
};

TEST_F(RichTextTest, LinesWrapAtTheirLastSpaceThatFits) {
    text.append({.text = "AAA AAA", .color = kRed});
    text.append({.text = "AAAAAAA"});
    text.append({.text = ""});
    EXPECT_EQ(textsOf(text.rows()), (std::vector<std::string>{"AAA", "AAA", "AAAAA", "AA", ""}))
        << "at the space, the space left out; anywhere in a word wider than the panel";
    EXPECT_EQ(text.rows()[1].color, kRed) << "a line's rows in its colour";
    EXPECT_EQ(text.visibleRows(), 3U);
}

TEST_F(RichTextTest, ScrollsWithinItsRows) {
    for (const std::string_view line : {"A", "AB", "B", "BA", "AA"}) {
        text.append({.text = std::string{line}});
    }
    text.scrollBy(10);
    EXPECT_EQ(text.scrolled(), 2U) << "no further than the oldest row at the top";
    text.scrollBy(-1);
    EXPECT_EQ(text.scrolled(), 1U);
    text.append({.text = "B"});
    EXPECT_EQ(text.scrolled(), 2U) << "scrolled up, the rows shown stay put";
    text.scrollBy(-10);
    EXPECT_EQ(text.scrolled(), 0U) << "the newest at the bottom";
    text.append({.text = "A"});
    EXPECT_EQ(text.scrolled(), 0U) << "at the bottom, it follows the newest";
}

TEST(RichTextCapacityTest, TheOldestLinesGoFirst) {
    RichText text{"Output", {.wide = 50, .tall = 60}, 2};
    for (const std::string_view line : {"one", "two", "three"}) {
        text.append({.text = std::string{line}});
    }
    ASSERT_EQ(text.lines().size(), 2U);
    EXPECT_EQ(text.lines().front().text, "two");
    text.clear();
    EXPECT_TRUE(text.lines().empty());
}

TEST(RichTextDrawTest, TheNewestRowsUpFromItsBottom) {
    const nx::test::GpuScreen screen;
    if (!screen.isReady()) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    RichText text{"Output", {.x = 0, .y = 0, .wide = 50, .tall = 45}};
    text.setFace(screen.face());
    text.append({.text = "B"});
    text.append({.text = "A", .color = kRed});
    text.append({.text = "AA"});
    std::vector<nx::render::ScreenQuad> quads;
    text.paintTree(nx::ui::PanelPainter{quads, {.width = 64, .height = 64}});
    // Two rows show, 20 tall each, drawn from the newest: "AA" from 25, then "A" from 5; "B" is cut
    // off above.
    ASSERT_EQ(quads.size(), 3U);
    EXPECT_EQ(quads[0].target.y, 25.0F + 9.0F) << "'A' of \"AA\", its square 9 below its row's top";
    EXPECT_EQ(quads[1].target.x, 10.0F);
    EXPECT_EQ(quads[1].color, kWhite);
    EXPECT_EQ(quads[2].target.y, 5.0F + 9.0F);
    EXPECT_EQ(quads[2].color, kRed);
}

} // namespace
