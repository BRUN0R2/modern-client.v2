// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Studio models drawn offscreen on whatever Vulkan device the machine has (lavapipe in CI), with
// validation: the triangle model of the reader's tests, two bones and one triangle, (0, 0, 0) on the
// first and (8, 0, 8) and (0, 8, 8) on the second, its normal up, its texture of one color, 9 in every
// channel (17 through the default `texgamma`), chrome and masked.

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
#include "triangleModel.hpp"

namespace {

using nx::Mat3x4;
using nx::Vec3;
using nx::render::ModelLight;
using nx::render::StudioDepth;
using nx::render::StudioInstance;
using nx::test::OffscreenTarget;
using nx::test::Pixel;
using nx::test::pixelAt;

/// The middle of the target.
constexpr std::uint32_t kMiddle = nx::test::kTargetExtent.width / 2;
/// The model's texture, 9 in each channel, through the default `texgamma`.
constexpr int kTexel = 17;
/// The model's triangle: its middle, and the side its corners wind counterclockwise from, which the
/// original culls.
constexpr Vec3 kTriangleMiddle{.x = 8.0F / 3.0F, .y = 8.0F / 3.0F, .z = 16.0F / 3.0F};
constexpr Vec3 kWoundSide{.x = -1.0F, .y = -1.0F, .z = 1.0F};
/// How far from the triangle the camera is.
constexpr float kViewDistance = 6.0F;

[[nodiscard]] auto readFileOf(const std::filesystem::path& path) -> std::vector<std::byte> {
    auto bytes = nx::platform::readFile(path);
    EXPECT_TRUE(bytes.has_value() && !bytes->empty()) << path.string();
    return bytes.value_or(std::vector<std::byte>{});
}

[[nodiscard]] auto studioShader() -> std::vector<std::byte> {
    const auto executable = nx::platform::executablePath();
    return executable ? readFileOf(executable->parent_path() / "shaders" / "studio.spv")
                      : std::vector<std::byte>{};
}

/// A camera `distance` from the triangle's middle on the side `side` points to, looking at it.
[[nodiscard]] auto cameraFrom(Vec3 side, float distance) -> nx::render::Camera {
    const Vec3 way = nx::normalized(side);
    return {.position = kTriangleMiddle + (way * distance), .angles = nx::vectorAngles(-way)};
}

/// The triangle model, its view, the renderer and the model on the GPU.
struct Scene {
    nx::test::TriangleModel file;
    nx::assets::MdlView view;
    nx::render::StudioRenderer renderer;
    nx::render::StudioModel model;
};

[[nodiscard]] auto sceneOn(nx::rhi::Device& device) -> std::unique_ptr<Scene> {
    nx::test::TriangleModel file;
    auto view = nx::assets::MdlView::parse(file.bytes);
    EXPECT_TRUE(view.has_value()) << (view ? "" : nx::describe(view.error()));
    const std::vector<std::byte> shader = studioShader();
    if (!view) {
        return nullptr;
    }
    auto renderer = nx::test::inFrameOfItsOwn(device, [&](nx::rhi::CommandList& commands) {
        return nx::render::StudioRenderer::create(
            device, commands, {.shaderCode = shader, .colorFormat = nx::rhi::Format::Rgba8Unorm});
    });
    EXPECT_TRUE(renderer.has_value()) << (renderer ? "" : nx::describe(renderer.error()));
    if (!renderer) {
        return nullptr;
    }
    auto model = nx::test::inFrameOfItsOwn(device, [&](nx::rhi::CommandList& commands) {
        return renderer->load(commands, {.model = *view, .textures = *view});
    });
    EXPECT_TRUE(model.has_value()) << (model ? "" : nx::describe(model.error()));
    if (!model) {
        return nullptr;
    }
    return std::make_unique<Scene>(std::move(file), std::move(*view), std::move(*renderer),
                                   std::move(*model));
}

/// Draws `instances` as `camera` sees them.
[[nodiscard]] auto drawStudio(OffscreenTarget& target, Scene& scene, const nx::render::Camera& camera,
                              std::span<const StudioInstance> instances) -> std::vector<std::byte> {
    return target.draw([&](nx::rhi::CommandList& list) {
        const auto drawn =
            scene.renderer.draw(list, {.camera = camera, .aspect = 1.0F, .instances = instances});
        EXPECT_TRUE(drawn.has_value()) << (drawn ? "" : nx::describe(drawn.error()));
    });
}

/// A light of `ambient` alone, white.
[[nodiscard]] auto ambientLight(float ambient) -> ModelLight {
    return {.ambient = ambient, .shade = 0.0F};
}

/// The model's texture lit by `ambient` alone, from 0 to 1: the original's light curve at four times
/// it, as a vertex's color stores it, times the texture.
[[nodiscard]] auto texelLitBy(float ambient) -> float {
    constexpr float kCurveTop = 1023.0F;
    constexpr float kChannelTop = 255.0F;
    const auto curve = nx::render::lightGammaTable({});
    const float light = std::floor(static_cast<float>(curve[static_cast<std::size_t>(ambient * 4.0F)]) /
                                   kCurveTop * kChannelTop);
    return static_cast<float>(kTexel) / kChannelTop * (light / kChannelTop);
}

/// The pixel the model's texture makes, lit by `ambient` alone: the original's light curve at four
/// times it, as a vertex's color stores it, times the texture.
[[nodiscard]] auto litPixel(float ambient) -> Pixel {
    constexpr float kChannelTop = 255.0F;
    const int channel = static_cast<int>(std::lround(texelLitBy(ambient) * kChannelTop));
    return {channel, channel, channel, static_cast<int>(kChannelTop)};
}

void expectNear(const Pixel& actual, const Pixel& expected, std::string_view what = {}) {
    for (std::size_t channel = 0; channel < actual.size(); ++channel) {
        EXPECT_NEAR(actual.at(channel), expected.at(channel), 1) << what << ", channel " << channel;
    }
}

TEST(StudioRendererTest, DrawsAModelLitAsTheOriginalLightsIt) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    const auto scene = sceneOn(**device);
    ASSERT_NE(scene, nullptr);
    OffscreenTarget target{**device};
    const std::vector bones(scene->model.boneCount(), Mat3x4{});

    const nx::render::Camera camera = cameraFrom(-kWoundSide, kViewDistance);
    const std::array lit{
        StudioInstance{.model = scene->model, .bones = bones, .light = ambientLight(100.0F)}};
    const std::vector<std::byte> pixels = drawStudio(target, *scene, camera, lit);
    expectNear(pixelAt(pixels, kMiddle, kMiddle), litPixel(100.0F), "its ambient light alone");
    EXPECT_EQ(pixelAt(pixels, 0, 0)[3], 0) << "the triangle does not reach the corners";

    const std::array otherSkin{
        StudioInstance{.model = scene->model, .bones = bones, .light = ambientLight(100.0F), .skin = 7}};
    EXPECT_EQ(drawStudio(target, *scene, camera, otherSkin), pixels) << "a skin it lacks is its first";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(StudioRendererTest, TheTrianglesWoundCounterclockwiseAreCulledAsTheOriginalCullsThem) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    const auto scene = sceneOn(**device);
    ASSERT_NE(scene, nullptr);
    OffscreenTarget target{**device};
    const std::vector bones(scene->model.boneCount(), Mat3x4{});
    const std::array instances{
        StudioInstance{.model = scene->model, .bones = bones, .light = ambientLight(100.0F)}};

    const std::vector<std::byte> pixels =
        drawStudio(target, *scene, cameraFrom(kWoundSide, kViewDistance), instances);
    EXPECT_EQ(pixelAt(pixels, kMiddle, kMiddle)[3], 0)
        << "GL_FRONT: the side they wind counterclockwise from";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(StudioRendererTest, TheViewsOwnModelIsDrawnInFrontOfNearerOnes) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    const auto scene = sceneOn(**device);
    ASSERT_NE(scene, nullptr);
    OffscreenTarget target{**device};
    const nx::render::Camera camera = cameraFrom(-kWoundSide, kViewDistance);
    // The same triangle twice: the second 20 units behind the first, its bones placed there.
    const std::vector nearBones(scene->model.boneCount(), Mat3x4{});
    const std::vector farBones(scene->model.boneCount(),
                               nx::affine(nx::Mat3{}, nx::normalized(kWoundSide) * 20.0F));

    const std::array amongTheWorld{
        StudioInstance{.model = scene->model, .bones = nearBones, .light = ambientLight(50.0F)},
        StudioInstance{.model = scene->model, .bones = farBones, .light = ambientLight(200.0F)}};
    expectNear(pixelAt(drawStudio(target, *scene, camera, amongTheWorld), kMiddle, kMiddle), litPixel(50.0F),
               "the nearer one");

    const std::array withTheView{
        StudioInstance{.model = scene->model, .bones = nearBones, .light = ambientLight(50.0F)},
        StudioInstance{.model = scene->model,
                       .bones = farBones,
                       .light = ambientLight(200.0F),
                       .depth = StudioDepth::ViewModel}};
    expectNear(pixelAt(drawStudio(target, *scene, camera, withTheView), kMiddle, kMiddle), litPixel(200.0F),
               "the view's own, though farther");
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(StudioRendererTest, AnAddedModelIsUnlitAndAddedToWhatIsBehind) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    const auto scene = sceneOn(**device);
    ASSERT_NE(scene, nullptr);
    OffscreenTarget target{**device};
    const std::vector bones(scene->model.boneCount(), Mat3x4{});
    const std::array added{StudioInstance{.model = scene->model,
                                          .bones = bones,
                                          .light = ambientLight(100.0F),
                                          .mode = nx::render::RenderMode::TransAdd,
                                          .amount = 200}};

    // Its light is its renderamt, 200 of 255, whatever the world's: 17 times 200 / 255, over black.
    const int channel = static_cast<int>(std::lround(17.0F * 200.0F / 255.0F));
    const std::vector<std::byte> pixels =
        drawStudio(target, *scene, cameraFrom(-kWoundSide, kViewDistance), added);
    for (std::size_t color = 0; color < 3; ++color) {
        EXPECT_NEAR(pixelAt(pixels, kMiddle, kMiddle).at(color), channel, 1) << "channel " << color;
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(StudioRendererTest, BlendedModelsAreDrawnFromTheFarthest) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    const auto scene = sceneOn(**device);
    ASSERT_NE(scene, nullptr);
    OffscreenTarget target{**device};
    const nx::render::Camera camera = cameraFrom(-kWoundSide, kViewDistance);
    // The same triangle twice, each 200 of 255 opaque: the second 20 units behind the first, given
    // first. Drawn from the farthest, the far one shows through the near one.
    const Vec3 behind = nx::normalized(kWoundSide) * 20.0F;
    const std::vector nearBones(scene->model.boneCount(), Mat3x4{});
    const std::vector farBones(scene->model.boneCount(), nx::affine(nx::Mat3{}, behind));
    const auto blended = [&](std::span<const Mat3x4> bones, Vec3 origin, float ambient) {
        return StudioInstance{.model = scene->model,
                              .bones = bones,
                              .light = ambientLight(ambient),
                              .origin = origin,
                              .mode = nx::render::RenderMode::TransTexture,
                              .amount = 200};
    };
    const std::array instances{blended(nearBones, {}, 50.0F), blended(farBones, behind, 200.0F)};

    constexpr float kOpacity = 200.0F / 255.0F;
    const float far = std::round(texelLitBy(200.0F) * kOpacity * 255.0F) / 255.0F;
    const float near = (texelLitBy(50.0F) * kOpacity) + (far * (1.0F - kOpacity));
    const int channel = static_cast<int>(std::lround(near * 255.0F));
    const std::vector<std::byte> pixels = drawStudio(target, *scene, camera, instances);
    for (std::size_t color = 0; color < 3; ++color) {
        EXPECT_NEAR(pixelAt(pixels, kMiddle, kMiddle).at(color), channel, 1) << "channel " << color;
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

} // namespace
