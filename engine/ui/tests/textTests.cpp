// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Text: a font's glyphs packed into an atlas, laid out where the pen puts them, and drawn on the
// screen offscreen on whatever Vulkan device the machine has (lavapipe in CI), with VGUI2's drop shadow
// when the font casts it.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.assets;
import nx.rhi;
import nx.rhi.vulkan;
import nx.render;
import nx.ui;

#include "offscreenTarget.hpp"
#include "squareFont.hpp"
#include "temporaryDirectory.hpp"

namespace {

using nx::assets::SpriteRect;
using nx::ui::Font;
using nx::ui::GlyphAtlas;
using nx::ui::PlacedGlyph;

/// The square font at 16 pixels per em (a line of 17 + 3), without antialiasing.
[[nodiscard]] auto squareFont() -> Font {
    auto font = Font::open(std::make_shared<const std::vector<std::byte>>(nx::test::SquareFont::build()),
                           {.cellHeight = 20});
    EXPECT_TRUE(font.has_value()) << (font ? "" : font.error().detail);
    return std::move(font).value();
}

[[nodiscard]] auto atlasOf(std::u32string_view characters, std::uint32_t width = nx::ui::kAtlasWidth)
    -> GlyphAtlas {
    auto atlas = GlyphAtlas::build(squareFont(), characters, width);
    EXPECT_TRUE(atlas.has_value()) << (atlas ? "" : atlas.error().detail);
    return std::move(atlas).value();
}

/// The alpha of texel `x`, `y` of `atlas`.
[[nodiscard]] auto alphaAt(const GlyphAtlas& atlas, std::uint32_t x, std::uint32_t y) -> int {
    return std::to_integer<int>(atlas.texels()[(((std::size_t{y} * atlas.width()) + x) * 4) + 3]);
}

TEST(GlyphAtlasTest, PacksEachGlyphOnceATexelApart) {
    const GlyphAtlas atlas = atlasOf(U"BAB A Z");
    EXPECT_EQ(atlas.width(), nx::ui::kAtlasWidth);
    EXPECT_EQ(atlas.height(), 10U) << "a row of glyphs 8 tall, a texel above and below";
    EXPECT_EQ(atlas.metrics().ascent, 17);

    const auto* square = atlas.glyph(U'A');
    ASSERT_NE(square, nullptr);
    EXPECT_EQ(square->source, (SpriteRect{.x = 1, .y = 1, .width = 8, .height = 8}));
    EXPECT_EQ(square->top, 8);
    EXPECT_EQ(square->advance, 10);
    const auto* shifted = atlas.glyph(U'B');
    ASSERT_NE(shifted, nullptr);
    EXPECT_EQ(shifted->source, (SpriteRect{.x = 10, .y = 1, .width = 8, .height = 8}));
    const auto* space = atlas.glyph(U' ');
    ASSERT_NE(space, nullptr);
    EXPECT_EQ(space->source, SpriteRect{}) << "nothing to draw";
    EXPECT_EQ(space->advance, 4);
    EXPECT_EQ(atlas.glyph(U'Z'), nullptr) << "the font has none";

    EXPECT_EQ(alphaAt(atlas, 1, 1), 255);
    EXPECT_EQ(alphaAt(atlas, 8, 8), 255);
    EXPECT_EQ(alphaAt(atlas, 0, 0), 0);
    EXPECT_EQ(alphaAt(atlas, 9, 1), 0) << "the gap between the glyphs";
    EXPECT_TRUE(
        std::ranges::all_of(std::views::iota(std::size_t{0}, atlas.texels().size()) |
                                std::views::filter([](std::size_t index) { return index % 4 != 3; }),
                            [&atlas](std::size_t index) { return atlas.texels()[index] == std::byte{255}; }))
        << "white everywhere: only the alpha is the glyphs'";
}

TEST(GlyphAtlasTest, FullRowsWrapAndWideGlyphsWiden) {
    const GlyphAtlas wrapped = atlasOf(U"AB", 18);
    EXPECT_EQ(wrapped.glyph(U'B')->source, (SpriteRect{.x = 1, .y = 10, .width = 8, .height = 8}));
    EXPECT_EQ(wrapped.height(), 19U);

    const GlyphAtlas widened = atlasOf(U"A", 4);
    EXPECT_EQ(widened.width(), 10U) << "the glyph and a texel either side";
}

TEST(TextLayoutTest, EachGlyphGoesWhereThePenPutsIt) {
    const GlyphAtlas atlas = atlasOf(U"AB ");
    // The baseline 17 below the line's top: each square's top 8 above it.
    const std::vector<PlacedGlyph> placed = nx::ui::layoutText(atlas, "AB A", 5.0F, 3.0F);
    ASSERT_EQ(placed.size(), 3U) << "the space draws nothing";
    EXPECT_EQ(placed[0].x, 5.0F);
    EXPECT_EQ(placed[0].y, 12.0F);
    EXPECT_EQ(placed[0].source, atlas.glyph(U'A')->source);
    EXPECT_EQ(placed[1].x, 15.0F);
    EXPECT_EQ(placed[1].source, atlas.glyph(U'B')->source);
    EXPECT_EQ(placed[2].x, 29.0F) << "after the space's advance";
    EXPECT_EQ(nx::ui::widthOf(atlas, "AB A"), 34);
}

TEST(TextLayoutTest, CharactersWithoutGlyphsAreLeftOut) {
    const GlyphAtlas atlas = atlasOf(U"A");
    const std::vector<PlacedGlyph> placed = nx::ui::layoutText(atlas,
                                                               "AZ\xC3\xA9\xFF"
                                                               "A",
                                                               0.0F, 0.0F);
    ASSERT_EQ(placed.size(), 2U);
    EXPECT_EQ(placed[1].x, 10.0F) << "nor do they move the pen";
    EXPECT_EQ(nx::ui::widthOf(atlas, "AZ"), 10);
}

TEST(TextDrawTest, GlyphsAreDrawnInTheTextsColor) {
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
    nx::test::OffscreenTarget target{**device};

    const GlyphAtlas atlas = atlasOf(U"AB");
    auto image = nx::test::inFrameOfItsOwn(**device, [&](nx::rhi::CommandList& commands) {
        return renderer->loadImage(commands, atlas.texels(), atlas.width(), atlas.height(), "text");
    });
    ASSERT_TRUE(image.has_value()) << nx::describe(image.error());
    std::vector<nx::render::ScreenQuad> quads;
    const float end =
        nx::ui::appendText(quads, atlas, *image, "AB", 2.0F, 0.0F, {.red = 200, .green = 100, .blue = 50});
    EXPECT_EQ(end, 22.0F);
    const std::vector<std::byte> drawn = target.draw([&](nx::rhi::CommandList& list) {
        const auto done = renderer->draw(list, {.size = nx::test::kTargetExtent, .quads = quads});
        EXPECT_TRUE(done.has_value()) << (done ? "" : nx::describe(done.error()));
    });

    // 'A' from 2 to 9 and 'B' from 12 to 19, rows 9 to 16: the baseline at 17.
    const auto rgbAt = [&drawn](std::uint32_t x, std::uint32_t y) {
        const nx::test::Pixel pixel = nx::test::pixelAt(drawn, x, y);
        return std::array{pixel[0], pixel[1], pixel[2]};
    };
    const std::array color{200, 100, 50};
    const std::array none{0, 0, 0};
    EXPECT_EQ(rgbAt(2, 9), color);
    EXPECT_EQ(rgbAt(9, 16), color);
    EXPECT_EQ(rgbAt(12, 9), color);
    EXPECT_EQ(rgbAt(10, 9), none) << "between the glyphs";
    EXPECT_EQ(rgbAt(2, 8), none) << "above the glyph";
    EXPECT_EQ(rgbAt(2, 17), none) << "on the baseline, under it";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(TextDrawTest, ADropShadowFallsAPixelRightAndDownUnderTheLine) {
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
    nx::test::OffscreenTarget target{**device};

    // The square font as a scheme's font that casts VGUI2's drop shadow.
    nx::test::TemporaryDirectory directory;
    directory.write("square.ttf", nx::test::SquareFont::build() | std::views::transform([](std::byte value) {
                                      return static_cast<char>(value);
                                  }) | std::ranges::to<std::string>());
    nx::ui::FontCatalog catalog;
    ASSERT_TRUE(catalog.addFolder(directory.path()).has_value());
    const nx::assets::SchemeFont font{
        .name = "EngineFont",
        .candidates = {nx::assets::FontCandidate{
            .face = "Square", .tall = 20, .flags = {nx::assets::FontFlag::DropShadow}}}};
    const auto set = nx::ui::SchemeFontSet::open(font, 768, catalog);
    ASSERT_TRUE(set.has_value()) << nx::describe(set.error());
    const auto atlas = GlyphAtlas::build(*set, U"AB");
    ASSERT_TRUE(atlas.has_value()) << atlas.error().detail;
    auto image = nx::test::inFrameOfItsOwn(**device, [&](nx::rhi::CommandList& commands) {
        return renderer->loadImage(commands, atlas->texels(), atlas->width(), atlas->height(), "text");
    });
    ASSERT_TRUE(image.has_value()) << nx::describe(image.error());

    // A grey ground, so that the shadow's black shows on it.
    constexpr nx::render::ScreenColor kGround{.red = 100, .green = 100, .blue = 100};
    std::vector<nx::render::ScreenQuad> quads{
        nx::render::ScreenQuad{.target = {.width = 32.0F, .height = 32.0F}, .color = kGround}};
    std::ignore = nx::ui::appendText(quads, *atlas, *image, "AB", 2.0F, 0.0F, {});
    ASSERT_EQ(quads.size(), 5U) << "the ground, both shadows, then both glyphs";
    for (const std::size_t shadow : {1U, 2U}) {
        EXPECT_EQ(quads[shadow].color, (nx::render::ScreenColor{.red = 0, .green = 0, .blue = 0}));
        EXPECT_EQ(quads[shadow].target.x, quads[shadow + 2].target.x + nx::ui::kDropShadowOffset);
        EXPECT_EQ(quads[shadow].target.y, quads[shadow + 2].target.y + nx::ui::kDropShadowOffset);
    }
    const std::vector<std::byte> drawn = target.draw([&](nx::rhi::CommandList& list) {
        const auto done = renderer->draw(list, {.size = nx::test::kTargetExtent, .quads = quads});
        EXPECT_TRUE(done.has_value()) << (done ? "" : nx::describe(done.error()));
    });

    // 'A' from 2 to 9 and 'B' from 12 to 19, rows 9 to 16; their shadows a pixel right and down.
    const auto rgbAt = [&drawn](std::uint32_t x, std::uint32_t y) {
        const nx::test::Pixel pixel = nx::test::pixelAt(drawn, x, y);
        return std::array{pixel[0], pixel[1], pixel[2]};
    };
    const std::array white{255, 255, 255};
    const std::array black{0, 0, 0};
    const std::array ground{100, 100, 100};
    EXPECT_EQ(rgbAt(2, 9), white);
    EXPECT_EQ(rgbAt(9, 16), white) << "over its own shadow";
    EXPECT_EQ(rgbAt(10, 10), black) << "A's shadow, right of it";
    EXPECT_EQ(rgbAt(3, 17), black) << "under it";
    EXPECT_EQ(rgbAt(10, 9), ground) << "a pixel down from the glyph's top";
    EXPECT_EQ(rgbAt(2, 17), ground) << "a pixel right of the glyph's left";
    EXPECT_EQ(rgbAt(11, 10), ground) << "between A's shadow and B";
    EXPECT_EQ(rgbAt(20, 17), black) << "B's";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

} // namespace
