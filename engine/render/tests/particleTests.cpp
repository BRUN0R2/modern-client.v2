// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Particles drawn offscreen on whatever Vulkan device the machine has (lavapipe in CI), with validation:
// a particle 5 units in front of a camera at the origin looking along +X, which sees 3.75 units each way
// there over the target's 16 pixels, its quad two units across, its dot in the quad's quarter to the
// view's right and up.

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

using nx::render::ParticleInstance;
using nx::test::OffscreenTarget;
using nx::test::Pixel;
using nx::test::pixelAt;

/// A particle 5 units in front of the view.
constexpr nx::Vec3 kAhead{.x = 5.0F};
/// The pixels of the middle of its dot, two pixels and a half to the right of the middle and above it,
/// and of its quad's opposite quarter, below and to the left.
constexpr std::uint32_t kDotX = 18;
constexpr std::uint32_t kDotY = 13;
constexpr std::uint32_t kOppositeX = 13;
constexpr std::uint32_t kOppositeY = 18;
constexpr nx::render::RenderColor kColor{.red = 200, .green = 100, .blue = 50};

[[nodiscard]] auto readFileOf(const std::filesystem::path& path) -> std::vector<std::byte> {
    auto bytes = nx::platform::readFile(path);
    EXPECT_TRUE(bytes.has_value()) << path.string();
    return std::move(bytes).value_or(std::vector<std::byte>{});
}

/// The particles' renderer of `device`, its dot uploaded.
[[nodiscard]] auto rendererOf(nx::rhi::Device& device) -> std::optional<nx::render::ParticleRenderer> {
    const auto executable = nx::platform::executablePath();
    const std::vector<std::byte> shader =
        executable ? readFileOf(executable->parent_path() / "shaders" / "particle.spv")
                   : std::vector<std::byte>{};
    auto renderer = nx::test::inFrameOfItsOwn(device, [&](nx::rhi::CommandList& commands) {
        return nx::render::ParticleRenderer::create(
            device, commands, {.shaderCode = shader, .colorFormat = nx::rhi::Format::Rgba8Unorm});
    });
    EXPECT_TRUE(renderer.has_value()) << (renderer ? "" : nx::describe(renderer.error()));
    if (!renderer) {
        return std::nullopt;
    }
    return std::move(*renderer);
}

/// The pixels of `particles` seen from the origin along +X.
[[nodiscard]] auto drawn(OffscreenTarget& target, nx::render::ParticleRenderer& renderer,
                         std::span<const ParticleInstance> particles) -> std::vector<std::byte> {
    return target.draw([&](nx::rhi::CommandList& list) {
        const auto drawnParticles = renderer.draw(list, {.aspect = 1.0F, .particles = particles});
        EXPECT_TRUE(drawnParticles.has_value())
            << (drawnParticles ? "" : nx::describe(drawnParticles.error()));
    });
}

void expectNear(const Pixel& actual, std::array<int, 3> expected, std::string_view what) {
    for (std::size_t channel = 0; channel < expected.size(); ++channel) {
        EXPECT_NEAR(actual.at(channel), expected.at(channel), 1) << what << ", channel " << channel;
    }
}

TEST(ParticleRendererTest, AParticleIsADotOfItsColorInTheCornerOfItsQuadTowardTheViewsRightAndUp) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    std::optional<nx::render::ParticleRenderer> renderer = rendererOf(**device);
    ASSERT_TRUE(renderer.has_value());
    OffscreenTarget target{**device};
    const std::array particles{ParticleInstance{.origin = kAhead, .color = kColor, .amount = 255}};
    const std::vector<std::byte> pixels = drawn(target, *renderer, particles);
    expectNear(pixelAt(pixels, kDotX, kDotY), {200, 100, 50}, "its dot, opaque");
    expectNear(pixelAt(pixels, kOppositeX, kOppositeY), {0, 0, 0}, "its quad's clear quarter");
    expectNear(pixelAt(pixels, 2, 2), {0, 0, 0}, "away from it");
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(ParticleRendererTest, AParticleIsBlendedAsOpaqueAsItsAmount) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    std::optional<nx::render::ParticleRenderer> renderer = rendererOf(**device);
    ASSERT_TRUE(renderer.has_value());
    OffscreenTarget target{**device};
    const std::array particles{ParticleInstance{.origin = kAhead, .color = kColor, .amount = 128}};
    const float share = 128.0F / 255.0F;
    const auto times = [share](int channel) {
        return static_cast<int>(std::lround(static_cast<float>(channel) * share));
    };
    expectNear(pixelAt(drawn(target, *renderer, particles), kDotX, kDotY),
               {times(200), times(100), times(50)}, "half over black");

    // Nothing to draw draws nothing.
    expectNear(pixelAt(drawn(target, *renderer, {}), kDotX, kDotY), {0, 0, 0}, "none");
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

} // namespace
