// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The screen drawn offscreen on whatever Vulkan device the machine has (lavapipe in CI), with
// validation: pictures of a palette built here, each texel on the pixel it covers, drawn as they are,
// added or blended; a sprite's rectangle as the original draws it; colors alone; text in a console
// font; and a sprite's frames.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.assets;
import nx.rhi;
import nx.rhi.vulkan;
import nx.render;

#include "offscreenTarget.hpp"
#include "vulkanAvailability.hpp"

namespace {

using nx::render::ScreenBlend;
using nx::render::ScreenColor;
using nx::render::ScreenQuad;
using nx::test::OffscreenTarget;
using nx::test::Pixel;
using nx::test::pixelAt;

/// The palette: index `i` is (i, 2i, 3i) modulo 256, so that each index reads back as its own color.
[[nodiscard]] auto paletteBytes() -> std::vector<std::byte> {
    std::vector<std::byte> palette;
    for (std::size_t index = 0; index < nx::assets::kPaletteColors; ++index) {
        for (std::size_t channel = 1; channel <= 3; ++channel) {
            palette.push_back(static_cast<std::byte>((index * channel) % 256));
        }
    }
    return palette;
}

/// A 4 x 4 picture whose texel at `x`, `y` is index 10 + 4y + x, but the one at 3, 3: 255.
[[nodiscard]] auto pictureBytes() -> std::vector<std::byte> {
    std::vector<std::byte> pixels;
    for (std::size_t y = 0; y < 4; ++y) {
        for (std::size_t x = 0; x < 4; ++x) {
            pixels.push_back(x == 3 && y == 3 ? std::byte{255} : static_cast<std::byte>(10 + (4 * y) + x));
        }
    }
    return pixels;
}

[[nodiscard]] auto readFileOf(const std::filesystem::path& path) -> std::vector<std::byte> {
    auto bytes = nx::platform::readFile(path);
    EXPECT_TRUE(bytes.has_value() && !bytes->empty()) << path.string();
    return bytes.value_or(std::vector<std::byte>{});
}

/// What the tests draw with: a device, the screen renderer, the target, and the palette's pictures.
class Screen {
public:
    Screen() {
        auto device =
            nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
        if (!device) {
            EXPECT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
            return;
        }
        m_device = std::move(*device);
        const auto executable = nx::platform::executablePath();
        const std::vector<std::byte> shader =
            executable ? readFileOf(executable->parent_path() / "shaders" / "screen.spv")
                       : std::vector<std::byte>{};
        auto renderer = nx::render::ScreenRenderer::create(
            *m_device, {.shaderCode = shader, .colorFormat = nx::rhi::Format::Rgba8Unorm});
        EXPECT_TRUE(renderer.has_value()) << (renderer ? "" : nx::describe(renderer.error()));
        if (renderer) {
            m_renderer.emplace(std::move(*renderer));
            m_target.emplace(*m_device);
        }
    }

    [[nodiscard]] auto isReady() const noexcept -> bool { return m_renderer.has_value(); }
    [[nodiscard]] auto renderer() -> nx::render::ScreenRenderer& { return *m_renderer; }

    /// Loads what `load` makes in a frame of its own.
    template <class Load>
    [[nodiscard]] auto load(const Load& load) {
        auto made = nx::test::inFrameOfItsOwn(*m_device,
                                              [&](nx::rhi::CommandList& commands) { return load(commands); });
        EXPECT_TRUE(made.has_value()) << (made ? "" : nx::describe(made.error()));
        return std::move(made).value();
    }

    /// The pixels of `quads` drawn on the cleared target.
    [[nodiscard]] auto draw(std::span<const ScreenQuad> quads) -> std::vector<std::byte> {
        return m_target->draw([&](nx::rhi::CommandList& list) {
            const auto drawn = m_renderer->draw(list, {.size = nx::test::kTargetExtent, .quads = quads});
            EXPECT_TRUE(drawn.has_value()) << (drawn ? "" : nx::describe(drawn.error()));
        });
    }

    [[nodiscard]] auto validationErrors() const -> std::uint64_t { return m_device->validationErrors(); }

private:
    std::unique_ptr<nx::rhi::Device> m_device;
    std::optional<nx::render::ScreenRenderer> m_renderer;
    std::optional<OffscreenTarget> m_target;
};

/// The color index `index` of the palette shows, through the texture gamma.
[[nodiscard]] auto shown(std::size_t index) -> std::array<int, 3> {
    const auto gamma = nx::render::textureGammaTable({});
    return {gamma[(index * 1) % 256], gamma[(index * 2) % 256], gamma[(index * 3) % 256]};
}

void expectRgb(const Pixel& actual, std::array<int, 3> expected, std::string_view what) {
    for (std::size_t channel = 0; channel < expected.size(); ++channel) {
        EXPECT_NEAR(actual.at(channel), expected.at(channel), 1) << what << ", channel " << channel;
    }
}

TEST(RetainedQuadsTest, MakesTheQuadsAgainOnlyWhenWhatTheyShowChanges) {
    nx::render::RetainedQuads<int> retained;
    int made = 0;
    // A quad as wide as what it shows.
    const auto append = [&retained, &made](std::vector<ScreenQuad>& quads, int shown) {
        retained.append(quads, shown, [&made, shown](std::vector<ScreenQuad>& into) {
            ++made;
            into.push_back(ScreenQuad{.target = {.width = static_cast<float>(shown)}});
        });
    };
    std::vector<ScreenQuad> quads{ScreenQuad{}}; // what was appended before
    append(quads, 1);
    append(quads, 1);
    EXPECT_EQ(made, 1) << "what it shows is the same: its quads again";
    append(quads, 2);
    append(quads, 1);
    EXPECT_EQ(made, 3) << "made again each time what it shows changes";
    ASSERT_EQ(quads.size(), 5U);
    EXPECT_EQ(quads[0].target.width, 0.0F) << "appended after what was there";
    EXPECT_EQ(quads[1].target.width, 1.0F);
    EXPECT_EQ(quads[2].target.width, 1.0F);
    EXPECT_EQ(quads[3].target.width, 2.0F);
    EXPECT_EQ(quads[4].target.width, 1.0F);
}

TEST(ScreenRendererTest, EachTexelLandsOnThePixelItCovers) {
    Screen screen;
    if (!screen.isReady()) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    const std::vector<std::byte> palette = paletteBytes();
    const std::vector<std::byte> pixels = pictureBytes();
    const nx::assets::WadPicture picture{.width = 4, .height = 4, .pixels = pixels, .palette = palette};
    const nx::render::ScreenImage masked = screen.load([&](nx::rhi::CommandList& commands) {
        return screen.renderer().loadPicture(commands, picture, nx::render::PictureMask::Masked, "masked");
    });
    const std::array quads{ScreenQuad{.image = &masked,
                                      .source = {.x = 0, .y = 0, .width = 4, .height = 4},
                                      .target = {.x = 10.0F, .y = 6.0F, .width = 4.0F, .height = 4.0F}}};
    const std::vector<std::byte> drawn = screen.draw(quads);

    expectRgb(pixelAt(drawn, 10, 6), shown(10), "the top left texel on the top left pixel, from the top");
    expectRgb(pixelAt(drawn, 12, 7), shown(16), "texel 2, 1");
    EXPECT_EQ(pixelAt(drawn, 10, 6)[3], 255) << "drawn opaque";
    EXPECT_EQ(pixelAt(drawn, 13, 9), (Pixel{0, 0, 0, 0})) << "index 255 masked: what is behind";
    EXPECT_EQ(pixelAt(drawn, 9, 6), (Pixel{0, 0, 0, 0})) << "nothing left of it";
    EXPECT_EQ(pixelAt(drawn, 10, 10), (Pixel{0, 0, 0, 0})) << "nothing under it";
    EXPECT_EQ(screen.validationErrors(), 0U);
}

TEST(ScreenRendererTest, APartOfAPictureStretchesTexelsIntoBlocks) {
    Screen screen;
    if (!screen.isReady()) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    const std::vector<std::byte> palette = paletteBytes();
    const std::vector<std::byte> pixels = pictureBytes();
    const nx::assets::WadPicture picture{.width = 4, .height = 4, .pixels = pixels, .palette = palette};
    const nx::render::ScreenImage opaque = screen.load([&](nx::rhi::CommandList& commands) {
        return screen.renderer().loadPicture(commands, picture, nx::render::PictureMask::Opaque, "opaque");
    });
    // Texels 2..3 x 2..3, each made 3 x 3 pixels.
    const std::array quads{ScreenQuad{.image = &opaque,
                                      .source = {.x = 2, .y = 2, .width = 2, .height = 2},
                                      .target = {.x = 0.0F, .y = 0.0F, .width = 6.0F, .height = 6.0F}}};
    const std::vector<std::byte> drawn = screen.draw(quads);

    for (const auto& [x, y, index] :
         {std::tuple{0U, 0U, 20UZ}, std::tuple{2U, 2U, 20UZ}, std::tuple{3U, 0U, 21UZ},
          std::tuple{0U, 3U, 24UZ}, std::tuple{5U, 5U, 255UZ}}) {
        expectRgb(pixelAt(drawn, x, y), shown(index), std::format("pixel {}, {}", x, y));
    }
    EXPECT_EQ(screen.validationErrors(), 0U);
}

TEST(ScreenRendererTest, ASpriteRectangleIsDrawnAPixelLargerEachWayAndFilteredLinearly) {
    Screen screen;
    if (!screen.isReady()) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    // A 2 x 1 picture of indices 10 and 40: over 3 x 2 pixels, the middle column is half of each.
    const std::vector<std::byte> palette = paletteBytes();
    const std::array pixels{std::byte{10}, std::byte{40}};
    const nx::assets::WadPicture picture{.width = 2, .height = 1, .pixels = pixels, .palette = palette};
    const nx::render::ScreenImage opaque = screen.load([&](nx::rhi::CommandList& commands) {
        return screen.renderer().loadPicture(commands, picture, nx::render::PictureMask::Opaque, "pair");
    });
    const std::array quads{nx::render::spriteQuad(opaque, {.x = 0, .y = 0, .width = 2, .height = 1}, 4.0F,
                                                  2.0F, {}, ScreenBlend::Normal)};
    EXPECT_EQ(quads[0].target.width, 3.0F);
    EXPECT_EQ(quads[0].target.height, 2.0F);
    const std::vector<std::byte> drawn = screen.draw(quads);

    const std::array<int, 3> left = shown(10);
    const std::array<int, 3> right = shown(40);
    for (const std::uint32_t y : {2U, 3U}) {
        expectRgb(pixelAt(drawn, 4, y), left, std::format("the first texel, row {}", y));
        expectRgb(pixelAt(drawn, 5, y),
                  {(left[0] + right[0]) / 2, (left[1] + right[1]) / 2, (left[2] + right[2]) / 2},
                  std::format("half of each, row {}", y));
        expectRgb(pixelAt(drawn, 6, y), right, std::format("the second texel, row {}", y));
    }
    EXPECT_EQ(pixelAt(drawn, 7, 2), (Pixel{0, 0, 0, 0})) << "nothing right of it";
    EXPECT_EQ(pixelAt(drawn, 4, 4), (Pixel{0, 0, 0, 0})) << "nothing under it";
    EXPECT_EQ(screen.validationErrors(), 0U);
}

TEST(ScreenRendererTest, ColorsAreDrawnAsTheyAreAddedOrBlended) {
    Screen screen;
    if (!screen.isReady()) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    const ScreenColor half{.red = 200, .green = 100, .blue = 0, .alpha = 128};
    const std::array quads{
        // A gray square, then the color added over its left half, and blended over its right one.
        ScreenQuad{.target = {.x = 0.0F, .y = 0.0F, .width = 8.0F, .height = 4.0F},
                   .color = {.red = 40, .green = 40, .blue = 40},
                   .blend = ScreenBlend::Normal},
        ScreenQuad{.target = {.x = 0.0F, .y = 0.0F, .width = 4.0F, .height = 4.0F},
                   .color = half,
                   .blend = ScreenBlend::Additive},
        ScreenQuad{.target = {.x = 4.0F, .y = 0.0F, .width = 4.0F, .height = 4.0F},
                   .color = half,
                   .blend = ScreenBlend::Translucent}};
    const std::vector<std::byte> drawn = screen.draw(quads);

    const float share = 128.0F / 255.0F;
    const auto of = [share](float color, float under) {
        return static_cast<int>(std::lround(color * share + under));
    };
    expectRgb(pixelAt(drawn, 1, 1), {of(200.0F, 40.0F), of(100.0F, 40.0F), 40}, "added");
    expectRgb(pixelAt(drawn, 5, 1),
              {of(200.0F, 40.0F * (1.0F - share)), of(100.0F, 40.0F * (1.0F - share)),
               static_cast<int>(std::lround(40.0F * (1.0F - share)))},
              "blended");
    EXPECT_EQ(screen.validationErrors(), 0U);
}

TEST(ScreenRendererTest, AClippedRectangleDrawsOnlyWithinItsClip) {
    Screen screen;
    if (!screen.isReady()) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    const ScreenColor red{.red = 255, .green = 0, .blue = 0};
    const std::array quads{
        ScreenQuad{.target = {.x = 0.0F, .y = 0.0F, .width = 8.0F, .height = 8.0F},
                   .color = red,
                   .clip = nx::rhi::Rect{.x = 2, .y = 2, .size = {.width = 4, .height = 4}}},
        ScreenQuad{.target = {.x = 0.0F, .y = 10.0F, .width = 4.0F, .height = 4.0F}, .color = red},
        ScreenQuad{.target = {.x = 10.0F, .y = 0.0F, .width = 4.0F, .height = 4.0F},
                   .color = red,
                   .clip = nx::rhi::Rect{.x = 40, .y = 0, .size = {.width = 4, .height = 4}}}};
    const std::vector<std::byte> drawn = screen.draw(quads);

    expectRgb(pixelAt(drawn, 1, 1), {0, 0, 0}, "outside the clip");
    expectRgb(pixelAt(drawn, 2, 2), {255, 0, 0}, "the clip's first pixel");
    expectRgb(pixelAt(drawn, 5, 5), {255, 0, 0}, "the clip's last pixel");
    expectRgb(pixelAt(drawn, 6, 6), {0, 0, 0}, "past the clip");
    expectRgb(pixelAt(drawn, 3, 13), {255, 0, 0}, "the next rectangle unclipped");
    expectRgb(pixelAt(drawn, 11, 1), {0, 0, 0}, "a clip off the target draws nothing");
    EXPECT_EQ(screen.validationErrors(), 0U);
}

TEST(ScreenRendererTest, AnImageIsDrawnAsItIsTimesItsColor) {
    Screen screen;
    if (!screen.isReady()) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    // Two white texels, the first opaque and the second a quarter opaque, as a glyph's coverage is.
    const std::array<std::byte, 8> texels{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255},
                                          std::byte{255}, std::byte{255}, std::byte{255}, std::byte{64}};
    const nx::render::ScreenImage image = screen.load([&](nx::rhi::CommandList& commands) {
        return screen.renderer().loadImage(commands, texels, 2, 1, "pair");
    });
    const std::array quads{ScreenQuad{.image = &image,
                                      .source = {.x = 0, .y = 0, .width = 2, .height = 1},
                                      .target = {.x = 1.0F, .y = 1.0F, .width = 2.0F, .height = 1.0F},
                                      .color = {.red = 200, .green = 100, .blue = 0},
                                      .blend = ScreenBlend::Translucent}};
    const std::vector<std::byte> drawn = screen.draw(quads);

    expectRgb(pixelAt(drawn, 1, 1), {200, 100, 0}, "opaque: the color itself, no texture gamma");
    const float share = 64.0F / 255.0F;
    expectRgb(
        pixelAt(drawn, 2, 1),
        {static_cast<int>(std::lround(200.0F * share)), static_cast<int>(std::lround(100.0F * share)), 0},
        "a quarter of the color over the black");
    EXPECT_EQ(screen.validationErrors(), 0U);
}

TEST(ScreenRendererTest, TextIsTheFontsCharactersOneAfterAnother) {
    Screen screen;
    if (!screen.isReady()) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    // A font of a 256 x 4 picture: character 'A' 3 texels wide at 0, 'B' 2 wide at 3, of indices 1
    // and 2; the rest of the picture index 255, transparent.
    const std::vector<std::byte> palette = paletteBytes();
    std::vector<std::byte> pixels(std::size_t{256} * 4, std::byte{255});
    for (std::size_t y = 0; y < 4; ++y) {
        for (std::size_t x = 0; x < 5; ++x) {
            pixels[(y * 256) + x] = x < 3 ? std::byte{1} : std::byte{2};
        }
    }
    nx::assets::FontView font{.rowHeight = 4,
                              .picture = {.width = 256, .height = 4, .pixels = pixels, .palette = palette},
                              .glyphs = {}};
    font.glyphs[std::size_t{'A'}] = {.x = 0, .y = 0, .width = 3};
    font.glyphs[std::size_t{'B'}] = {.x = 3, .y = 0, .width = 2};
    const nx::render::ScreenFont loaded = screen.load(
        [&](nx::rhi::CommandList& commands) { return screen.renderer().loadFont(commands, font, "font"); });
    EXPECT_EQ(loaded.widthOf("ABA"), 8U);

    std::vector<ScreenQuad> quads;
    const float end = nx::render::appendText(quads, loaded, "ABA", 2.0F, 5.0F, {});
    EXPECT_EQ(end, 10.0F);
    const std::vector<std::byte> drawn = screen.draw(quads);
    expectRgb(pixelAt(drawn, 2, 5), shown(1), "A");
    expectRgb(pixelAt(drawn, 5, 8), shown(2), "B");
    expectRgb(pixelAt(drawn, 7, 5), shown(1), "A again");
    EXPECT_EQ(pixelAt(drawn, 10, 5), (Pixel{0, 0, 0, 0})) << "after the text";
    EXPECT_EQ(screen.validationErrors(), 0U);
}

TEST(ScreenRendererTest, ASpriteGivesAPictureForEachFrame) {
    Screen screen;
    if (!screen.isReady()) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    // An SPR v2 of two 2 x 1 frames, of indices 1 and 2.
    std::vector<std::byte> file{std::byte{'I'}, std::byte{'D'}, std::byte{'S'}, std::byte{'P'}};
    const auto append = [&file]<class Value>(Value value) {
        const auto raw = std::bit_cast<std::array<std::byte, sizeof(Value)>>(value);
        file.insert(file.end(), raw.begin(), raw.end());
    };
    for (const std::int32_t word : {2, 2, 0}) { // version, parallel, normal
        append(word);
    }
    append(1.0F);
    for (const std::int32_t word : {2, 1, 2}) { // width, height, frames
        append(word);
    }
    append(0.0F);
    append(std::int32_t{0});
    append(static_cast<std::int16_t>(nx::assets::kPaletteColors));
    const std::vector<std::byte> palette = paletteBytes();
    file.insert(file.end(), palette.begin(), palette.end());
    for (const std::byte index : {std::byte{1}, std::byte{2}}) {
        for (const std::int32_t word : {0, 0, 0, 2, 1}) { // a single frame, its origin, its size
            append(word);
        }
        file.insert(file.end(), 2, index);
    }
    const auto sprite = nx::assets::SpriteView::parse(file);
    ASSERT_TRUE(sprite.has_value()) << nx::describe(sprite.error());
    const std::vector<nx::render::ScreenImage> frames = screen.load([&](nx::rhi::CommandList& commands) {
        return screen.renderer().loadSprite(commands, *sprite, "sprite");
    });
    ASSERT_EQ(frames.size(), 2U);
    EXPECT_EQ(frames[1].width(), 2U);
    EXPECT_EQ(frames[1].height(), 1U);

    const std::array quads{ScreenQuad{.image = &frames[1],
                                      .source = {.x = 0, .y = 0, .width = 2, .height = 1},
                                      .target = {.x = 0.0F, .y = 0.0F, .width = 2.0F, .height = 1.0F}}};
    expectRgb(pixelAt(screen.draw(quads), 1, 0), shown(2), "the second frame");
    EXPECT_EQ(screen.validationErrors(), 0U);
}

} // namespace
