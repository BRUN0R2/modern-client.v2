// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Particles drawn offscreen on whatever Vulkan device the machine has (lavapipe in CI), with validation:
// a particle 5 units in front of a camera at the origin looking along +X, which sees 3.75 units each way
// there over the target's 16 pixels, its quad two units across, its dot in the quad's quarter to the
// view's right and up; streaks there too, drawn with a sprite of one white image built here.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.assets;
import nx.rhi;
import nx.rhi.vulkan;
import nx.render;

#include "offscreenTarget.hpp"
#include "vulkanAvailability.hpp"

namespace {

using nx::render::ParticleInstance;
using nx::render::StreakInstance;
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
/// A streak across the view 5 units in front of it, a unit to either side, half a unit wide each way:
/// over the middle pixel, two above it clear.
constexpr StreakInstance kAcross{.start = {.x = 5.0F, .y = 1.0F},
                                 .end = {.x = 5.0F, .y = -1.0F},
                                 .halfWidth = nx::HammerUnits{0.5F},
                                 .color = {.red = 255, .green = 167, .blue = 17},
                                 .amount = 255};
constexpr std::uint32_t kMiddle = nx::test::kTargetExtent.width / 2;
constexpr std::uint32_t kAboveTheStreak = kMiddle - 4;

template <class Value>
void append(std::vector<std::byte>& bytes, Value value) {
    const auto raw = std::bit_cast<std::array<std::byte, sizeof(Value)>>(value);
    bytes.insert(bytes.end(), raw.begin(), raw.end());
}

/// An additive sprite of version 2, parallel, of one image of 4 texels a side of its palette's white.
[[nodiscard]] auto whiteSprite() -> std::vector<std::byte> {
    constexpr std::int32_t kSide = 4;
    constexpr std::int32_t kVersion = 2;
    constexpr std::int32_t kParallel = 2;
    constexpr std::int32_t kAdditive = 1;
    std::vector<std::byte> bytes{std::byte{'I'}, std::byte{'D'}, std::byte{'S'}, std::byte{'P'}};
    append(bytes, kVersion);
    append(bytes, kParallel);
    append(bytes, kAdditive);
    append(bytes, 4.0F); // its radius
    append(bytes, kSide);
    append(bytes, kSide);
    append(bytes, std::int32_t{1}); // frames
    append(bytes, 0.0F);            // its beam's length
    append(bytes, std::int32_t{0}); // synchronized
    append(bytes, static_cast<std::int16_t>(nx::assets::kPaletteColors));
    bytes.insert(bytes.end(), nx::assets::kPaletteColors * nx::assets::kRgbBytes, std::byte{0xff});
    append(bytes, std::int32_t{0}); // a single image
    append(bytes, -kSide / 2);
    append(bytes, kSide / 2);
    append(bytes, kSide);
    append(bytes, kSide);
    bytes.insert(bytes.end(), std::size_t{kSide} * kSide, std::byte{1});
    return bytes;
}

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

/// The pixels of `particles` and `streaks` seen from the origin along +X.
[[nodiscard]] auto drawn(OffscreenTarget& target, nx::render::ParticleRenderer& renderer,
                         std::span<const ParticleInstance> particles,
                         std::span<const StreakInstance> streaks = {}) -> std::vector<std::byte> {
    return target.draw([&](nx::rhi::CommandList& list) {
        const auto drawnParticles =
            renderer.draw(list, {.aspect = 1.0F, .particles = particles, .streaks = streaks});
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

TEST(ParticleRendererTest, AStreakIsItsSpriteAlongItsLineAddedInItsColorOnceItHasItsSprite) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    std::optional<nx::render::ParticleRenderer> renderer = rendererOf(**device);
    ASSERT_TRUE(renderer.has_value());
    OffscreenTarget target{**device};
    const std::array across{kAcross};
    expectNear(pixelAt(drawn(target, *renderer, {}, across), kMiddle, kMiddle), {0, 0, 0}, "no sprite yet");

    const std::vector<std::byte> file = whiteSprite();
    const auto sprite = nx::assets::SpriteView::parse(file);
    ASSERT_TRUE(sprite.has_value()) << (sprite ? "" : nx::describe(sprite.error()));
    const auto loaded = nx::test::inFrameOfItsOwn(**device, [&](nx::rhi::CommandList& commands) {
        return renderer->loadStreakSprite(commands, *sprite);
    });
    ASSERT_TRUE(loaded.has_value()) << (loaded ? "" : nx::describe(loaded.error()));
    const std::vector<std::byte> pixels = drawn(target, *renderer, {}, across);
    expectNear(pixelAt(pixels, kMiddle, kMiddle), {255, 167, 17}, "white times its color, added to black");
    expectNear(pixelAt(pixels, kMiddle, kAboveTheStreak), {0, 0, 0}, "beyond its width");

    // Seen end on, it makes no line on the screen.
    const std::array endOn{StreakInstance{.start = {.x = 5.0F},
                                          .end = {.x = 10.0F},
                                          .halfWidth = nx::HammerUnits{0.5F},
                                          .color = kAcross.color,
                                          .amount = 255}};
    expectNear(pixelAt(drawn(target, *renderer, {}, endOn), kMiddle, kMiddle), {0, 0, 0}, "end on");
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

} // namespace
