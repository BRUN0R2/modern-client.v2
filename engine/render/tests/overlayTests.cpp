// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The overlay drawn offscreen on whatever Vulkan device the machine has (lavapipe in CI), with
// validation: triangles blended by their alpha over what is there, lines as they are, both where the
// camera sees them.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.rhi;
import nx.rhi.vulkan;
import nx.render;

#include "offscreenTarget.hpp"

namespace {

using nx::Vec3;
using nx::render::OverlayColor;
using nx::render::OverlayVertex;
using nx::test::pixelAt;

TEST(OverlayRendererTest, DrawsTrianglesBlendedAndLinesWhereTheCameraSeesThem) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    const auto executable = nx::platform::executablePath();
    ASSERT_TRUE(executable.has_value());
    const auto shader = nx::platform::readFile(executable->parent_path() / "shaders" / "overlay.spv");
    ASSERT_TRUE(shader.has_value());
    auto renderer = nx::render::OverlayRenderer::create(
        **device, {.shaderCode = *shader, .colorFormat = nx::rhi::Format::Rgba8Unorm});
    ASSERT_TRUE(renderer.has_value()) << nx::describe(renderer.error());

    // The camera at the origin looks along +x: +y is to its left, +z up. A red square, half opaque, on the
    // left; a green line, opaque, on the right.
    constexpr OverlayColor kRed{.red = 255, .green = 0, .blue = 0, .alpha = 128};
    constexpr OverlayColor kGreen{.red = 0, .green = 255, .blue = 0};
    const std::array triangles{
        OverlayVertex{.position = {.x = 100.0F, .y = 10.0F, .z = -50.0F}, .color = kRed},
        OverlayVertex{.position = {.x = 100.0F, .y = 60.0F, .z = -50.0F}, .color = kRed},
        OverlayVertex{.position = {.x = 100.0F, .y = 60.0F, .z = 50.0F}, .color = kRed},
        OverlayVertex{.position = {.x = 100.0F, .y = 10.0F, .z = -50.0F}, .color = kRed},
        OverlayVertex{.position = {.x = 100.0F, .y = 60.0F, .z = 50.0F}, .color = kRed},
        OverlayVertex{.position = {.x = 100.0F, .y = 10.0F, .z = 50.0F}, .color = kRed}};
    const std::array lines{
        OverlayVertex{.position = {.x = 100.0F, .y = -30.0F, .z = -50.0F}, .color = kGreen},
        OverlayVertex{.position = {.x = 100.0F, .y = -30.0F, .z = 50.0F}, .color = kGreen}};
    nx::test::OffscreenTarget target{**device};
    const std::vector<std::byte> pixels = target.draw([&](nx::rhi::CommandList& list) {
        const auto drawn = renderer->draw(list, {.camera = {}, .triangles = triangles, .lines = lines});
        EXPECT_TRUE(drawn.has_value());
    });

    const nx::test::Pixel left = pixelAt(pixels, 8, 16);
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
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

} // namespace
