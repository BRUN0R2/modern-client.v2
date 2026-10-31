// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The enhanced look's blood drawn offscreen on whatever Vulkan device the machine has (lavapipe in CI),
// with validation: drops 5 units in front of a camera at the origin looking along +X, which sees 3.75
// units each way there over half the target's 32 pixels, the view's right toward -Y and its up +Z.

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

using nx::render::BloodInstance;
using nx::render::BloodShape;
using nx::test::OffscreenTarget;
using nx::test::Pixel;
using nx::test::pixelAt;

/// 5 units in front of the view, where a unit is 16 / 3.75 pixels.
constexpr nx::Vec3 kAhead{.x = 5.0F};
constexpr float kPixelsPerUnit = 16.0F / 3.75F;
constexpr std::uint32_t kMiddle = nx::test::kTargetExtent.width / 2;
constexpr nx::render::RenderColor kRed{.red = 200, .green = 40, .blue = 30};
/// A thirtieth of a second of a droplet's flight drawn along it.
constexpr nx::Seconds kStreakTime{1.0F / 30.0F};

/// The pixel `units` from the middle toward the view's up (negative: down), or its right.
[[nodiscard]] constexpr auto pixelsAbove(float units) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(static_cast<float>(kMiddle) - (units * kPixelsPerUnit));
}
[[nodiscard]] constexpr auto pixelsRight(float units) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(static_cast<float>(kMiddle) + (units * kPixelsPerUnit));
}

[[nodiscard]] auto readFileOf(const std::filesystem::path& path) -> std::vector<std::byte> {
    auto bytes = nx::platform::readFile(path);
    EXPECT_TRUE(bytes.has_value()) << path.string();
    return std::move(bytes).value_or(std::vector<std::byte>{});
}

/// The blood's renderer of `device`.
[[nodiscard]] auto rendererOf(nx::rhi::Device& device) -> std::optional<nx::render::BloodRenderer> {
    const auto executable = nx::platform::executablePath();
    const std::vector<std::byte> shader =
        executable ? readFileOf(executable->parent_path() / "shaders" / "blood.spv")
                   : std::vector<std::byte>{};
    auto renderer = nx::render::BloodRenderer::create(
        device, {.shaderCode = shader, .colorFormat = nx::rhi::Format::Rgba8Unorm});
    EXPECT_TRUE(renderer.has_value()) << (renderer ? "" : nx::describe(renderer.error()));
    if (!renderer) {
        return std::nullopt;
    }
    return std::move(*renderer);
}

/// The pixels of `drops` seen from the origin along +X, through `fog`.
[[nodiscard]] auto drawn(OffscreenTarget& target, nx::render::BloodRenderer& renderer,
                         std::span<const BloodInstance> drops,
                         std::optional<nx::render::Fog> fog = std::nullopt) -> std::vector<std::byte> {
    return target.draw([&](nx::rhi::CommandList& list) {
        const auto drawnDrops =
            renderer.draw(list, {.aspect = 1.0F, .drops = drops, .streakTime = kStreakTime, .fog = fog});
        EXPECT_TRUE(drawnDrops.has_value()) << (drawnDrops ? "" : nx::describe(drawnDrops.error()));
    });
}

void expectNear(const Pixel& actual, std::array<double, 3> expected, double tolerance,
                std::string_view what) {
    for (std::size_t channel = 0; channel < expected.size(); ++channel) {
        EXPECT_NEAR(actual.at(channel), expected.at(channel), tolerance) << what << ", channel " << channel;
    }
}

constexpr std::array<double, 3> kRedChannels{200.0, 40.0, 30.0};
constexpr std::array<double, 3> kBlack{0.0, 0.0, 0.0};

/// `channels` times `share`.
[[nodiscard]] constexpr auto times(std::array<double, 3> channels, double share) noexcept
    -> std::array<double, 3> {
    return {channels[0] * share, channels[1] * share, channels[2] * share};
}

TEST(BloodRendererTest, AStillDropletIsARoundDropOfItsColor) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    std::optional<nx::render::BloodRenderer> renderer = rendererOf(**device);
    ASSERT_TRUE(renderer.has_value());
    OffscreenTarget target{**device};
    const std::array drops{BloodInstance{.position = kAhead, .radius = nx::HammerUnits{1.0F}, .color = kRed}};
    const std::vector<std::byte> pixels = drawn(target, *renderer, drops);
    expectNear(pixelAt(pixels, kMiddle, kMiddle), kRedChannels, 3.0, "its middle, lit fully");
    expectNear(pixelAt(pixels, kMiddle, pixelsAbove(1.5F)), kBlack, 0.0, "past its radius, above");
    expectNear(pixelAt(pixels, pixelsRight(1.5F), kMiddle), kBlack, 0.0, "past its radius, to the right");
    // Darker toward its rim than at its middle.
    EXPECT_LT(pixelAt(pixels, pixelsRight(0.8F), kMiddle).at(0), pixelAt(pixels, kMiddle, kMiddle).at(0));

    // Nothing to draw draws nothing.
    expectNear(pixelAt(drawn(target, *renderer, {}), kMiddle, kMiddle), kBlack, 0.0, "none");
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(BloodRendererTest, AFlyingDropletIsDrawnAlongItsFlightBehindItsHead) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    std::optional<nx::render::BloodRenderer> renderer = rendererOf(**device);
    ASSERT_TRUE(renderer.has_value());
    OffscreenTarget target{**device};
    // Falling at 60 units a second: drawn along the 2 units it falls in a thirtieth, above where it is.
    const BloodInstance falling{
        .position = kAhead, .velocity = {.z = -60.0F}, .radius = nx::HammerUnits{0.5F}, .color = kRed};
    const std::array drops{falling};
    const std::vector<std::byte> pixels = drawn(target, *renderer, drops);
    EXPECT_GT(pixelAt(pixels, kMiddle, pixelsAbove(1.5F)).at(0), 0) << "its streak, behind it";
    expectNear(pixelAt(pixels, kMiddle, pixelsAbove(-1.5F)), kBlack, 0.0, "ahead of its head");
    expectNear(pixelAt(pixels, pixelsRight(1.5F), kMiddle), kBlack, 0.0, "as wide as it is");

    // Still, it is round.
    const std::array still{BloodInstance{.position = kAhead, .radius = nx::HammerUnits{0.5F}, .color = kRed}};
    expectNear(pixelAt(drawn(target, *renderer, still), kMiddle, pixelsAbove(1.5F)), kBlack, 0.0, "round");
    // Flying straight at the view, it shows no streak.
    const std::array endOn{BloodInstance{
        .position = kAhead, .velocity = {.x = -60.0F}, .radius = nx::HammerUnits{0.5F}, .color = kRed}};
    expectNear(pixelAt(drawn(target, *renderer, endOn), kMiddle, pixelsAbove(1.5F)), kBlack, 0.0, "end on");
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(BloodRendererTest, AMistIsAPuffOfSmokeThickThroughItsCoreItsDensityUnevenByItsSeed) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    std::optional<nx::render::BloodRenderer> renderer = rendererOf(**device);
    ASSERT_TRUE(renderer.has_value());
    OffscreenTarget target{**device};
    // Falling across the view: a mist is drawn round all the same, no streak behind it.
    const BloodInstance mist{.position = kAhead,
                             .velocity = {.z = -60.0F},
                             .radius = nx::HammerUnits{2.0F},
                             .color = kRed,
                             .shape = BloodShape::Mist,
                             .seed = 0.25F};
    const std::array drops{mist};
    const std::vector<std::byte> pixels = drawn(target, *renderer, drops);
    // Through its core, a quarter of its radius out, as thick as its smoke makes it: 0.4 of it to all.
    for (const auto [x, y] : {std::pair{kMiddle, kMiddle}, std::pair{pixelsRight(0.5F), kMiddle},
                              std::pair{kMiddle, pixelsAbove(0.5F)}}) {
        const int red = pixelAt(pixels, x, y).at(0);
        EXPECT_GE(red, 200 * 2 / 5 - 3) << x << ", " << y;
        EXPECT_LE(red, 200 + 3) << x << ", " << y;
    }
    EXPECT_GT(pixelAt(pixels, kMiddle, pixelsAbove(1.5F)).at(0), 0) << "thinner toward its edge";
    expectNear(pixelAt(pixels, pixelsRight(2.5F), kMiddle), kBlack, 0.0, "past its edge");
    expectNear(pixelAt(pixels, kMiddle, pixelsAbove(3.0F)), kBlack, 0.0, "no streak behind it");

    // Its seed shapes its smoke, the same each frame.
    EXPECT_EQ(drawn(target, *renderer, drops), pixels) << "the same seed, the same smoke";
    BloodInstance otherSeed = mist;
    otherSeed.seed = 0.75F;
    const std::array other{otherSeed};
    EXPECT_NE(drawn(target, *renderer, other), pixels) << "another seed, other smoke";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(BloodRendererTest, ADropIsLitByTheLightWhereItIsAndBlendedAsOpaqueAsItIs) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    std::optional<nx::render::BloodRenderer> renderer = rendererOf(**device);
    ASSERT_TRUE(renderer.has_value());
    OffscreenTarget target{**device};
    const std::array dim{
        BloodInstance{.position = kAhead, .radius = nx::HammerUnits{1.0F}, .color = kRed, .light = 0.5F}};
    expectNear(pixelAt(drawn(target, *renderer, dim), kMiddle, kMiddle), times(kRedChannels, 0.5), 3.0,
               "in half the light");
    const std::array half{
        BloodInstance{.position = kAhead, .radius = nx::HammerUnits{1.0F}, .color = kRed, .opacity = 0.5F}};
    expectNear(pixelAt(drawn(target, *renderer, half), kMiddle, kMiddle), times(kRedChannels, 0.5), 3.0,
               "half over black");
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(BloodRendererTest, ASplatLiesFlatOnWhatItLandedOnDarkerWhereItPools) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    std::optional<nx::render::BloodRenderer> renderer = rendererOf(**device);
    ASSERT_TRUE(renderer.has_value());
    OffscreenTarget target{**device};
    // On a wall facing the view: seen whole, 0.7 as light at its middle, where it pools.
    const BloodInstance onTheWall{.position = kAhead,
                                  .radius = nx::HammerUnits{1.0F},
                                  .color = kRed,
                                  .shape = BloodShape::Splat,
                                  .normal = {.x = -1.0F}};
    const std::array wall{onTheWall};
    const std::vector<std::byte> pixels = drawn(target, *renderer, wall);
    // The middle pixel's center, half a pixel off its middle each way: lighter by how far out it is.
    const double middleReach = std::hypot(0.5, 0.5) / static_cast<double>(kPixelsPerUnit);
    expectNear(pixelAt(pixels, kMiddle, kMiddle), times(kRedChannels, std::lerp(0.7, 1.0, middleReach)), 3.0,
               "where it pools");
    EXPECT_GT(pixelAt(pixels, pixelsRight(0.6F), kMiddle).at(0), pixelAt(pixels, kMiddle, kMiddle).at(0))
        << "lighter toward its edge";
    expectNear(pixelAt(pixels, pixelsRight(1.5F), kMiddle), kBlack, 0.0, "past its lobes");

    // On a floor level with the view, it is seen edge on: not at all.
    BloodInstance onTheFloor = onTheWall;
    onTheFloor.normal = {.z = 1.0F};
    const std::array floor{onTheFloor};
    expectNear(pixelAt(drawn(target, *renderer, floor), kMiddle, kMiddle), kBlack, 0.0, "edge on");
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(BloodRendererTest, ADropIsSeenThroughTheFog) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    std::optional<nx::render::BloodRenderer> renderer = rendererOf(**device);
    ASSERT_TRUE(renderer.has_value());
    OffscreenTarget target{**device};
    // A GL_EXP2 fog hiding half of what is 5 units ahead along the view: (density 5)^2 = ln 2.
    const nx::render::Fog blue{.color = {.blue = 1.0F},
                               .curve = nx::render::FogCurve::ExponentialSquared,
                               .density = std::sqrt(std::numbers::ln2_v<float>) / kAhead.x};
    const std::array drops{BloodInstance{.position = kAhead, .radius = nx::HammerUnits{1.0F}, .color = kRed}};
    const Pixel seen = pixelAt(drawn(target, *renderer, drops), kMiddle, kMiddle);
    const Pixel fogged = pixelAt(drawn(target, *renderer, drops, blue), kMiddle, kMiddle);
    constexpr std::array kFogColor{0.0, 0.0, 255.0};
    for (std::size_t channel = 0; channel < kFogColor.size(); ++channel) {
        EXPECT_NEAR(fogged.at(channel),
                    std::lerp(static_cast<double>(seen.at(channel)), kFogColor.at(channel), 0.5), 2.0)
            << "half hidden, channel " << channel;
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

} // namespace
