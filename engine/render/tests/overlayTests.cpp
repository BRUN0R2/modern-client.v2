// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The overlay drawn offscreen on whatever Vulkan device the machine has (lavapipe in CI), with
// validation: the mesh it keeps, triangles blended by their alpha and lines as they are, where the camera
// sees them and only while shown; each vertex's light times its brightness in a target of linear light
// alone; and arrows along a stretch, on a band facing the view, pointing to its end. The camera at the
// origin looks along +x: +y is to its left, +z up.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.rhi;
import nx.rhi.vulkan;
import nx.render;

#include "offscreenTarget.hpp"
#include "vulkanAvailability.hpp"

namespace {

using nx::Vec3;
using nx::render::ColorEncoding;
using nx::render::OverlayArrows;
using nx::render::OverlayColor;
using nx::render::OverlayVertex;
using nx::test::OffscreenTarget;
using nx::test::pixelAt;

/// Where the square the tests keep is seen, on the target's left.
constexpr std::uint32_t kSquareX = 8;
constexpr std::uint32_t kSquareY = 16;

[[nodiscard]] auto rendererOf(nx::rhi::Device& device, ColorEncoding encoding)
    -> std::optional<nx::render::OverlayRenderer> {
    const auto executable = nx::platform::executablePath();
    if (!executable) {
        ADD_FAILURE() << nx::describe(executable.error());
        return std::nullopt;
    }
    const auto shader = nx::platform::readFile(executable->parent_path() / "shaders" / "overlay.spv");
    if (!shader) {
        ADD_FAILURE() << nx::describe(shader.error());
        return std::nullopt;
    }
    auto renderer = nx::render::OverlayRenderer::create(
        device, {.shaderCode = *shader, .colorFormat = nx::rhi::Format::Rgba8Unorm, .encoding = encoding});
    if (!renderer) {
        ADD_FAILURE() << nx::describe(renderer.error());
        return std::nullopt;
    }
    return std::move(*renderer);
}

/// A square on the left of `color`, of `brightness`.
[[nodiscard]] auto squareOf(OverlayColor color, float brightness) -> std::array<OverlayVertex, 6> {
    const auto corner = [color, brightness](float y, float z) {
        return OverlayVertex{
            .position = {.x = 100.0F, .y = y, .z = z}, .color = color, .brightness = brightness};
    };
    return {corner(10.0F, -50.0F), corner(60.0F, -50.0F), corner(60.0F, 50.0F),
            corner(10.0F, -50.0F), corner(60.0F, 50.0F),  corner(10.0F, 50.0F)};
}

/// Keeps `mesh` in `renderer`, in a frame of its own.
void keep(nx::rhi::Device& device, nx::render::OverlayRenderer& renderer,
          const nx::render::OverlayMesh& mesh) {
    const auto kept = nx::test::inFrameOfItsOwn(
        device, [&](nx::rhi::CommandList& commands) { return renderer.keep(commands, mesh); });
    EXPECT_TRUE(kept.has_value()) << (kept ? "" : nx::describe(kept.error()));
}

TEST(OverlayRendererTest, DrawsTheMeshItKeepsWhereTheCameraSeesItWhileItIsShown) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    std::optional<nx::render::OverlayRenderer> renderer = rendererOf(**device, ColorEncoding::Display);
    ASSERT_TRUE(renderer.has_value());

    // A red square, half opaque, on the left; a green line, opaque, on the right.
    constexpr OverlayColor kRed{.red = 255, .green = 0, .blue = 0, .alpha = 128};
    constexpr OverlayColor kGreen{.red = 0, .green = 255, .blue = 0};
    const std::array triangles = squareOf(kRed, 1.0F);
    const std::array lines{
        OverlayVertex{.position = {.x = 100.0F, .y = -30.0F, .z = -50.0F}, .color = kGreen},
        OverlayVertex{.position = {.x = 100.0F, .y = -30.0F, .z = 50.0F}, .color = kGreen}};
    keep(**device, *renderer, {.triangles = triangles, .lines = lines});
    OffscreenTarget target{**device};
    const auto drawnShowing = [&](bool isMeshShown) {
        return target.draw([&](nx::rhi::CommandList& list) {
            EXPECT_TRUE(renderer->draw(list, {.camera = {}, .isMeshShown = isMeshShown}).has_value());
        });
    };

    const std::vector<std::byte> pixels = drawnShowing(true);
    const nx::test::Pixel left = pixelAt(pixels, kSquareX, kSquareY);
    EXPECT_NEAR(left[0], 128, 1);
    EXPECT_EQ(left[1], 0);
    EXPECT_EQ(left[3], 0); // the target's alpha is kept
    const nx::test::Pixel middle = pixelAt(pixels, 16, 16);
    EXPECT_EQ(middle[0], 0);
    EXPECT_EQ(middle[1], 0);
    bool isLineDrawn = false;
    for (std::uint32_t x = 18; x < nx::test::kTargetExtent.width; ++x) {
        isLineDrawn = isLineDrawn || pixelAt(pixels, x, 16)[1] == 255;
    }
    EXPECT_TRUE(isLineDrawn);

    const std::vector<std::byte> hidden = drawnShowing(false);
    EXPECT_TRUE(std::ranges::all_of(hidden, [](std::byte value) { return value == std::byte{0}; }))
        << "kept, but not shown";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(OverlayRendererTest, AVertexGivesItsLightTimesItsBrightnessInATargetOfLinearLightAlone) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    constexpr OverlayColor kGray{.red = 128, .green = 128, .blue = 128};
    constexpr float kBrightness = 4.0F;
    const auto drawnRed = [&](ColorEncoding encoding, float brightness) {
        std::optional<nx::render::OverlayRenderer> renderer = rendererOf(**device, encoding);
        EXPECT_TRUE(renderer.has_value());
        if (!renderer) {
            return 0;
        }
        const std::array triangles = squareOf(kGray, brightness);
        keep(**device, *renderer, {.triangles = triangles});
        OffscreenTarget target{**device};
        const std::vector<std::byte> pixels = target.draw([&](nx::rhi::CommandList& list) {
            EXPECT_TRUE(renderer->draw(list, {.camera = {}, .isMeshShown = true}).has_value());
        });
        return pixelAt(pixels, kSquareX, kSquareY)[0];
    };

    const int plain = drawnRed(ColorEncoding::Linear, 1.0F);
    EXPECT_GT(plain, 0);
    EXPECT_LT(plain, 128) << "its display color made linear light";
    EXPECT_NEAR(drawnRed(ColorEncoding::Linear, kBrightness), kBrightness * static_cast<float>(plain),
                kBrightness);
    EXPECT_NEAR(drawnRed(ColorEncoding::Display, kBrightness), 128, 1) << "a display color, as it is";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(OverlayRendererTest, ArrowsAlongAStretchPointToItsEndTheirHeadsAndShaftsOfTheirOwnColors) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    std::optional<nx::render::OverlayRenderer> renderer = rendererOf(**device, ColorEncoding::Display);
    ASSERT_TRUE(renderer.has_value());
    // Across the view, from its right to its left, at the height of its middle: a band of a few pixels,
    // whose arrows point left; their heads red, their shafts green, added to black.
    constexpr OverlayColor kRed{.red = 255, .green = 0, .blue = 0};
    constexpr OverlayColor kGreen{.red = 0, .green = 255, .blue = 0};
    const std::array arrows{OverlayArrows{.start = {.x = 100.0F, .y = -60.0F},
                                          .end = {.x = 100.0F, .y = 60.0F},
                                          .headColor = kRed,
                                          .shaftColor = kGreen}};
    constexpr nx::render::OverlayArrowStyle kStyle{.width = nx::HammerUnits{40.0F},
                                                   .spacing = nx::HammerUnits{60.0F}};
    OffscreenTarget target{**device};
    const std::vector<std::byte> pixels = target.draw([&](nx::rhi::CommandList& list) {
        EXPECT_TRUE(renderer->draw(list, {.camera = {}, .arrows = arrows, .arrowStyle = kStyle}).has_value());
    });

    // Along the band's middle, from the end on the left: an arrow's head, then its shaft.
    constexpr std::uint32_t kMiddleRow = 16;
    std::optional<std::uint32_t> firstHead;
    std::optional<std::uint32_t> firstShaft;
    for (std::uint32_t x = 0; x < nx::test::kTargetExtent.width; ++x) {
        const nx::test::Pixel pixel = pixelAt(pixels, x, kMiddleRow);
        EXPECT_EQ(pixel[2], 0);
        if (pixel[0] > 0 && !firstHead) {
            firstHead = x;
        }
        if (pixel[1] > 0 && !firstShaft) {
            firstShaft = x;
        }
        EXPECT_EQ(pixelAt(pixels, x, 1), (nx::test::Pixel{0, 0, 0, 0})) << "above the band";
        EXPECT_EQ(pixelAt(pixels, x, 30), (nx::test::Pixel{0, 0, 0, 0})) << "below the band";
    }
    ASSERT_TRUE(firstHead.has_value());
    ASSERT_TRUE(firstShaft.has_value());
    EXPECT_LT(*firstHead, *firstShaft) << "the heads toward the stretch's end";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

} // namespace
