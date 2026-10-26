// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The enhanced look's surfaces on a triangle of solid colors, under constant skies and probes: their
// reflection measured against an integral of GGX of its own, apart from any game's content and maps.
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

using nx::Vec3;
using nx::render::ColorEncoding;
using nx::render::ProbeBlend;
using nx::render::StudioLook;
using nx::render::StudioMaterial;
using nx::render::StudioMetallicRoughness;
using nx::render::StudioPainted;
using nx::render::StudioPaintedLight;
using nx::test::OffscreenTarget;
using nx::test::Pixel;

constexpr std::uint32_t kMiddle = nx::test::kTargetExtent.width / 2;
constexpr Vec3 kTriangleMiddle{.x = 8.0F / 3.0F, .y = 8.0F / 3.0F, .z = 16.0F / 3.0F};
/// The triangle's normal, toward its seen side, which the views face or graze.
constexpr Vec3 kNormalDirection{.x = 1.0F, .y = 1.0F, .z = -1.0F};
constexpr int kSkyChannel = 180;
constexpr int kChannelTop = 255;
constexpr int kHalfChannel = 128;
constexpr double kDisplayGamma = 2.2;
/// The cosine of a grazing view to the normal.
constexpr float kGrazingCosine = 0.2F;

[[nodiscard]] auto solidImage(int channel) -> nx::assets::RgbaImage {
    return {.width = 1,
            .height = 1,
            .pixels = {static_cast<std::byte>(channel), static_cast<std::byte>(channel),
                       static_cast<std::byte>(channel), std::byte{kChannelTop}}};
}

[[nodiscard]] auto metalOf(float roughness, int channel = kHalfChannel) -> StudioMaterial {
    return {.surface = StudioMetallicRoughness{.metallic = 1.0F, .roughness = roughness},
            .baseColor = solidImage(channel)};
}

/// A fullbright texture of a solid color without a surface: the original's picture, lit wholly.
[[nodiscard]] auto fullbrightOf(int channel) -> StudioMaterial {
    return {.isFullbright = true, .baseColor = solidImage(channel)};
}

[[nodiscard]] auto cameraFrom(Vec3 direction) -> nx::render::Camera {
    constexpr float kDistance = 8.0F;
    const Vec3 way = nx::normalized(direction);
    return {.position = kTriangleMiddle + (way * kDistance), .angles = nx::vectorAngles(-way)};
}

/// A view at kGrazingCosine to the triangle's normal.
[[nodiscard]] auto grazingDirection() -> Vec3 {
    const Vec3 normal = nx::normalized(kNormalDirection);
    const Vec3 tangent = nx::normalized(Vec3{.x = 1.0F, .y = -1.0F});
    return (normal * kGrazingCosine) + (tangent * std::sqrt(1.0F - (kGrazingCosine * kGrazingCosine)));
}

struct SurfaceScene {
    nx::test::TriangleModel file;
    nx::assets::MdlView view;
    nx::render::StudioRenderer renderer;
    nx::render::StudioModel model;
};

/// How a scene's renderer draws, and whether its model has a second texture, without a material,
/// that its second skin family puts on the triangle.
struct SceneOptions {
    StudioLook look = StudioLook::Enhanced;
    ColorEncoding encoding = ColorEncoding::Display;
    bool hasSecondTexture = false;
};

[[nodiscard]] auto sceneOn(nx::rhi::Device& device, const std::optional<StudioMaterial>& material,
                           SceneOptions options = {}) -> std::unique_ptr<SurfaceScene> {
    nx::test::TriangleModel file;
    constexpr std::size_t kTextureFlagsOffset = 64;
    constexpr std::size_t kVertexCount = 3;
    constexpr std::size_t kNormalCount = 1;
    file.put(file.texture + kTextureFlagsOffset, std::uint32_t{0});
    // The fixture stores bone indices, positions, the normal's bone, then the normal: the triangle's
    // smooth normal turned to its seen side, so that views face it or graze it as the tests say.
    file.put(file.vertexBones + kVertexCount + (kVertexCount * sizeof(Vec3)) + kNormalCount,
             nx::normalized(kNormalDirection));
    if (options.hasSecondTexture) {
        constexpr std::size_t kTextureRecordSize = 80;
        constexpr std::size_t kTextureTableOffset = 180;
        const auto first = file.bytes.begin() + static_cast<std::ptrdiff_t>(file.texture);
        nx::test::Bytes records(first, first + kTextureRecordSize);
        const nx::test::Bytes second = records;
        records.insert(records.end(), second.begin(), second.end());
        file.table(kTextureTableOffset, 2, records);
        file.put(file.skins + sizeof(std::int16_t), std::int16_t{1});
    }
    auto view = nx::assets::MdlView::parse(file.bytes);
    EXPECT_TRUE(view.has_value()) << (view ? "" : nx::describe(view.error()));
    const auto executable = nx::platform::executablePath();
    EXPECT_TRUE(executable.has_value());
    if (!view || !executable) {
        return nullptr;
    }
    const auto shader = nx::platform::readFile(executable->parent_path() / "shaders" / "studio.spv");
    EXPECT_TRUE(shader.has_value());
    if (!shader) {
        return nullptr;
    }
    auto renderer = nx::test::inFrameOfItsOwn(device, [&](nx::rhi::CommandList& commands) {
        return nx::render::StudioRenderer::create(device, commands,
                                                  {.shaderCode = *shader,
                                                   .colorFormat = nx::rhi::Format::Rgba8Unorm,
                                                   .look = options.look,
                                                   .encoding = options.encoding});
    });
    EXPECT_TRUE(renderer.has_value()) << (renderer ? "" : nx::describe(renderer.error()));
    if (!renderer) {
        return nullptr;
    }
    std::vector materials{material};
    if (options.hasSecondTexture) {
        materials.emplace_back(StudioMaterial{.baseColor = solidImage(kHalfChannel)});
    }
    auto model = nx::test::inFrameOfItsOwn(device, [&](nx::rhi::CommandList& commands) {
        return renderer->load(commands, {.model = *view, .textures = *view, .materials = materials});
    });
    EXPECT_TRUE(model.has_value()) << (model ? "" : nx::describe(model.error()));
    if (!model) {
        return nullptr;
    }
    return std::make_unique<SurfaceScene>(std::move(file), std::move(*view), std::move(*renderer),
                                          std::move(*model));
}

constexpr SceneOptions kLinear{.encoding = ColorEncoding::Linear};

struct SurfaceDrawOptions {
    nx::render::RenderMode mode = nx::render::RenderMode::Normal;
    std::uint8_t amount = kChannelTop;
    std::uint32_t skin = 0;
    std::size_t layers = 1;
    Vec3 lightColor{.x = 1.0F, .y = 1.0F, .z = 1.0F};
};

void setSky(nx::rhi::Device& device, SurfaceScene& scene, int channel = kSkyChannel) {
    const std::vector sky(nx::rhi::kCubeFaces, solidImage(channel));
    const auto set = nx::test::inFrameOfItsOwn(
        device, [&](nx::rhi::CommandList& commands) { return scene.renderer.setEnvironment(commands, sky); });
    EXPECT_TRUE(set.has_value()) << (set ? "" : nx::describe(set.error()));
}

[[nodiscard]] auto drawSurface(OffscreenTarget& target, SurfaceScene& scene, float ambient = 0.0F,
                               Vec3 direction = kNormalDirection,
                               std::optional<ProbeBlend> probes = std::nullopt,
                               std::span<const nx::render::StudioLight> lights = {},
                               SurfaceDrawOptions options = {}) -> std::vector<std::byte> {
    const std::vector bones(scene.model.boneCount(), nx::Mat3x4{});
    const std::vector instances(
        options.layers,
        nx::render::StudioInstance{.model = scene.model,
                                   .bones = bones,
                                   .light = {.ambient = ambient, .shade = 0.0F, .color = options.lightColor},
                                   .skin = options.skin,
                                   .mode = options.mode,
                                   .amount = options.amount,
                                   .lights = lights,
                                   .probes = probes});
    return target.draw([&](nx::rhi::CommandList& commands) {
        const auto drawn = scene.renderer.draw(
            commands, {.camera = cameraFrom(direction), .aspect = 1.0F, .instances = instances});
        EXPECT_TRUE(drawn.has_value()) << (drawn ? "" : nx::describe(drawn.error()));
    });
}

/// Probes of one level, each a constant light on its six sides, `lights[probe]` in each channel, as the
/// renderer samples them (setProbes), in ShaderRead.
[[nodiscard]] auto constantProbes(nx::rhi::Device& device, std::initializer_list<float> lights)
    -> nx::Result<nx::rhi::OwnedTexture, nx::rhi::RhiError> {
    auto probes = device.createTexture(
        {.label = "test.constantProbes",
         .format = nx::rhi::Format::Rgba8Unorm,
         .extent = {.width = 1, .height = 1},
         .arrayLayers = nx::narrow<std::uint32_t>(lights.size()) * nx::rhi::kCubeFaces,
         .usage = {nx::rhi::TextureUsage::Sampled, nx::rhi::TextureUsage::RenderTarget}});
    if (!probes) {
        return probes;
    }
    const bool cleared = nx::test::inFrameOfItsOwn(device, [&](nx::rhi::CommandList& commands) {
        commands.transition(probes->get(), nx::rhi::TextureState::Undefined,
                            nx::rhi::TextureState::RenderTarget);
        std::uint32_t layer = 0;
        for (const float light : lights) {
            for (std::uint32_t side = 0; side < nx::rhi::kCubeFaces; ++side) {
                const std::array colors{
                    nx::rhi::ColorAttachment{.texture = probes->get(),
                                             .clear = {.red = light, .green = light, .blue = light},
                                             .arrayLayer = layer++}};
                commands.beginRendering({.colors = colors, .area = {.width = 1, .height = 1}});
                commands.endRendering();
            }
        }
        commands.transition(probes->get(), nx::rhi::TextureState::RenderTarget,
                            nx::rhi::TextureState::ShaderRead);
        return true;
    });
    EXPECT_TRUE(cleared);
    return probes;
}

[[nodiscard]] auto centerOf(std::span<const std::byte> pixels) -> Pixel {
    constexpr std::size_t kTargetBytes =
        std::size_t{nx::test::kTargetExtent.width} * nx::test::kTargetExtent.height * nx::test::kPixelBytes;
    EXPECT_EQ(pixels.size(), kTargetBytes);
    return pixels.size() == kTargetBytes ? nx::test::pixelAt(pixels, kMiddle, kMiddle) : Pixel{};
}

/// A light at the camera that views the triangle from `direction`, of `power` in each channel.
[[nodiscard]] auto lightAtTheView(Vec3 direction, float power) -> std::array<nx::render::StudioLight, 1> {
    constexpr float kReach = 100.0F;
    return {nx::render::StudioLight{.position = cameraFrom(direction).position,
                                    .reach = kReach,
                                    .color = {.x = power, .y = power, .z = power}}};
}

/// GGX's reflection (distribution, height-correlated visibility, Schlick's Fresnel, times the cosine)
/// of a surface of `roughness` and `f0` seen head on, integrated over the incoming directions evenly:
/// not by the renderer's own importance sampling.
[[nodiscard]] auto referenceReflectance(double roughness, double f0) -> double {
    constexpr std::size_t kCosineSteps = 512;
    constexpr std::size_t kAzimuthSteps = 512;
    constexpr double kSchlickPower = 5.0;
    const double alphaSquared = std::pow(roughness, 4.0);
    double integral = 0.0;
    for (std::size_t row = 0; row < kCosineSteps; ++row) {
        const double nDotL = (static_cast<double>(row) + 0.5) / kCosineSteps;
        const double sine = std::sqrt(1.0 - (nDotL * nDotL));
        for (std::size_t column = 0; column < kAzimuthSteps; ++column) {
            const double azimuth =
                2.0 * std::numbers::pi * (static_cast<double>(column) + 0.5) / kAzimuthSteps;
            const double lightX = std::cos(azimuth) * sine;
            const double lightY = std::sin(azimuth) * sine;
            const double halfZ = 1.0 + nDotL;
            const double nDotH = halfZ / std::sqrt((lightX * lightX) + (lightY * lightY) + (halfZ * halfZ));
            const double denominator = (nDotH * nDotH * (alphaSquared - 1.0)) + 1.0;
            const double distribution = alphaSquared / (std::numbers::pi * denominator * denominator);
            const double visibility =
                0.5 / (nDotL + std::sqrt((nDotL * nDotL * (1.0 - alphaSquared)) + alphaSquared));
            const double fresnel = f0 + ((1.0 - f0) * std::pow(1.0 - nDotH, kSchlickPower));
            integral += distribution * visibility * fresnel * nDotL;
        }
    }
    return integral * 2.0 * std::numbers::pi / static_cast<double>(kCosineSteps * kAzimuthSteps);
}

[[nodiscard]] auto deviceForTests() -> nx::Result<std::unique_ptr<nx::rhi::Device>, nx::rhi::RhiError> {
    return nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
}

} // namespace

TEST(EnhancedStudioTest, AMetalReflectsTheSkyWithoutDiffuseLight) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const auto scene = sceneOn(**device, metalOf(0.35F));
    ASSERT_NE(scene, nullptr);
    setSky(**device, *scene);
    OffscreenTarget target{**device};
    const Pixel dark = centerOf(drawSurface(target, *scene));
    const Pixel lit = centerOf(drawSurface(target, *scene, 255.0F));
    EXPECT_GT(dark[0], 50) << "the sky it reflects, without ambient light or lights";
    EXPECT_EQ(dark, lit) << "a metal has no diffuse light";
    EXPECT_EQ(dark[3], kChannelTop);
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(EnhancedStudioTest, AMetalReflectsAProbeWithoutASkyOrDiffuseLight) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const auto scene = sceneOn(**device, metalOf(0.35F));
    ASSERT_NE(scene, nullptr);
    auto probes = constantProbes(**device, {1.0F});
    ASSERT_TRUE(probes.has_value());
    scene->renderer.setProbes(probes->get(), 1);
    OffscreenTarget target{**device};
    const Pixel without = centerOf(drawSurface(target, *scene));
    const Pixel reflected = centerOf(drawSurface(target, *scene, 0.0F, kNormalDirection, ProbeBlend{}));
    EXPECT_EQ(without[0], 0);
    EXPECT_GT(reflected[0], 100);
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(EnhancedStudioTest, AMetalReflectsItsTwoProbesAsTheyAreShared) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const auto scene = sceneOn(**device, metalOf(0.35F));
    ASSERT_NE(scene, nullptr);
    auto probes = constantProbes(**device, {1.0F, 0.0F}); // a white probe and a black one
    ASSERT_TRUE(probes.has_value());
    scene->renderer.setProbes(probes->get(), 1);
    OffscreenTarget target{**device};
    const auto lightOf = [](const Pixel& pixel) {
        return std::pow(pixel[0] / static_cast<double>(kChannelTop), kDisplayGamma);
    };
    const double whole = lightOf(centerOf(drawSurface(
        target, *scene, 0.0F, kNormalDirection, ProbeBlend{.first = 0, .second = 1, .secondShare = 0.0F})));
    ASSERT_GT(whole, 0.1);
    for (const float share : {0.25F, 0.5F, 1.0F}) {
        SCOPED_TRACE(share);
        const double blended =
            lightOf(centerOf(drawSurface(target, *scene, 0.0F, kNormalDirection,
                                         ProbeBlend{.first = 0, .second = 1, .secondShare = share})));
        EXPECT_NEAR(blended / whole, 1.0 - static_cast<double>(share), 0.04)
            << "the second probe's share of the light, in linear light";
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(EnhancedStudioTest, ATextureWithoutASurfaceDrawsAsTheOriginalUnderLightsAndASky) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    OffscreenTarget target{**device};
    for (const std::optional<StudioMaterial>& material :
         {std::optional<StudioMaterial>{},
          std::optional{StudioMaterial{.baseColor = solidImage(kHalfChannel)}}}) {
        const auto legacy = sceneOn(**device, material, {.look = StudioLook::Legacy});
        const auto enhanced = sceneOn(**device, material);
        ASSERT_NE(legacy, nullptr);
        ASSERT_NE(enhanced, nullptr);
        setSky(**device, *enhanced, kChannelTop);
        EXPECT_EQ(drawSurface(target, *enhanced, 100.0F, kNormalDirection, std::nullopt,
                              lightAtTheView(kNormalDirection, 2000.0F)),
                  drawSurface(target, *legacy, 100.0F))
            << "neither a reflection nor highlights without a surface";
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(EnhancedStudioTest, ARoughDielectricIsShadedAsItsSurfaceSays) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const auto rough = sceneOn(
        **device, StudioMaterial{.surface = StudioMetallicRoughness{.metallic = 0.0F, .roughness = 1.0F},
                                 .baseColor = solidImage(kHalfChannel)});
    const auto original = sceneOn(**device, StudioMaterial{.baseColor = solidImage(kHalfChannel)});
    ASSERT_NE(rough, nullptr);
    ASSERT_NE(original, nullptr);
    setSky(**device, *rough, kChannelTop);
    setSky(**device, *original, kChannelTop);
    OffscreenTarget target{**device};
    const Pixel shaded = centerOf(drawSurface(target, *rough, 100.0F, grazingDirection()));
    const Pixel asTheOriginal = centerOf(drawSurface(target, *original, 100.0F, grazingDirection()));
    EXPECT_GT(shaded[0], asTheOriginal[0])
        << "metallic 0 and roughness 1 are a surface, which reflects the sky at grazing angles";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(EnhancedStudioTest, RoughnessAndGrazingAnglesChangeTheReflection) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    constexpr int kDarkChannel = 32;
    const auto smooth = sceneOn(**device, metalOf(0.08F, kDarkChannel));
    const auto rough = sceneOn(**device, metalOf(1.0F, kDarkChannel));
    ASSERT_NE(smooth, nullptr);
    ASSERT_NE(rough, nullptr);
    setSky(**device, *smooth);
    setSky(**device, *rough);
    OffscreenTarget target{**device};
    const Pixel faceOn = centerOf(drawSurface(target, *smooth));
    const Pixel atGrazing = centerOf(drawSurface(target, *smooth, 0.0F, grazingDirection()));
    const Pixel roughFace = centerOf(drawSurface(target, *rough));
    EXPECT_GT(atGrazing[0], faceOn[0] + 20) << "Schlick's reflectance rises at grazing angles";
    EXPECT_LT(roughFace[0], faceOn[0] - 5) << "rough GGX loses light to its microfacets' masking";
    EXPECT_EQ(atGrazing[3], kChannelTop);
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(EnhancedStudioTest, AConstantSkyReflectsAsAnIntegralOfGgxOfItsOwn) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const auto gamma = nx::render::textureGammaTable({});
    const double f0 = std::pow(static_cast<double>(gamma[kHalfChannel]) / kChannelTop, kDisplayGamma);
    OffscreenTarget target{**device};
    for (const float roughness : {0.55F, 1.0F}) {
        const auto scene = sceneOn(**device, metalOf(roughness));
        ASSERT_NE(scene, nullptr);
        setSky(**device, *scene);
        const Pixel actual = centerOf(drawSurface(target, *scene));
        const double expected =
            kSkyChannel *
            std::pow(referenceReflectance(static_cast<double>(roughness), f0), 1.0 / kDisplayGamma);
        EXPECT_NEAR(actual[0], expected, 3.0) << "roughness " << roughness;
        EXPECT_EQ(actual[0], actual[1]);
        EXPECT_EQ(actual[1], actual[2]);
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(EnhancedStudioTest, ASurfaceStaysWithItsTextureAcrossSkinFamilies) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const auto scene =
        sceneOn(**device, metalOf(0.35F), {.encoding = ColorEncoding::Linear, .hasSecondTexture = true});
    ASSERT_NE(scene, nullptr);
    OffscreenTarget target{**device};
    setSky(**device, *scene, 0);
    const Pixel plainBefore =
        centerOf(drawSurface(target, *scene, 100.0F, kNormalDirection, std::nullopt, {}, {.skin = 1}));
    const Pixel metalBefore = centerOf(drawSurface(target, *scene, 100.0F));
    setSky(**device, *scene, kChannelTop);
    const auto lights = lightAtTheView(kNormalDirection, 1000.0F);
    const Pixel metalAfter =
        centerOf(drawSurface(target, *scene, 100.0F, kNormalDirection, std::nullopt, lights));
    const Pixel plainAfter =
        centerOf(drawSurface(target, *scene, 100.0F, kNormalDirection, std::nullopt, lights, {.skin = 1}));
    EXPECT_GT(metalAfter[0], metalBefore[0] + 20);
    EXPECT_GT(plainBefore[0], 0);
    EXPECT_EQ(plainAfter, plainBefore) << "the metal's surface gives the other texture nothing";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(EnhancedStudioTest, AnAddedModelsBlendScalesItsLinearLightOnce) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    OffscreenTarget target{**device};
    for (const auto blend : {nx::render::StudioBlend::Opaque, nx::render::StudioBlend::Additive}) {
        const auto scene =
            sceneOn(**device, StudioMaterial{.baseColor = solidImage(kHalfChannel), .blend = blend}, kLinear);
        ASSERT_NE(scene, nullptr);
        const Pixel whole = centerOf(drawSurface(target, *scene, 0.0F, kNormalDirection, std::nullopt, {},
                                                 {.mode = nx::render::RenderMode::TransAdd}));
        const Pixel half =
            centerOf(drawSurface(target, *scene, 0.0F, kNormalDirection, std::nullopt, {},
                                 {.mode = nx::render::RenderMode::TransAdd, .amount = kHalfChannel}));
        EXPECT_GT(whole[0], 30);
        EXPECT_NEAR(half[0], whole[0] * (static_cast<double>(kHalfChannel) / kChannelTop), 1.0);
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(EnhancedStudioTest, AddedEmissionSumsAsLight) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    OffscreenTarget target{**device};
    const auto emittingOf = [](float emissive) {
        return StudioMaterial{.emissive = emissive,
                              .baseColor = solidImage(kHalfChannel),
                              .blend = nx::render::StudioBlend::Additive};
    };
    const auto one = sceneOn(**device, emittingOf(1.0F), kLinear);
    const auto two = sceneOn(**device, emittingOf(2.0F), kLinear);
    ASSERT_NE(one, nullptr);
    ASSERT_NE(two, nullptr);
    // Without light: what they emit alone.
    const Pixel doubled = centerOf(drawSurface(target, *two));
    const Pixel layered =
        centerOf(drawSurface(target, *one, 0.0F, kNormalDirection, std::nullopt, {}, {.layers = 2}));
    EXPECT_GT(doubled[0], 60);
    for (std::size_t channel = 0; channel < 3; ++channel) {
        EXPECT_NEAR(layered[channel], doubled[channel], 1);
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(EnhancedStudioTest, ColorMipsAverageLightNotBytes) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    constexpr std::uint32_t kImageSize = 256;
    nx::assets::RgbaImage checker{.width = kImageSize, .height = kImageSize};
    for (std::uint32_t y = 0; y < kImageSize; ++y) {
        for (std::uint32_t x = 0; x < kImageSize; ++x) {
            const auto channel = static_cast<std::byte>(((x + y) % 2) * kChannelTop);
            checker.pixels.insert(checker.pixels.end(), {channel, channel, channel, std::byte{kChannelTop}});
        }
    }
    const auto scene =
        sceneOn(**device, StudioMaterial{.isFullbright = true, .baseColor = std::move(checker)}, kLinear);
    ASSERT_NE(scene, nullptr);
    OffscreenTarget target{**device};
    const Pixel actual = centerOf(drawSurface(target, *scene));
    EXPECT_NEAR(actual[0], kHalfChannel, 2) << "as many black and white texels average to half the light";
    EXPECT_EQ(actual[0], actual[1]);
    EXPECT_EQ(actual[1], actual[2]);
    EXPECT_EQ(actual[3], kChannelTop);
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(EnhancedStudioTest, AFullbrightTextureTakesNeitherTheMapsLightNorItsColor) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    OffscreenTarget target{**device};
    for (const ColorEncoding encoding : {ColorEncoding::Display, ColorEncoding::Linear}) {
        const auto scene = sceneOn(**device, fullbrightOf(kHalfChannel), {.encoding = encoding});
        ASSERT_NE(scene, nullptr);
        const Pixel neutral = centerOf(drawSurface(target, *scene, 255.0F));
        const Pixel coloredDark =
            centerOf(drawSurface(target, *scene, 0.0F, kNormalDirection, std::nullopt, {},
                                 {.lightColor = {.x = 0.1F, .y = 0.5F, .z = 1.0F}}));
        EXPECT_GT(neutral[0], 30);
        EXPECT_EQ(coloredDark, neutral);
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(EnhancedStudioTest, APaintedSurfaceKeepsItsPaintingAndReflectsItsOwnColor) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    OffscreenTarget target{**device};
    StudioMaterial material = fullbrightOf(kHalfChannel);
    material.surface = StudioPainted{
        .roughness = 0.5F, .specularColor = {.x = 1.0F, .y = 0.5F, .z = 0.08F}, .specularWeight = 0.4F};
    const auto painted = sceneOn(**device, material, kLinear);
    const auto plain = sceneOn(**device, fullbrightOf(kHalfChannel), kLinear);
    ASSERT_NE(painted, nullptr);
    ASSERT_NE(plain, nullptr);
    setSky(**device, *painted, 0);
    const Pixel unreflected = centerOf(drawSurface(target, *painted));
    EXPECT_EQ(unreflected, centerOf(drawSurface(target, *plain)))
        << "the painting as it is, under a black sky";
    setSky(**device, *painted, kChannelTop);
    const Pixel reflected = centerOf(drawSurface(target, *painted));
    EXPECT_GT(reflected[0] - unreflected[0], reflected[1] - unreflected[1]);
    EXPECT_GT(reflected[1] - unreflected[1], reflected[2] - unreflected[2]);
    EXPECT_GT(reflected[2], unreflected[2]);
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(EnhancedStudioTest, APaintedLightIsSampledAsLightWithoutAReflection) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    constexpr std::array kLightChannels{64, 128, 192};
    constexpr float kGain = 0.5F;
    auto map = solidImage(kChannelTop);
    map.pixels = {std::byte{kLightChannels[0]}, std::byte{kLightChannels[1]}, std::byte{kLightChannels[2]},
                  std::byte{kChannelTop}};
    StudioMaterial material = fullbrightOf(kChannelTop);
    material.surface = StudioPainted{.specularWeight = 0.0F,
                                     .light = StudioPaintedLight{.map = std::move(map), .gain = kGain}};
    const auto scene = sceneOn(**device, material, kLinear);
    ASSERT_NE(scene, nullptr);
    OffscreenTarget target{**device};
    const Pixel actual = centerOf(drawSurface(target, *scene));
    constexpr double kSrgbOffset = 0.055;
    constexpr double kSrgbScale = 1.055;
    constexpr double kSrgbGamma = 2.4;
    for (std::size_t channel = 0; channel < kLightChannels.size(); ++channel) {
        const double light = std::pow(
            ((kLightChannels[channel] / static_cast<double>(kChannelTop)) + kSrgbOffset) / kSrgbScale,
            kSrgbGamma);
        EXPECT_NEAR(actual[channel], light * static_cast<double>(kGain) * kChannelTop, 2.0);
    }
    EXPECT_EQ(actual[3], kChannelTop);
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(EnhancedStudioTest, APaintedLightLightsItsTextureAloneAndNotWhatItEmits) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    StudioMaterial material = fullbrightOf(kHalfChannel);
    material.emissive = 1.0F;
    material.surface =
        StudioPainted{.specularWeight = 0.0F, .light = StudioPaintedLight{.map = solidImage(0)}};
    const auto scene =
        sceneOn(**device, material, {.encoding = ColorEncoding::Linear, .hasSecondTexture = true});
    const auto withoutMaterial =
        sceneOn(**device, std::nullopt, {.encoding = ColorEncoding::Linear, .hasSecondTexture = true});
    const auto emitting = sceneOn(**device, fullbrightOf(kHalfChannel), kLinear);
    ASSERT_NE(scene, nullptr);
    ASSERT_NE(withoutMaterial, nullptr);
    ASSERT_NE(emitting, nullptr);
    OffscreenTarget target{**device};
    EXPECT_EQ(drawSurface(target, *scene), drawSurface(target, *emitting))
        << "a black painted light leaves what it emits, the painting once more";
    EXPECT_EQ(drawSurface(target, *scene, 255.0F, kNormalDirection, std::nullopt, {}, {.skin = 1}),
              drawSurface(target, *withoutMaterial, 255.0F, kNormalDirection, std::nullopt, {}, {.skin = 1}))
        << "the model's other texture keeps its own material, as hands beside a gun";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(EnhancedStudioTest, ASpecularMapOfNoWeightLeavesThePaintingAtEveryAngle) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    OffscreenTarget target{**device};
    auto map = solidImage(kChannelTop);
    map.pixels[3] = std::byte{0};
    StudioMaterial material = fullbrightOf(kHalfChannel);
    material.surface = StudioPainted{.roughness = 0.3F, .specularMap = std::move(map)};
    const auto scene = sceneOn(**device, material, kLinear);
    const auto plain = sceneOn(**device, fullbrightOf(kHalfChannel), kLinear);
    ASSERT_NE(scene, nullptr);
    ASSERT_NE(plain, nullptr);
    setSky(**device, *scene, kChannelTop);
    for (const Vec3 direction : {kNormalDirection, grazingDirection()}) {
        EXPECT_EQ(
            drawSurface(target, *scene, 0.0F, direction, std::nullopt, lightAtTheView(direction, 10000.0F)),
            drawSurface(target, *plain, 0.0F, direction))
            << "no reflection of the sky, at grazing angles too, nor highlights";
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(EnhancedStudioTest, ASpecularMapGivesTheReflectionsColorAndWeightAsData) {
    auto device = deviceForTests();
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    constexpr float kRoughness = 0.55F;
    constexpr std::array kReflectanceChannels{255, 128, 20};
    OffscreenTarget target{**device};
    for (const int weight : {kHalfChannel, kChannelTop}) {
        auto map = solidImage(0);
        map.pixels = {std::byte{kReflectanceChannels[0]}, std::byte{kReflectanceChannels[1]},
                      std::byte{kReflectanceChannels[2]}, static_cast<std::byte>(weight)};
        const auto scene = sceneOn(
            **device,
            StudioMaterial{.surface = StudioPainted{.roughness = kRoughness,
                                                    .specularColor = {.x = 0.0F, .y = 0.0F, .z = 1.0F},
                                                    .specularMap = std::move(map)},
                           .baseColor = solidImage(0)},
            kLinear);
        ASSERT_NE(scene, nullptr);
        setSky(**device, *scene, kChannelTop);
        const Pixel actual = centerOf(drawSurface(target, *scene));
        for (std::size_t channel = 0; channel < kReflectanceChannels.size(); ++channel) {
            const double f0 = kReflectanceChannels[channel] / static_cast<double>(kChannelTop);
            EXPECT_NEAR(actual[channel], weight * referenceReflectance(static_cast<double>(kRoughness), f0),
                        3.0)
                << "its color and weight through neither texgamma nor sRGB, in place of specularColor";
        }
        EXPECT_GT(actual[0], actual[1]);
        EXPECT_GT(actual[1], actual[2]);
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}
