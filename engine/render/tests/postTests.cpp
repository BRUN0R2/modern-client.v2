// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The real Vulkan post pipelines, with validation and synthetic HDR clears: constant colors retain
// their energy across the accumulated bloom levels and resizing, while a bright patch scatters light
// into a visible halo without taking excessive light from its center. The effects' layer is added over
// the mapped scene as the original adds effects, each channel stopping at white, and glows in the
// bloom. The tone curve is drawn in a pass with the frame's depth, as the screen's pictures are drawn
// over it. No game assets are needed.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.rhi;
import nx.rhi.vulkan;
import nx.render;

#include "offscreenTarget.hpp"
#include "vulkanAvailability.hpp"

namespace {

using nx::rhi::ClearColor;
using nx::rhi::Extent2d;
using nx::rhi::TextureState;
using nx::test::Pixel;
using nx::test::pixelAt;

constexpr float kChannelTop = 255.0F;
constexpr std::size_t kRgbChannels = 3;
// The bloom may use R11G11B10F and is read back through an eight-bit target.
constexpr int kByteTolerance = 2;
constexpr float kDisplayGamma = 2.2F;

[[nodiscard]] auto linearColorOf(ClearColor display) -> ClearColor {
    return {.red = std::pow(display.red, kDisplayGamma),
            .green = std::pow(display.green, kDisplayGamma),
            .blue = std::pow(display.blue, kDisplayGamma),
            .alpha = display.alpha};
}

struct ScenePatch {
    Extent2d size;
    ClearColor color;
};

/// What a target holds: a color, and a patch of another at its top left.
struct Fill {
    ClearColor background{};
    std::optional<ScenePatch> patch{};
};

class PostRendererTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto device =
            nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
        if (!device) {
            const auto code = device.error().code;
            ASSERT_TRUE(nx::test::isVulkanUnavailable(code)) << nx::describe(device.error());
            GTEST_SKIP() << nx::test::kNoVulkan;
        }
        devicePtr = std::move(*device);
        const auto executable = nx::platform::executablePath();
        ASSERT_TRUE(executable.has_value()) << nx::describe(executable.error());
        const auto shader = nx::platform::readFile(executable->parent_path() / "shaders" / "post.spv");
        ASSERT_TRUE(shader.has_value()) << nx::describe(shader.error());
        auto renderer = nx::render::PostRenderer::create(
            *devicePtr, {.shaderCode = *shader, .outputFormat = nx::rhi::Format::Rgba8Unorm});
        ASSERT_TRUE(renderer.has_value()) << nx::describe(renderer.error());
        postRenderer.emplace(std::move(*renderer));
        target.emplace(*devicePtr);
        ASSERT_TRUE(resizeTo(nx::test::kTargetExtent));
    }

    void TearDown() override {
        if (devicePtr) {
            devicePtr->waitIdle();
            EXPECT_EQ(devicePtr->validationErrors(), 0U);
        }
    }

    [[nodiscard]] auto resizeTo(Extent2d size) -> bool {
        devicePtr->waitIdle();
        const auto resized = postRenderer->resize(size);
        EXPECT_TRUE(resized.has_value()) << (resized ? "" : nx::describe(resized.error()));
        if (resized) {
            sceneSize = size;
        }
        return resized.has_value();
    }

    [[nodiscard]] auto prepareScene(ClearColor background, std::optional<ScenePatch> patch = std::nullopt,
                                    const Fill& effects = {}) -> bool {
        auto frame = devicePtr->beginFrame();
        EXPECT_TRUE(frame.has_value()) << (frame ? "" : nx::describe(frame.error()));
        if (!frame) {
            return false;
        }
        auto& commands = frame->get();
        fill(commands, postRenderer->sceneTarget(), {.background = background, .patch = patch});
        fill(commands, postRenderer->effectsTarget(), effects);
        postRenderer->bloom(commands);
        const auto submitted = devicePtr->submitFrame();
        EXPECT_TRUE(submitted.has_value()) << (submitted ? "" : nx::describe(submitted.error()));
        devicePtr->waitIdle();
        return submitted.has_value();
    }

    /// The tone curve to the target, in a pass with the frame's depth; then what `after` records in it.
    [[nodiscard]] auto draw(const std::function<void(nx::rhi::CommandList&)>& after = {})
        -> std::vector<std::byte> {
        return target->draw([&](nx::rhi::CommandList& commands) {
            postRenderer->drawToneMapped(commands);
            if (after) {
                after(commands);
            }
        });
    }

    [[nodiscard]] auto device() -> nx::rhi::Device& { return *devicePtr; }

private:
    /// Clears `texture` to `with`, and leaves it in ShaderRead.
    void fill(nx::rhi::CommandList& commands, nx::rhi::TextureHandle texture, const Fill& with) const {
        commands.transition(texture, TextureState::Undefined, TextureState::RenderTarget);
        const std::array colors{nx::rhi::ColorAttachment{.texture = texture, .clear = with.background}};
        commands.beginRendering({.colors = colors, .area = sceneSize});
        commands.endRendering();
        if (with.patch) {
            // A load-op clear affects only the render area, leaving the surrounding background.
            commands.transition(texture, TextureState::RenderTarget, TextureState::RenderTarget);
            const std::array patchColors{
                nx::rhi::ColorAttachment{.texture = texture, .clear = with.patch->color}};
            commands.beginRendering({.colors = patchColors, .area = with.patch->size});
            commands.endRendering();
        }
        commands.transition(texture, TextureState::RenderTarget, TextureState::ShaderRead);
    }

    std::unique_ptr<nx::rhi::Device> devicePtr;
    std::optional<nx::render::PostRenderer> postRenderer;
    std::optional<nx::test::OffscreenTarget> target;
    Extent2d sceneSize{};
};

void expectConstant(std::span<const std::byte> pixels, ClearColor color) {
    ASSERT_EQ(pixels.size(), std::size_t{nx::test::kTargetExtent.width} * nx::test::kTargetExtent.height *
                                 nx::test::kPixelBytes);
    const auto byteOf = [](float channel) {
        return static_cast<int>(std::lround(channel * kChannelTop));
    };
    const std::array expected{byteOf(color.red), byteOf(color.green), byteOf(color.blue)};
    for (std::size_t offset = 0; offset < pixels.size(); offset += nx::test::kPixelBytes) {
        for (std::size_t channel = 0; channel < kRgbChannels; ++channel) {
            EXPECT_NEAR(std::to_integer<int>(pixels[offset + channel]), expected[channel], kByteTolerance)
                << "pixel " << offset / nx::test::kPixelBytes << ", channel " << channel;
        }
        EXPECT_EQ(pixels[offset + kRgbChannels], std::byte{255});
    }
}

[[nodiscard]] auto luminanceOf(const Pixel& color) -> float {
    constexpr std::array kWeights{0.2126F, 0.7152F, 0.0722F};
    float luminance = 0.0F;
    for (std::size_t channel = 0; channel < kRgbChannels; ++channel) {
        luminance += static_cast<float>(color[channel]) * kWeights[channel];
    }
    return luminance;
}

TEST_F(PostRendererTest, PreservesConstantGrayBelowTheToneKnee) {
    constexpr ClearColor kGray{.red = 0.4F, .green = 0.4F, .blue = 0.4F};
    ASSERT_TRUE(prepareScene(linearColorOf(kGray)));
    expectConstant(draw(), kGray);
}

TEST_F(PostRendererTest, PreservesConstantColorsAcrossOddAndSmallSizes) {
    constexpr std::array kSizes{Extent2d{.width = 63, .height = 47}, Extent2d{.width = 7, .height = 5},
                                Extent2d{.width = 1, .height = 1}, nx::test::kTargetExtent};
    constexpr std::array kColors{ClearColor{}, ClearColor{.red = 0.1F, .green = 0.35F, .blue = 0.75F},
                                 ClearColor{.red = 0.7F, .green = 0.2F, .blue = 0.5F}};
    for (const auto size : kSizes) {
        SCOPED_TRACE(std::format("scene {} x {}", size.width, size.height));
        ASSERT_TRUE(resizeTo(size));
        for (const auto color : kColors) {
            ASSERT_TRUE(prepareScene(linearColorOf(color)));
            expectConstant(draw(), color);
        }
    }
}

TEST_F(PostRendererTest, ScattersAnHdrHighlightInLinearLightAndRetainsItsCenter) {
    constexpr ScenePatch kHighlight{.size = {.width = 8, .height = 8},
                                    .color = {.red = 4.0F, .green = 1.0F, .blue = 0.5F}};
    ASSERT_TRUE(prepareScene({}, kHighlight));
    const auto pixels = draw();
    ASSERT_FALSE(pixels.empty());
    const Pixel center = pixelAt(pixels, kHighlight.size.width / 2, kHighlight.size.height / 2);
    const Pixel halo = pixelAt(pixels, kHighlight.size.width + 1, kHighlight.size.height / 2);

    // Linear scattering keeps a colored neighboring glow visible after the common shoulder gain.
    constexpr int kLeastBlueHalo = 5;
    EXPECT_GE(halo[2], kLeastBlueHalo);
    EXPECT_GT(halo[0], halo[1]);
    EXPECT_GT(halo[1], halo[2]);
    // With nonnegative bloom, at least this fraction of display light remains even if none returns.
    const float leastRetainedDisplay = std::pow(1.0F - nx::render::kBloomStrength, 1.0F / kDisplayGamma);
    constexpr Pixel kUnbloomedHighlight{255, 136, 99, 255};
    EXPECT_GE(luminanceOf(center),
              (luminanceOf(kUnbloomedHighlight) * leastRetainedDisplay) - kByteTolerance);
}

TEST_F(PostRendererTest, PreservesHdrGoldBlueAndNeutralChromaticityAcrossExposure) {
    // Filament's sRGB gold F0 (1, .85, .57), decoded once to linear Rec.709.
    constexpr ClearColor kGold{.red = 1.0F, .green = 0.692071F, .blue = 0.284647F};
    const ClearColor blue = linearColorOf({.red = 0.15F, .green = 0.35F, .blue = 1.0F});
    constexpr ClearColor kNeutral{.red = 0.18F, .green = 0.18F, .blue = 0.18F};
    const std::array colors{kGold, blue, kNeutral};
    constexpr std::array kGains{1.0F, 2.0F, 4.0F, 8.0F, 16.0F};
    constexpr float kRatioTolerance = 3.0F / kChannelTop;
    for (const auto color : colors) {
        const std::array radiance{color.red, color.green, color.blue};
        const float inputPeak = std::ranges::max(radiance);
        int previousPeak = 0;
        for (const float gain : kGains) {
            SCOPED_TRACE(std::format("RGB {}, {}, {}, gain {}", color.red, color.green, color.blue, gain));
            ASSERT_TRUE(prepareScene(
                {.red = color.red * gain, .green = color.green * gain, .blue = color.blue * gain}));
            const auto pixels = draw();
            ASSERT_FALSE(pixels.empty());
            const Pixel pixel = pixelAt(pixels, 0, 0);
            const int outputPeak = std::ranges::max(std::span{pixel}.first(kRgbChannels));
            ASSERT_GT(outputPeak, 0);
            EXPECT_GE(outputPeak, previousPeak - kByteTolerance);
            previousPeak = outputPeak;
            for (std::size_t channel = 0; channel < kRgbChannels; ++channel) {
                // A common gain preserves channel ratios regardless of the shoulder's exact curve.
                const float expectedRatio = std::pow(radiance[channel] / inputPeak, 1.0F / kDisplayGamma);
                EXPECT_NEAR(static_cast<float>(pixel[channel]) / static_cast<float>(outputPeak),
                            expectedRatio, kRatioTolerance);
            }
            EXPECT_EQ(pixel[3], 255);
        }
    }
}

TEST_F(PostRendererTest, AddsTheEffectsOverTheMappedSceneEachChannelStoppingAtWhite) {
    // A golden flash, orange as its texture is, over the map's mid tones: the original's addition turns
    // it yellow, red and green stopping at white, blue going on.
    constexpr ClearColor kGray{.red = 0.4F, .green = 0.4F, .blue = 0.4F};
    constexpr ClearColor kFlash{.red = 0.9F, .green = 0.7F, .blue = 0.2F};
    ASSERT_TRUE(prepareScene(linearColorOf(kGray), std::nullopt, {.background = kFlash}));
    const auto pixels = draw();
    ASSERT_FALSE(pixels.empty());
    const Pixel pixel = pixelAt(pixels, 0, 0);
    // A constant layer's light reaches the bloom as a constant does: the scene's light, plus the
    // bloom's share of the flash's own, through the curve's straight part, then the flash's colors added.
    const std::array flash{kFlash.red, kFlash.green, kFlash.blue};
    for (std::size_t channel = 0; channel < kRgbChannels; ++channel) {
        const float light = std::pow(kGray.red, kDisplayGamma) +
                            (nx::render::kBloomStrength * std::pow(flash.at(channel), kDisplayGamma));
        const float expected = std::min(std::pow(light, 1.0F / kDisplayGamma) + flash.at(channel), 1.0F);
        const auto expectedByte = static_cast<int>(std::lround(expected * kChannelTop));
        EXPECT_NEAR(pixel.at(channel), expectedByte, kByteTolerance) << "channel " << channel;
    }
    EXPECT_EQ(pixel[0], 255) << "red at white";
    EXPECT_EQ(pixel[1], 255) << "green at white: yellow, where the flash alone is orange";
    EXPECT_LT(pixel[2], 200);
}

TEST_F(PostRendererTest, AnEffectGlowsInTheBloomAboutIt) {
    constexpr ScenePatch kFlash{.size = {.width = 8, .height = 8},
                                .color = {.red = 1.0F, .green = 0.8F, .blue = 0.3F}};
    ASSERT_TRUE(prepareScene({}, std::nullopt, {.patch = kFlash}));
    const auto pixels = draw();
    ASSERT_FALSE(pixels.empty());
    const Pixel center = pixelAt(pixels, kFlash.size.width / 2, kFlash.size.height / 2);
    const Pixel halo = pixelAt(pixels, kFlash.size.width + 1, kFlash.size.height / 2);
    EXPECT_EQ(center[0], 255);
    EXPECT_GT(halo[0], 0) << "its light, about it on a black scene";
    EXPECT_GT(halo[0], halo[2]) << "of its color";

    ASSERT_TRUE(prepareScene({}));
    EXPECT_EQ(pixelAt(draw(), kFlash.size.width + 1, kFlash.size.height / 2)[0], 0) << "without it, none";
}

TEST_F(PostRendererTest, PresentationAlphaDoesNotModulateSceneRadiance) {
    constexpr ClearColor kRadiance{.red = 2.0F, .green = 0.8F, .blue = 0.2F};
    ASSERT_TRUE(prepareScene(kRadiance));
    const auto opaque = draw();
    ASSERT_FALSE(opaque.empty());
    for (const float alpha : {0.0F, 0.25F, 1.0F}) {
        SCOPED_TRACE(alpha);
        ASSERT_TRUE(prepareScene(
            {.red = kRadiance.red, .green = kRadiance.green, .blue = kRadiance.blue, .alpha = alpha}));
        EXPECT_EQ(draw(), opaque);
    }
}

TEST_F(PostRendererTest, TheScreensPicturesAreDrawnOverTheToneCurveInItsPass) {
    // As the client draws the HUD over the enhanced look: the tone curve and the screen's rectangles in
    // one pass, which has the frame's depth that both pipelines declare (validation reports a pipeline
    // whose depth format the pass lacks).
    const auto executable = nx::platform::executablePath();
    ASSERT_TRUE(executable.has_value()) << nx::describe(executable.error());
    const auto shader = nx::platform::readFile(executable->parent_path() / "shaders" / "screen.spv");
    ASSERT_TRUE(shader.has_value()) << nx::describe(shader.error());
    auto screen = nx::render::ScreenRenderer::create(
        device(), {.shaderCode = *shader, .colorFormat = nx::rhi::Format::Rgba8Unorm});
    ASSERT_TRUE(screen.has_value()) << nx::describe(screen.error());
    constexpr ClearColor kGray{.red = 0.4F, .green = 0.4F, .blue = 0.4F};
    ASSERT_TRUE(prepareScene(linearColorOf(kGray)));
    const std::array quads{nx::render::ScreenQuad{.target = {.width = 4.0F, .height = 4.0F},
                                                  .color = {.red = 255, .green = 0, .blue = 0}}};
    const std::vector<std::byte> pixels = draw([&](nx::rhi::CommandList& commands) {
        const auto drawn = screen->draw(commands, {.size = nx::test::kTargetExtent, .quads = quads});
        EXPECT_TRUE(drawn.has_value()) << (drawn ? "" : nx::describe(drawn.error()));
    });
    const Pixel corner = pixelAt(pixels, 0, 0);
    EXPECT_EQ(corner[0], 255) << "the rectangle over the scene";
    EXPECT_EQ(corner[1], 0);
    const Pixel middle =
        pixelAt(pixels, nx::test::kTargetExtent.width / 2, nx::test::kTargetExtent.height / 2);
    EXPECT_NEAR(middle[1], static_cast<int>(std::lround(kGray.green * kChannelTop)), kByteTolerance)
        << "the scene through the tone curve beside it";
}

} // namespace
