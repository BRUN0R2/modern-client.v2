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
#include "vulkanAvailability.hpp"

namespace {

using nx::Mat3x4;
using nx::Vec3;
using nx::render::DepthLayer;
using nx::render::ModelLight;
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

[[nodiscard]] auto shaderOf(std::string_view file) -> std::vector<std::byte> {
    const auto executable = nx::platform::executablePath();
    return executable ? readFileOf(executable->parent_path() / "shaders" / file) : std::vector<std::byte>{};
}

[[nodiscard]] auto studioShader() -> std::vector<std::byte> {
    return shaderOf("studio.spv");
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
    const std::vector<std::byte> crowdShader = shaderOf("crowd.spv");
    if (!view) {
        return nullptr;
    }
    auto renderer = nx::test::inFrameOfItsOwn(device, [&](nx::rhi::CommandList& commands) {
        return nx::render::StudioRenderer::create(device, commands,
                                                  {.shaderCode = shader,
                                                   .crowdShaderCode = crowdShader,
                                                   .colorFormat = nx::rhi::Format::Rgba8Unorm});
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

// The blended tests' triangles: each 200 of 255 opaque, the far one 20 units behind the near one, the
// near one lit by 50, the far one by 200.
constexpr std::uint8_t kBlendedAmount = 200;
constexpr float kBehind = 20.0F;
constexpr float kNearAmbient = 50.0F;
constexpr float kFarAmbient = 200.0F;

/// The triangle of `scene`'s model posed by `bones` at `origin`, blended, lit by `ambient` alone.
[[nodiscard]] auto blendedTriangle(const Scene& scene, std::span<const Mat3x4> bones, Vec3 origin,
                                   float ambient) -> StudioInstance {
    return StudioInstance{.model = scene.model,
                          .bones = bones,
                          .light = ambientLight(ambient),
                          .origin = origin,
                          .mode = nx::render::RenderMode::TransTexture,
                          .amount = kBlendedAmount};
}

/// Each channel of the near triangle blended over the far one, blended over nothing.
[[nodiscard]] auto nearOverFarChannel() -> int {
    constexpr float kChannelTop = 255.0F;
    constexpr float kOpacity = static_cast<float>(kBlendedAmount) / kChannelTop;
    const float far = std::round(texelLitBy(kFarAmbient) * kOpacity * kChannelTop) / kChannelTop;
    const float near = (texelLitBy(kNearAmbient) * kOpacity) + (far * (1.0F - kOpacity));
    return static_cast<int>(std::lround(near * kChannelTop));
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
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
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

TEST(StudioRendererTest, ModelsDrawnAlikeTakeOneCallEachWithItsOwnLight) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const auto scene = sceneOn(**device);
    ASSERT_NE(scene, nullptr);
    OffscreenTarget target{**device};
    const std::vector bones(scene->model.boneCount(), Mat3x4{});
    const nx::render::Camera camera = cameraFrom(-kWoundSide, kViewDistance);
    // The later is the nearer, and shows: only if each has its own light does the light show its.
    constexpr float kNearer = 0.25F;
    Mat3x4 nearer = Mat3x4{};
    nearer.rows[0].w = kNearer;
    const std::vector nearerBones(scene->model.boneCount(), nearer);
    const std::array alike{
        StudioInstance{.model = scene->model, .bones = bones, .light = ambientLight(200.0F)},
        StudioInstance{.model = scene->model, .bones = nearerBones, .light = ambientLight(100.0F)}};
    const std::vector<std::byte> pixels = drawStudio(target, *scene, camera, alike);
    EXPECT_EQ(scene->renderer.modelCallCount(), 1U) << "of one model, skin, body and facing";
    expectNear(pixelAt(pixels, kMiddle, kMiddle), litPixel(100.0F), "the nearer's light");
    const std::array otherFacing{
        StudioInstance{.model = scene->model, .bones = bones, .light = ambientLight(100.0F)},
        StudioInstance{
            .model = scene->model, .bones = bones, .light = ambientLight(100.0F), .isMirrored = true}};
    std::ignore = drawStudio(target, *scene, camera, otherFacing);
    EXPECT_EQ(scene->renderer.modelCallCount(), 2U) << "they face other ways";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

/// A crowd of `scene`'s model that plays one sequence of two frames, its bones moved `kPoseMove` along x by
/// the second, on `scene`'s renderer.
constexpr float kPoseMove = 0.5F;

void loadCrowdOf(nx::rhi::Device& device, Scene& scene) {
    const std::size_t bones = scene.model.boneCount();
    std::vector<nx::render::CrowdBonePose> poses(bones * 2, {.rotation = {.w = 1.0F}});
    for (std::size_t bone = bones; bone < poses.size(); ++bone) {
        poses[bone].position.x = kPoseMove;
    }
    const std::vector<std::int32_t> parents(bones, -1);
    const std::array sequences{nx::render::CrowdSequence{
        .poses = poses, .frameCount = 2, .reach = 64.0F, .lowest = -64.0F, .highest = 64.0F}};
    const auto loaded = nx::test::inFrameOfItsOwn(device, [&](nx::rhi::CommandList& commands) {
        return scene.renderer.loadCrowdModel(
            commands, {.model = scene.model, .parents = parents, .sequences = sequences});
    });
    ASSERT_TRUE(loaded.has_value()) << (loaded ? "" : nx::describe(loaded.error()));
}

/// Works `body` out on the GPU, the only one of the crowd, as `camera` sees it.
void recordCrowdOf(nx::rhi::Device& device, Scene& scene, const nx::render::Camera& camera,
                   const nx::render::CrowdBody& body, std::uint64_t tick) {
    const std::array frustum = nx::render::frustumPlanes(camera, 1.0F);
    const auto recorded = nx::test::inFrameOfItsOwn(
        device, [&](nx::rhi::CommandList& commands) -> nx::Result<void, nx::render::StudioError> {
            auto room = scene.renderer.crowdRoom(1, tick);
            if (!room) {
                return std::unexpected{std::move(room.error())};
            }
            room->bodies[0] = body;
            room->bodiesOfModel[0] = 1;
            return scene.renderer.recordCrowd(commands, {.frustum = frustum});
        });
    ASSERT_TRUE(recorded.has_value()) << (recorded ? "" : nx::describe(recorded.error()));
}

TEST(StudioRendererTest, ACrowdIsDrawnAsTheCpuDrawsTheModelsPosedTheSameWay) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const auto scene = sceneOn(**device);
    ASSERT_NE(scene, nullptr);
    OffscreenTarget target{**device};
    const nx::render::Camera camera = cameraFrom(-kWoundSide, kViewDistance);
    // Where a body at `origin`, halfway through its two frames, puts its bones.
    constexpr Vec3 kOrigin{.x = 0.25F, .y = -0.25F};
    constexpr float kHalfway = kPoseMove / 2.0F;
    Mat3x4 posed{};
    posed.rows[0].w = kOrigin.x + kHalfway;
    posed.rows[1].w = kOrigin.y;
    const std::vector bones(scene->model.boneCount(), posed);
    const std::array byTheCpu{
        StudioInstance{.model = scene->model, .bones = bones, .light = ambientLight(100.0F)}};
    const std::vector<std::byte> expected = drawStudio(target, *scene, camera, byTheCpu);
    ASSERT_GT(pixelAt(expected, kMiddle, kMiddle)[3], 0) << "the model shows";
    ASSERT_NO_FATAL_FAILURE(loadCrowdOf(**device, *scene));
    // Half of a sequence's frames, in 256ths; its move ended where the second half of it started.
    constexpr float kHalfOfTheFrames = 128.0F;
    const nx::render::CrowdBody body{.previousOrigin = kOrigin,
                                     .origin = kOrigin,
                                     .frame = kHalfOfTheFrames,
                                     .framerate = 0.0F,
                                     .ambient = 100.0F,
                                     .moveShare = 1.0F,
                                     .moveSharePerTick = 0.0F};
    ASSERT_NO_FATAL_FAILURE(recordCrowdOf(**device, *scene, camera, body, 1));
    EXPECT_GT(scene->renderer.crowdDrawCount(), 0U);
    const std::vector<std::byte> byTheGpu = drawStudio(target, *scene, camera, {});
    expectNear(pixelAt(byTheGpu, kMiddle, kMiddle), pixelAt(expected, kMiddle, kMiddle),
               "as the CPU draws it");
    EXPECT_EQ(byTheGpu, expected) << "every pixel";

    // Along its move: a body a quarter of the way at the last tick, and as much again by the moment shown, a
    // whole tick on, is halfway from where it started to where it ends: at the same place.
    constexpr Vec3 kMovedBy{.x = 1.0F};
    nx::render::CrowdBody moving = body;
    moving.previousOrigin = kOrigin - kMovedBy;
    moving.origin = kOrigin + kMovedBy;
    moving.moveShare = 0.25F;
    moving.moveSharePerTick = 0.25F;
    ASSERT_NO_FATAL_FAILURE(recordCrowdOf(**device, *scene, camera, moving, 2));
    EXPECT_EQ(drawStudio(target, *scene, camera, {}), expected) << "along its move";

    // Behind the view, none of it: its draws draw no instance.
    nx::render::CrowdBody behind = body;
    const Vec3 away = nx::normalized(-kWoundSide) * (kViewDistance * 4.0F);
    behind.previousOrigin = away;
    behind.origin = away;
    ASSERT_NO_FATAL_FAILURE(recordCrowdOf(**device, *scene, camera, behind, 3));
    const std::vector<std::byte> hidden = drawStudio(target, *scene, camera, {});
    EXPECT_EQ(pixelAt(hidden, kMiddle, kMiddle)[3], 0) << "hidden by the view's planes";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(StudioRendererTest, ACrowdPlacesEachBoneInItsParentAsTheCpuDoes) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const auto scene = sceneOn(**device);
    ASSERT_NE(scene, nullptr);
    ASSERT_EQ(scene->model.boneCount(), 2U) << "a root and its child";
    OffscreenTarget target{**device};
    const nx::render::Camera camera = cameraFrom(-kWoundSide, kViewDistance);
    // A body turned a little about its height, its root turned a little more and moved, its child moved from
    // it.
    constexpr float kYaw = 0.1F;
    constexpr float kRootTurn = 0.1F;
    constexpr Vec3 kOrigin{.x = 0.1F};
    constexpr Vec3 kRootMoved{.y = 0.1F};
    constexpr Vec3 kChildMoved{.x = 0.1F, .y = 0.1F};
    const float halfSine = std::sin(kRootTurn / 2.0F);
    const float halfCosine = std::cos(kRootTurn / 2.0F);
    const auto turnedAboutHeight = [](float cosine, float sine, Vec3 moved) {
        return Mat3x4{.rows = {nx::Vec4{.x = cosine, .y = -sine, .w = moved.x},
                               nx::Vec4{.x = sine, .y = cosine, .w = moved.y},
                               nx::Vec4{.z = 1.0F, .w = moved.z}}};
    };
    const Mat3x4 placement = turnedAboutHeight(std::cos(kYaw), std::sin(kYaw), kOrigin);
    const Mat3x4 root = placement * turnedAboutHeight(1.0F - (2.0F * halfSine * halfSine),
                                                      2.0F * halfCosine * halfSine, kRootMoved);
    const Mat3x4 child = root * turnedAboutHeight(1.0F, 0.0F, kChildMoved);
    const std::array bones{root, child};
    const std::array byTheCpu{
        StudioInstance{.model = scene->model, .bones = bones, .light = ambientLight(100.0F)}};
    const std::vector<std::byte> expected = drawStudio(target, *scene, camera, byTheCpu);
    ASSERT_GT(pixelAt(expected, kMiddle, kMiddle)[3], 0) << "the model shows";

    const std::array poses{
        nx::render::CrowdBonePose{.position = nx::extend(kRootMoved, 0.0F),
                                  .rotation = {.z = halfSine, .w = halfCosine}},
        nx::render::CrowdBonePose{.position = nx::extend(kChildMoved, 0.0F), .rotation = {.w = 1.0F}}};
    const std::array<std::int32_t, 2> parents{-1, 0};
    const std::array sequences{nx::render::CrowdSequence{
        .poses = poses, .frameCount = 1, .reach = 64.0F, .lowest = -64.0F, .highest = 64.0F}};
    const auto loaded = nx::test::inFrameOfItsOwn(**device, [&](nx::rhi::CommandList& commands) {
        return scene->renderer.loadCrowdModel(
            commands, {.model = scene->model, .parents = parents, .sequences = sequences});
    });
    ASSERT_TRUE(loaded.has_value()) << (loaded ? "" : nx::describe(loaded.error()));
    const nx::render::CrowdBody body{.previousOrigin = kOrigin,
                                     .yaw = kYaw,
                                     .origin = kOrigin,
                                     .ambient = 100.0F,
                                     .moveShare = 1.0F,
                                     .moveSharePerTick = 0.0F};
    ASSERT_NO_FATAL_FAILURE(recordCrowdOf(**device, *scene, camera, body, 1));
    const std::vector<std::byte> byTheGpu = drawStudio(target, *scene, camera, {});
    expectNear(pixelAt(byTheGpu, kMiddle, kMiddle), pixelAt(expected, kMiddle, kMiddle),
               "as the CPU draws it");
    // Sines of two processors: a pixel on an edge may fall the other way, and no more than that.
    constexpr std::ptrdiff_t kMostPixelsOnAnEdge = 16;
    ASSERT_EQ(byTheGpu.size(), expected.size());
    constexpr std::size_t kPixelBytes = 4;
    std::ptrdiff_t different = 0;
    for (std::size_t at = 0; at + kPixelBytes <= expected.size(); at += kPixelBytes) {
        different += std::memcmp(byTheGpu.data() + at, expected.data() + at, kPixelBytes) != 0 ? 1 : 0;
    }
    EXPECT_LE(different, kMostPixelsOnAnEdge);
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(StudioRendererTest, TheTrianglesWoundCounterclockwiseAreCulledAsTheOriginalCullsThem) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
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
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
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
                       .depth = DepthLayer::ViewModel}};
    expectNear(pixelAt(drawStudio(target, *scene, camera, withTheView), kMiddle, kMiddle), litPixel(200.0F),
               "the view's own, though farther");
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(StudioRendererTest, AnAddedModelIsUnlitAndAddedToWhatIsBehind) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
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
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const auto scene = sceneOn(**device);
    ASSERT_NE(scene, nullptr);
    OffscreenTarget target{**device};
    const nx::render::Camera camera = cameraFrom(-kWoundSide, kViewDistance);
    // The same triangle twice, the far one given second. Drawn from the farthest, the far one shows
    // through the near one.
    const Vec3 behind = nx::normalized(kWoundSide) * kBehind;
    const std::vector nearBones(scene->model.boneCount(), Mat3x4{});
    const std::vector farBones(scene->model.boneCount(), nx::affine(nx::Mat3{}, behind));
    const std::array instances{blendedTriangle(*scene, nearBones, {}, kNearAmbient),
                               blendedTriangle(*scene, farBones, behind, kFarAmbient)};

    const std::vector<std::byte> pixels = drawStudio(target, *scene, camera, instances);
    for (std::size_t color = 0; color < 3; ++color) {
        EXPECT_NEAR(pixelAt(pixels, kMiddle, kMiddle).at(color), nearOverFarChannel(), 1)
            << "channel " << color;
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(StudioRendererTest, TheBlendedModelsOfTwoRenderersAreDrawnInOneOrder) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const auto first = sceneOn(**device);
    const auto second = sceneOn(**device);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    OffscreenTarget target{**device};
    const nx::render::Camera camera = cameraFrom(-kWoundSide, kViewDistance);
    // The near triangle drawn by the renderer given first, the far one by the other: drawn one renderer
    // after the other, the near one would hide the far one.
    const Vec3 behind = nx::normalized(kWoundSide) * kBehind;
    const std::vector nearBones(first->model.boneCount(), Mat3x4{});
    const std::vector farBones(second->model.boneCount(), nx::affine(nx::Mat3{}, behind));
    const std::array near{blendedTriangle(*first, nearBones, {}, kNearAmbient)};
    const std::array far{blendedTriangle(*second, farBones, behind, kFarAmbient)};

    const std::vector<std::byte> pixels = target.draw([&](nx::rhi::CommandList& list) {
        const auto preparedNear =
            first->renderer.prepare({.camera = camera, .aspect = 1.0F, .instances = near});
        const auto preparedFar =
            second->renderer.prepare({.camera = camera, .aspect = 1.0F, .instances = far});
        EXPECT_TRUE(preparedNear.has_value() && preparedFar.has_value());
        nx::render::drawInOrder(list, first->renderer, second->renderer);
    });
    for (std::size_t color = 0; color < 3; ++color) {
        EXPECT_NEAR(pixelAt(pixels, kMiddle, kMiddle).at(color), nearOverFarChannel(), 1)
            << "channel " << color;
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

} // namespace
