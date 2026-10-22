// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Panels drawn: rectangles placed from a panel's top left and kept within it and the panels it is
// in; a panel's background, then its visible children over it; and a label's text, placed by its
// alignment.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.rhi;
import nx.rhi.vulkan;
import nx.render;
import nx.ui;

#include "offscreenTarget.hpp"
#include "squareFont.hpp"

namespace {

using nx::render::ScreenColor;
using nx::render::ScreenQuad;
using nx::ui::Label;
using nx::ui::Panel;
using nx::ui::PanelPainter;
using nx::ui::TextAlignment;

constexpr nx::rhi::Extent2d kScreen{.width = 32, .height = 32};
constexpr ScreenColor kGray{.red = 64, .green = 64, .blue = 64};

[[nodiscard]] auto isSameRect(const nx::rhi::Rect& a, const nx::rhi::Rect& b) -> bool {
    return a.x == b.x && a.y == b.y && a.size.width == b.size.width && a.size.height == b.size.height;
}

[[nodiscard]] auto rect(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height)
    -> nx::rhi::Rect {
    return {.x = x, .y = y, .size = {.width = width, .height = height}};
}

TEST(PanelPainterTest, PlacesFromThePanelsTopLeftWithinIt) {
    std::vector<ScreenQuad> quads;
    PanelPainter screen{quads, kScreen};
    screen.fill(2, 3, 4, 5, kGray);
    ASSERT_EQ(quads.size(), 1U);
    EXPECT_EQ(quads[0].target.x, 2.0F);
    EXPECT_EQ(quads[0].target.height, 5.0F);
    EXPECT_EQ(quads[0].blend, nx::render::ScreenBlend::Translucent);
    EXPECT_TRUE(isSameRect(*quads[0].clip, rect(0, 0, 32, 32)));

    PanelPainter panel = screen.inside(10, 10, 8, 8);
    panel.fill(-2, -2, 20, 20, kGray);
    ASSERT_EQ(quads.size(), 2U);
    EXPECT_EQ(quads[1].target.x, 8.0F) << "from the panel's top left";
    EXPECT_TRUE(isSameRect(*quads[1].clip, rect(10, 10, 8, 8))) << "kept within the panel";

    EXPECT_TRUE(isSameRect(screen.inside(30, 28, 8, 8).clip(), rect(30, 28, 2, 4)))
        << "and within the screen";
    EXPECT_TRUE(isSameRect(panel.inside(6, -4, 8, 8).clip(), rect(16, 10, 2, 4))) << "and within its parent";

    PanelPainter offscreen = screen.inside(40, 40, 4, 4);
    EXPECT_TRUE(offscreen.isClippedAway());
    offscreen.fill(0, 0, 4, 4, kGray);
    EXPECT_EQ(quads.size(), 2U) << "nothing drawn where nothing shows";
}

TEST(PanelPaintTest, ABackgroundThenTheVisibleChildrenWithinIt) {
    Panel dialog{"Dialog", {.x = 4, .y = 4, .wide = 20, .tall = 20}};
    dialog.setBackground(kGray);
    Panel& list = dialog.add(
        std::make_unique<Panel>("List", nx::ui::PanelBounds{.x = 2, .y = 2, .wide = 30, .tall = 30}));
    list.setBackground(ScreenColor{.red = 255});
    Panel& hidden = dialog.add(std::make_unique<Panel>("Hidden", nx::ui::PanelBounds{.wide = 4, .tall = 4}));
    hidden.setBackground(kGray);
    hidden.setVisible(false);
    hidden.add(std::make_unique<Panel>("Inside", nx::ui::PanelBounds{.wide = 2, .tall = 2}))
        .setBackground(kGray);
    dialog.add(std::make_unique<Panel>("Bare", nx::ui::PanelBounds{.wide = 4, .tall = 4}));

    std::vector<ScreenQuad> quads;
    dialog.paintTree(PanelPainter{quads, kScreen});
    ASSERT_EQ(quads.size(), 2U)
        << "the dialog's and the list's: the hidden panel draws nothing, nor its child";
    EXPECT_EQ(quads[0].target.x, 4.0F);
    EXPECT_EQ(quads[1].target.x, 6.0F);
    EXPECT_EQ(quads[1].color.red, 255);
    EXPECT_TRUE(isSameRect(*quads[1].clip, rect(6, 6, 18, 18))) << "the list within the dialog";
}

/// The square font at 16 pixels per em, a line of 20: 'A' and 'B' 10 pixels apart.
[[nodiscard]] auto squareAtlas() -> nx::ui::GlyphAtlas {
    auto font = nx::ui::Font::open(
        std::make_shared<const std::vector<std::byte>>(nx::test::SquareFont::build()), {.cellHeight = 20});
    EXPECT_TRUE(font.has_value());
    auto atlas = nx::ui::GlyphAtlas::build(*font, U"AB");
    EXPECT_TRUE(atlas.has_value());
    return std::move(atlas).value();
}

TEST(LabelTest, ItsAlignmentPlacesItsText) {
    const nx::ui::GlyphAtlas atlas = squareAtlas();
    Label label{"Title", {.wide = 40, .tall = 30}};
    label.setFace({.atlas = &atlas});
    label.setText("AB");
    const auto at = [&label](TextAlignment alignment) {
        label.setAlignment(alignment);
        return label.textPosition();
    };
    EXPECT_EQ(at(TextAlignment::West), (std::pair{0, 5})) << "the line halfway down";
    EXPECT_EQ(at(TextAlignment::Center), (std::pair{10, 5}));
    EXPECT_EQ(at(TextAlignment::East), (std::pair{20, 5}));
    EXPECT_EQ(at(TextAlignment::NorthWest), (std::pair{0, 0}));
    EXPECT_EQ(at(TextAlignment::SouthEast), (std::pair{20, 10}));
}

TEST(LabelTest, AlignmentsByTheirNamesInLayouts) {
    EXPECT_EQ(nx::ui::textAlignmentNamed("north-west"), TextAlignment::NorthWest);
    EXPECT_EQ(nx::ui::textAlignmentNamed("Center"), TextAlignment::Center);
    EXPECT_EQ(nx::ui::textAlignmentNamed("SOUTH-EAST"), TextAlignment::SouthEast);
    EXPECT_FALSE(nx::ui::textAlignmentNamed("middle").has_value());
}

TEST(LabelTest, DrawsItsTextOverItsBackground) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxUiTests", .isValidationEnabled = true});
    if (!device) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    const auto executable = nx::platform::executablePath();
    ASSERT_TRUE(executable.has_value());
    const auto shader = nx::platform::readFile(executable->parent_path() / "shaders" / "screen.spv");
    ASSERT_TRUE(shader.has_value());
    auto renderer = nx::render::ScreenRenderer::create(
        **device, {.shaderCode = *shader, .colorFormat = nx::rhi::Format::Rgba8Unorm});
    ASSERT_TRUE(renderer.has_value()) << nx::describe(renderer.error());
    const nx::ui::GlyphAtlas atlas = squareAtlas();
    auto image = nx::test::inFrameOfItsOwn(**device, [&](nx::rhi::CommandList& commands) {
        return renderer->loadImage(commands, atlas.texels(), atlas.width(), atlas.height(), "label");
    });
    ASSERT_TRUE(image.has_value()) << nx::describe(image.error());

    Label label{"Title", {.x = 4, .y = 2, .wide = 12, .tall = 24}};
    label.setBackground(kGray);
    label.setFace({.atlas = &atlas, .image = &*image});
    label.setText("AB");
    label.setColor({.red = 200});
    std::vector<ScreenQuad> quads;
    label.paintTree(PanelPainter{quads, kScreen});
    ASSERT_EQ(quads.size(), 3U) << "its background, then 'A' and 'B'";
    EXPECT_EQ(quads[0].image, nullptr);
    EXPECT_EQ(quads[1].image, &*image);
    EXPECT_EQ(quads[1].target.x, 4.0F);
    EXPECT_EQ(quads[1].target.y, 2.0F + 2.0F + 9.0F)
        << "the line 2 down, its baseline 17 below, the square 8 above it";
    EXPECT_EQ(quads[2].target.x, 14.0F);
    EXPECT_TRUE(isSameRect(*quads[2].clip, rect(4, 2, 12, 24))) << "'B' cut at the label's edge";
}

} // namespace
