// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Sprites drawn offscreen on whatever Vulkan device the machine has (lavapipe in CI), with validation:
// sprites of 8 x 8 texels of one color built here, their origin in their middle, lit by the world of
// the BSP seed of the fuzz corpus, a box whose floor's luxel 1, 1 is 126, 133 and 140.

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

using nx::Vec3;
using nx::render::RenderMode;
using nx::render::SpriteInstance;
using nx::test::OffscreenTarget;
using nx::test::Pixel;
using nx::test::pixelAt;

constexpr std::uint32_t kMiddle = nx::test::kTargetExtent.width / 2;
constexpr std::int32_t kSize = 8;
// The sprites' palette: three colors, and the last one, transparent to an alpha-tested sprite.
constexpr std::byte kFirst{1};
constexpr std::byte kSecond{2};
constexpr std::byte kThird{3};
constexpr std::byte kLast{255};
constexpr std::array<std::array<int, 3>, 4> kColors{
    {{200, 100, 50}, {50, 200, 100}, {100, 50, 200}, {10, 20, 30}}};

// SPR v2: its orientations and formats as the file numbers them.
constexpr std::int32_t kParallel = 2;
constexpr std::int32_t kOriented = 3;
constexpr std::int32_t kNormal = 0;
constexpr std::int32_t kAdditive = 1;
constexpr std::int32_t kAlphaTest = 3;

template <class Value>
void append(std::vector<std::byte>& bytes, Value value) {
    const auto raw = std::bit_cast<std::array<std::byte, sizeof(Value)>>(value);
    bytes.insert(bytes.end(), raw.begin(), raw.end());
}

/// An image of 8 x 8 texels of `left` on its left half and `right` on its right one.
[[nodiscard]] auto imageOf(std::byte left, std::byte right) -> std::vector<std::byte> {
    std::vector<std::byte> pixels;
    for (std::int32_t y = 0; y < kSize; ++y) {
        for (std::int32_t x = 0; x < kSize; ++x) {
            pixels.push_back(x < kSize / 2 ? left : right);
        }
    }
    return pixels;
}

/// An SPR v2 file of `orientation` and `format`: one frame of `images`' first image, or a group of them
/// shown until `intervals`.
[[nodiscard]] auto spriteFile(std::int32_t orientation, std::int32_t format,
                              std::span<const std::vector<std::byte>> images,
                              std::span<const float> intervals = {}) -> std::vector<std::byte> {
    std::vector<std::byte> bytes{std::byte{'I'}, std::byte{'D'}, std::byte{'S'}, std::byte{'P'}};
    append(bytes, std::int32_t{2});
    append(bytes, orientation);
    append(bytes, format);
    append(bytes, 8.0F);
    append(bytes, kSize);
    append(bytes, kSize);
    append(bytes, std::int32_t{1}); // frames
    append(bytes, 0.0F);
    append(bytes, std::int32_t{0}); // synchronized
    append(bytes, static_cast<std::int16_t>(nx::assets::kPaletteColors));
    for (std::size_t index = 0; index < nx::assets::kPaletteColors; ++index) {
        std::array<int, 3> color{};
        if (index >= std::to_integer<std::size_t>(kFirst) && index <= std::to_integer<std::size_t>(kThird)) {
            color = kColors.at(index - 1);
        } else if (index == std::to_integer<std::size_t>(kLast)) {
            color = kColors.back();
        }
        for (const int channel : color) {
            bytes.push_back(static_cast<std::byte>(channel));
        }
    }
    const auto appendImage = [&bytes](const std::vector<std::byte>& pixels) {
        append(bytes, -kSize / 2);
        append(bytes, kSize / 2);
        append(bytes, kSize);
        append(bytes, kSize);
        bytes.insert(bytes.end(), pixels.begin(), pixels.end());
    };
    if (intervals.empty()) {
        append(bytes, std::int32_t{0});
        appendImage(images.front());
    } else {
        append(bytes, std::int32_t{1});
        append(bytes, static_cast<std::int32_t>(images.size()));
        for (const float interval : intervals) {
            append(bytes, interval);
        }
        for (const std::vector<std::byte>& pixels : images) {
            appendImage(pixels);
        }
    }
    return bytes;
}

/// An SPR v3 file, Counter-Strike Online's, of one frame of 8 x 8 pixels of BC3 blocks, all of the
/// 5-6-5 `color` and of `alpha`.
[[nodiscard]] auto compressedSpriteFile(std::uint16_t color, std::uint8_t alpha) -> std::vector<std::byte> {
    std::vector<std::byte> bytes{std::byte{'I'}, std::byte{'D'}, std::byte{'S'}, std::byte{'P'}};
    append(bytes, std::int32_t{3});
    append(bytes, kParallel);
    append(bytes, kNormal);
    append(bytes, 8.0F);
    append(bytes, kSize);
    append(bytes, kSize);
    append(bytes, std::int32_t{1}); // frames
    append(bytes, 0.0F);
    append(bytes, std::int32_t{1}); // at random
    bytes.insert(bytes.end(), {std::byte{'D'}, std::byte{'D'}, std::byte{'S'}, std::byte{' '}});
    std::vector<std::byte> header(124);
    const auto put = [&header](std::size_t offset, std::uint32_t value) {
        const auto raw = std::bit_cast<std::array<std::byte, sizeof(value)>>(value);
        std::ranges::copy(raw, header.begin() + static_cast<std::ptrdiff_t>(offset));
    };
    put(0, 124);
    put(8, static_cast<std::uint32_t>(kSize));
    put(12, static_cast<std::uint32_t>(kSize));
    put(24, 1);   // one level
    put(72, 32);  // the pixel format's size
    put(76, 0x4); // its FourCC is its format
    std::ranges::copy(std::array{std::byte{'D'}, std::byte{'X'}, std::byte{'T'}, std::byte{'5'}},
                      header.begin() + 80);
    bytes.insert(bytes.end(), header.begin(), header.end());
    for (std::size_t block = 0; block < 4; ++block) {
        bytes.insert(bytes.end(), {std::byte{alpha}, std::byte{alpha}});
        bytes.insert(bytes.end(), 6, std::byte{0}); // every pixel the first alpha
        append(bytes, color);
        append(bytes, color);
        append(bytes, std::uint32_t{0}); // and the first color
    }
    return bytes;
}

[[nodiscard]] auto readFileOf(const std::filesystem::path& path) -> std::vector<std::byte> {
    auto bytes = nx::platform::readFile(path);
    EXPECT_TRUE(bytes.has_value() && !bytes->empty()) << path.string();
    return bytes.value_or(std::vector<std::byte>{});
}

/// The renderers, of the world's sprites and of the view model's, the box's light, and the sprites drawn.
class Scene {
public:
    explicit Scene(nx::rhi::Device& device,
                   nx::render::ColorEncoding encoding = nx::render::ColorEncoding::Display)
        : m_device{&device},
          m_mapBytes{readFileOf(std::filesystem::path{NX_FUZZ_CORPUS_DIRECTORY} / "bsp" / "boxEmbedded.bsp")},
          m_map{nx::assets::BspView::parse(m_mapBytes).value()}, m_light{m_map} {
        const auto executable = nx::platform::executablePath();
        const std::vector<std::byte> shader =
            executable ? readFileOf(executable->parent_path() / "shaders" / "sprite.spv")
                       : std::vector<std::byte>{};
        for (std::optional<nx::render::SpriteRenderer>* const made : {&m_renderer, &m_viewRenderer}) {
            auto renderer = nx::render::SpriteRenderer::create(
                device,
                {.shaderCode = shader, .colorFormat = nx::rhi::Format::Rgba8Unorm, .encoding = encoding});
            EXPECT_TRUE(renderer.has_value()) << (renderer ? "" : nx::describe(renderer.error()));
            if (renderer) {
                made->emplace(std::move(*renderer));
            }
        }
    }

    /// `file` on the GPU, for the world's renderer.
    [[nodiscard]] auto load(const std::vector<std::byte>& file) -> nx::render::SpriteModel {
        return loadOn(*m_renderer, file);
    }
    /// `file` on the GPU, for the view model's.
    [[nodiscard]] auto loadInView(const std::vector<std::byte>& file) -> nx::render::SpriteModel {
        return loadOn(*m_viewRenderer, file);
    }

    /// The pixels of `instances` seen by `camera` at `time`, the box's style 0 at 'm'.
    [[nodiscard]] auto draw(OffscreenTarget& target, const nx::render::Camera& camera,
                            std::span<const SpriteInstance> instances, nx::Seconds time = nx::Seconds{0.0F},
                            const std::optional<nx::render::Fog>& fog = std::nullopt)
        -> std::vector<std::byte> {
        nx::render::LightStyles styles;
        styles.set(0, "m");
        return target.draw([&](nx::rhi::CommandList& list) {
            const auto drawn = m_renderer->draw(list,
                                                {.camera = camera,
                                                 .aspect = 1.0F,
                                                 .time = time,
                                                 .lightStyles = styles.valuesAt(nx::Seconds{0.0F}),
                                                 .instances = instances,
                                                 .fog = fog},
                                                m_light);
            EXPECT_TRUE(drawn.has_value()) << (drawn ? "" : nx::describe(drawn.error()));
        });
    }

    /// The pixels of `world`'s sprites then `view`'s, in the view model's depths, seen by `camera`.
    [[nodiscard]] auto drawWithView(OffscreenTarget& target, const nx::render::Camera& camera,
                                    std::span<const SpriteInstance> world,
                                    std::span<const SpriteInstance> view) -> std::vector<std::byte> {
        return target.draw([&](nx::rhi::CommandList& list) {
            for (const auto& [renderer, instances, depth] :
                 {std::tuple{&*m_renderer, world, nx::render::DepthLayer::World},
                  std::tuple{&*m_viewRenderer, view, nx::render::DepthLayer::ViewModel}}) {
                const auto drawn = renderer->draw(
                    list, {.camera = camera, .aspect = 1.0F, .instances = instances, .depth = depth},
                    m_light);
                EXPECT_TRUE(drawn.has_value()) << (drawn ? "" : nx::describe(drawn.error()));
            }
        });
    }

    [[nodiscard]] auto isReady() const noexcept -> bool { return m_renderer && m_viewRenderer; }

private:
    [[nodiscard]] auto loadOn(nx::render::SpriteRenderer& renderer, const std::vector<std::byte>& file)
        -> nx::render::SpriteModel {
        auto view = nx::assets::SpriteView::parse(file);
        EXPECT_TRUE(view.has_value()) << (view ? "" : nx::describe(view.error()));
        auto model = nx::test::inFrameOfItsOwn(*m_device, [&](nx::rhi::CommandList& commands) {
            return renderer.load(commands, *view, "test");
        });
        EXPECT_TRUE(model.has_value()) << (model ? "" : nx::describe(model.error()));
        return std::move(model).value();
    }

    nx::rhi::Device* m_device;
    std::vector<std::byte> m_mapBytes;
    nx::assets::BspView m_map;
    nx::render::WorldLight m_light;
    std::optional<nx::render::SpriteRenderer> m_renderer;
    std::optional<nx::render::SpriteRenderer> m_viewRenderer;
};

/// A camera at `eye` looking along +X.
[[nodiscard]] auto cameraAt(Vec3 eye) -> nx::render::Camera {
    return {.position = eye};
}

/// `value` of 255 times `share`, as a byte.
[[nodiscard]] auto scaled(int value, float share) -> int {
    return static_cast<int>(std::lround(static_cast<float>(value) * share));
}

void expectNear(const Pixel& actual, std::array<int, 3> expected, std::string_view what = {}) {
    for (std::size_t channel = 0; channel < expected.size(); ++channel) {
        EXPECT_NEAR(actual.at(channel), expected.at(channel), 1) << what << ", channel " << channel;
    }
}

TEST(SpriteRendererTest, AnAddedSpriteAddsItsImageTimesItsColorAndAmount) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    Scene scene{**device};
    ASSERT_TRUE(scene.isReady());
    OffscreenTarget target{**device};
    const std::array images{imageOf(kFirst, kFirst)};
    const nx::render::SpriteModel model = scene.load(spriteFile(kParallel, kAdditive, images));
    const std::array added{SpriteInstance{.model = model,
                                          .origin = {.x = 20.0F},
                                          .mode = RenderMode::TransAdd,
                                          .amount = 128,
                                          .color = {.red = 255, .green = 128, .blue = 0}}};

    const auto gamma = nx::render::textureGammaTable({});
    const float amount = 128.0F / 255.0F;
    expectNear(pixelAt(scene.draw(target, cameraAt({}), added), kMiddle, kMiddle),
               {scaled(gamma[200], amount), scaled(gamma[100], 128.0F / 255.0F * amount), 0},
               "texgamma'd, times its color and its amount, added to black");
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(SpriteRendererTest, ASpriteIsSeenThroughTheFogButOneAddedIsNot) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    Scene scene{**device};
    ASSERT_TRUE(scene.isReady());
    OffscreenTarget target{**device};
    const std::array images{imageOf(kFirst, kFirst)};
    // 20 units ahead along the view, a GL_EXP2 fog hiding half of it: (density 20)^2 = ln 2.
    constexpr float kAhead = 20.0F;
    const nx::render::Fog blue{.color = {.blue = 1.0F},
                               .curve = nx::render::FogCurve::ExponentialSquared,
                               .density = std::sqrt(std::numbers::ln2_v<float>) / kAhead};
    const nx::render::SpriteModel solidModel = scene.load(spriteFile(kParallel, kNormal, images));
    const std::array solid{
        SpriteInstance{
            .model = solidModel,
            .origin = {.x = kAhead},
            .mode = RenderMode::Normal,
        },
    };
    const Pixel seen = pixelAt(scene.draw(target, cameraAt({}), solid), kMiddle, kMiddle);
    const Pixel fogged =
        pixelAt(scene.draw(target, cameraAt({}), solid, nx::Seconds{0.0F}, blue), kMiddle, kMiddle);
    ASSERT_EQ(seen.at(3), 255) << "the sprite is visible without fog";
    ASSERT_EQ(fogged.at(3), 255) << "the sprite is visible through the fog";
    constexpr std::array kFogColor{0.0, 0.0, 255.0};
    for (std::size_t channel = 0; channel < kFogColor.size(); ++channel) {
        EXPECT_NEAR(fogged[channel], std::lerp(static_cast<double>(seen[channel]), kFogColor[channel], 0.5),
                    2.0)
            << "half hidden, channel " << channel;
    }
    const nx::render::SpriteModel addedModel = scene.load(spriteFile(kParallel, kAdditive, images));
    const std::array added{
        SpriteInstance{
            .model = addedModel,
            .origin = {.x = kAhead},
            .mode = RenderMode::TransAdd,
            .amount = 255,
        },
    };
    const auto addedClear = scene.draw(target, cameraAt({}), added);
    const auto addedFogged = scene.draw(target, cameraAt({}), added, nx::Seconds{0.0F}, blue);
    ASSERT_GT(pixelAt(addedClear, kMiddle, kMiddle).at(0), 0) << "the added sprite is visible";
    EXPECT_EQ(addedFogged, addedClear) << "what is added, through no fog";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(SpriteRendererTest, AnImageOfBc3BlocksIsDrawnAsItsAuthorMadeItItsAlphaItsOwn) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    Scene scene{**device};
    ASSERT_TRUE(scene.isReady());
    OffscreenTarget target{**device};
    // 0x8410: 132, 130 and 132; an alpha of 128.
    const nx::render::SpriteModel model = scene.load(compressedSpriteFile(0x8410, 128));
    const std::array added{
        SpriteInstance{.model = model, .origin = {.x = 20.0F}, .mode = RenderMode::TransAdd, .amount = 255}};

    const float alpha = 128.0F / 255.0F;
    expectNear(pixelAt(scene.draw(target, cameraAt({}), added), kMiddle, kMiddle),
               {scaled(132, alpha), scaled(130, alpha), scaled(132, alpha)},
               "no texgamma, a palette's, and added as its alpha says");
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(SpriteRendererTest, ABrightnessMultipliesTheLightASpriteAdds) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    Scene scene{**device, nx::render::ColorEncoding::Linear};
    ASSERT_TRUE(scene.isReady());
    OffscreenTarget target{**device};
    const std::array images{imageOf(kFirst, kFirst)};
    const nx::render::SpriteModel model = scene.load(spriteFile(kParallel, kAdditive, images));
    SpriteInstance instance{
        .model = model, .origin = {.x = 20.0F}, .mode = RenderMode::TransAdd, .amount = 64};
    const Pixel once = pixelAt(scene.draw(target, cameraAt({}), std::array{instance}), kMiddle, kMiddle);
    // A target of bytes holds a fragment's color to 1 before it is added (the enhanced look's, of half
    // floats, does not): a brightness that keeps the brightest channel, about 0.64, below.
    constexpr float kBrightness = 1.5F;
    instance.brightness = kBrightness;
    const Pixel brighter = pixelAt(scene.draw(target, cameraAt({}), std::array{instance}), kMiddle, kMiddle);
    for (std::size_t channel = 0; channel < 3; ++channel) {
        EXPECT_GT(once.at(channel), 0) << "channel " << channel;
        EXPECT_NEAR(brighter.at(channel), static_cast<float>(once.at(channel)) * kBrightness, 2.0F)
            << "channel " << channel;
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(SpriteRendererTest, AParallelSpriteFacesTheViewAndAnOrientedOneIsCulledFromBehind) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    Scene scene{**device};
    ASSERT_TRUE(scene.isReady());
    OffscreenTarget target{**device};
    const std::array images{imageOf(kFirst, kFirst)};
    const nx::render::SpriteModel parallel = scene.load(spriteFile(kParallel, kNormal, images));
    const nx::render::SpriteModel oriented = scene.load(spriteFile(kOriented, kNormal, images));

    for (const float yaw : {0.0F, 90.0F, 200.0F}) {
        const nx::render::Camera camera{.angles = {.yaw = nx::Degrees{yaw}}};
        const Vec3 ahead = nx::angleVectors(camera.angles).forward * 20.0F;
        const std::array facing{SpriteInstance{.model = parallel, .origin = ahead}};
        EXPECT_EQ(pixelAt(scene.draw(target, camera, facing), kMiddle, kMiddle)[3], 255) << "yaw " << yaw;
    }
    // Turned to +X, it shows to a view that looks that way too, and its back is culled.
    const std::array turned{SpriteInstance{.model = oriented, .origin = {.x = 20.0F}}};
    EXPECT_EQ(pixelAt(scene.draw(target, cameraAt({}), turned), kMiddle, kMiddle)[3], 255);
    const nx::render::Camera behind{.position = {.x = 40.0F}, .angles = {.yaw = nx::Degrees{180.0F}}};
    EXPECT_EQ(pixelAt(scene.draw(target, behind, turned), kMiddle, kMiddle)[3], 0) << "culled from behind";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(SpriteRendererTest, AGlowShowsOverWhatIsInFrontUnlessTheGameHidesIt) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    Scene scene{**device};
    ASSERT_TRUE(scene.isReady());
    OffscreenTarget target{**device};
    const std::array images{imageOf(kFirst, kFirst)};
    const nx::render::SpriteModel wall = scene.load(spriteFile(kParallel, kNormal, images));
    const nx::render::SpriteModel glow = scene.load(spriteFile(kParallel, kAdditive, images));
    const auto gamma = nx::render::textureGammaTable({});
    // The wall, 10 units ahead and scaled to fill the view; the glow 200 units behind it, 25 times its
    // size, which the original dims to 19000 / 200^2 and scales by 200 / 200.
    const SpriteInstance wallInstance{.model = wall, .origin = {.x = 10.0F}, .scale = 4.0F};
    const SpriteInstance glowInstance{.model = glow,
                                      .origin = {.x = 200.0F},
                                      .scale = 25.0F,
                                      .mode = RenderMode::Glow,
                                      .amount = 255,
                                      .color = {.red = 0, .green = 0, .blue = 255}};
    const float dimmed = 19000.0F / (200.0F * 200.0F);

    const std::array seen{wallInstance, glowInstance};
    expectNear(pixelAt(scene.draw(target, cameraAt({}), seen), kMiddle, kMiddle),
               {gamma[200], gamma[100], gamma[50] + scaled(gamma[50], dimmed)}, "added over the wall");

    SpriteInstance hidden = glowInstance;
    hidden.isHidden = true;
    const std::array blocked{wallInstance, hidden};
    expectNear(pixelAt(scene.draw(target, cameraAt({}), blocked), kMiddle, kMiddle),
               {gamma[200], gamma[100], gamma[50]}, "hidden by the game");

    SpriteInstance undissipated = glowInstance;
    undissipated.effect = nx::render::RenderEffect::NoDissipation;
    const std::array bright{undissipated};
    expectNear(pixelAt(scene.draw(target, cameraAt({}), bright), kMiddle, kMiddle), {0, 0, gamma[50]},
               "NoDissipation: as bright at any distance");
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(SpriteRendererTest, ASpriteInTheViewModelsDepthsIsNotHiddenByAWallNearerThanIt) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    Scene scene{**device};
    ASSERT_TRUE(scene.isReady());
    OffscreenTarget target{**device};
    const std::array images{imageOf(kFirst, kFirst)};
    const nx::render::SpriteModel wall = scene.load(spriteFile(kParallel, kNormal, images));
    const nx::render::SpriteModel flash = scene.loadInView(spriteFile(kParallel, kAdditive, images));
    const auto gamma = nx::render::textureGammaTable({});
    // A wall 16 units ahead, filling the view; a flash 30 units ahead, behind it, added in blue.
    const std::array walls{SpriteInstance{.model = wall, .origin = {.x = 16.0F}, .scale = 6.0F}};
    const std::array flashes{SpriteInstance{.model = flash,
                                            .origin = {.x = 30.0F},
                                            .mode = RenderMode::TransAdd,
                                            .amount = 255,
                                            .color = {.red = 0, .green = 0, .blue = 255}}};
    expectNear(pixelAt(scene.drawWithView(target, cameraAt({}), walls, flashes), kMiddle, kMiddle),
               {gamma[200], gamma[100], gamma[50] + gamma[50]}, "added over the wall in front of it");
    expectNear(pixelAt(scene.draw(target, cameraAt({}), std::array{walls[0], flashes[0]}), kMiddle, kMiddle),
               {gamma[200], gamma[100], gamma[50]}, "among the world's, the wall hides it");
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(SpriteRendererTest, AnAlphaTestedSpriteIsCutAndLitByTheWorld) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    Scene scene{**device};
    ASSERT_TRUE(scene.isReady());
    OffscreenTarget target{**device};
    // Its left half the transparent index: the right half is seen right of the middle.
    const std::array images{imageOf(kLast, kFirst)};
    const nx::render::SpriteModel model = scene.load(spriteFile(kParallel, kAlphaTest, images));
    const std::array lit{SpriteInstance{.model = model,
                                        .origin = {.x = 8.0F, .y = 8.0F, .z = 8.0F},
                                        .mode = RenderMode::TransTexture,
                                        .amount = 255}};
    const nx::render::Camera camera{.position = {.x = 2.0F, .y = 8.0F, .z = 8.0F}};

    const std::vector<std::byte> pixels = scene.draw(target, camera, lit);
    EXPECT_EQ(pixelAt(pixels, kMiddle - 4, kMiddle)[3], 0) << "palette index 255 is cut";
    // Its own palette, not texgamma'd, times the floor's light below it: 129, 137 and 144.
    expectNear(pixelAt(pixels, kMiddle + 4, kMiddle),
               {scaled(200, 129.0F / 255.0F), scaled(100, 137.0F / 255.0F), scaled(50, 144.0F / 255.0F)});
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(SpriteRendererTest, AGroupShowsItsImagesAsItsLoopGoes) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    Scene scene{**device};
    ASSERT_TRUE(scene.isReady());
    OffscreenTarget target{**device};
    const std::array images{imageOf(kFirst, kFirst), imageOf(kSecond, kSecond), imageOf(kThird, kThird)};
    const std::array intervals{0.1F, 0.2F, 0.35F};
    const nx::render::SpriteModel model = scene.load(spriteFile(kParallel, kNormal, images, intervals));
    const std::array shown{SpriteInstance{.model = model, .origin = {.x = 20.0F}}};
    const auto gamma = nx::render::textureGammaTable({});

    for (const auto& [seconds, color] :
         {std::pair{0.05F, 0}, std::pair{0.15F, 1}, std::pair{0.3F, 2}, std::pair{0.4F, 0}}) {
        const auto& expected = kColors.at(static_cast<std::size_t>(color));
        expectNear(pixelAt(scene.draw(target, cameraAt({}), shown, nx::Seconds{seconds}), kMiddle, kMiddle),
                   {gamma[static_cast<std::size_t>(expected[0])],
                    gamma[static_cast<std::size_t>(expected[1])],
                    gamma[static_cast<std::size_t>(expected[2])]},
                   std::format("{} seconds in", seconds));
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(SpriteRendererTest, DrawnAsLinearLightAnAddedSpriteAddsItsLightTimesItsAmountOnce) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    Scene scene{**device, nx::render::ColorEncoding::Linear};
    ASSERT_TRUE(scene.isReady());
    OffscreenTarget target{**device};
    const std::array images{imageOf(kFirst, kFirst)};
    const nx::render::SpriteModel model = scene.load(spriteFile(kParallel, kAdditive, images));
    const SpriteInstance instance{.model = model,
                                  .origin = {.x = 20.0F},
                                  .mode = RenderMode::TransAdd,
                                  .amount = 128,
                                  .color = {.red = 255, .green = 128, .blue = 0}};
    const std::array added{instance, instance};
    const Pixel actual = pixelAt(scene.draw(target, cameraAt({}), added), kMiddle, kMiddle);
    const auto gamma = nx::render::textureGammaTable({});
    constexpr double kDisplayGamma = 2.2;
    constexpr double kChannelTop = 255.0;
    constexpr double kAmount = 128.0 / kChannelTop;
    const std::array tint{1.0, kAmount, 0.0};
    for (std::size_t channel = 0; channel < tint.size(); ++channel) {
        const double source =
            static_cast<double>(gamma[static_cast<std::size_t>(kColors[0][channel])]) / kChannelTop;
        const double expected = 2.0 * std::pow(source, kDisplayGamma) *
                                std::pow(tint[channel], kDisplayGamma) * kAmount * kChannelTop;
        EXPECT_NEAR(actual[channel], expected, 2.0) << "channel " << channel;
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(SpriteRendererTest, DrawnAsLinearLightABlendedSpriteMixesTheLightOfBoth) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    Scene scene{**device, nx::render::ColorEncoding::Linear};
    ASSERT_TRUE(scene.isReady());
    OffscreenTarget target{**device};
    const std::array foregroundImages{imageOf(kFirst, kFirst)};
    const std::array backgroundImages{imageOf(kSecond, kSecond)};
    const nx::render::SpriteModel foreground = scene.load(spriteFile(kParallel, kNormal, foregroundImages));
    const nx::render::SpriteModel background = scene.load(spriteFile(kParallel, kNormal, backgroundImages));
    const std::array shown{
        SpriteInstance{.model = background, .origin = {.x = 24.0F}},
        SpriteInstance{
            .model = foreground, .origin = {.x = 20.0F}, .mode = RenderMode::TransTexture, .amount = 128}};
    const Pixel actual = pixelAt(scene.draw(target, cameraAt({}), shown), kMiddle, kMiddle);
    const auto gamma = nx::render::textureGammaTable({});
    constexpr double kDisplayGamma = 2.2;
    constexpr double kChannelTop = 255.0;
    constexpr double kAmount = 128.0 / kChannelTop;
    for (std::size_t channel = 0; channel < kColors[0].size(); ++channel) {
        const auto linear = [&](std::size_t color) {
            const auto index = static_cast<std::size_t>(kColors[color][channel]);
            return std::pow(static_cast<double>(gamma[index]) / kChannelTop, kDisplayGamma);
        };
        const double expected = (linear(0) * kAmount + linear(1) * (1.0 - kAmount)) * kChannelTop;
        EXPECT_NEAR(actual[channel], expected, 2.0) << "channel " << channel;
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(SpriteRendererTest, DrawnAsLinearLightALitSpriteTakesTheWorldsLightAsLight) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    Scene scene{**device, nx::render::ColorEncoding::Linear};
    ASSERT_TRUE(scene.isReady());
    OffscreenTarget target{**device};
    const std::array images{imageOf(kLast, kFirst)};
    const nx::render::SpriteModel model = scene.load(spriteFile(kParallel, kAlphaTest, images));
    const std::array lit{SpriteInstance{.model = model,
                                        .origin = {.x = 8.0F, .y = 8.0F, .z = 8.0F},
                                        .mode = RenderMode::TransTexture,
                                        .amount = 255}};
    const nx::render::Camera camera{.position = {.x = 2.0F, .y = 8.0F, .z = 8.0F}};
    const auto pixels = scene.draw(target, camera, lit);
    EXPECT_EQ(pixelAt(pixels, kMiddle - 4, kMiddle)[3], 0);
    const Pixel actual = pixelAt(pixels, kMiddle + 4, kMiddle);
    constexpr std::array kFloorLight{129.0, 137.0, 144.0};
    constexpr double kDisplayGamma = 2.2;
    constexpr double kChannelTop = 255.0;
    for (std::size_t channel = 0; channel < kFloorLight.size(); ++channel) {
        const double source = static_cast<double>(kColors[0][channel]) / kChannelTop;
        const double light = kFloorLight[channel] / kChannelTop;
        const double expected =
            std::pow(source, kDisplayGamma) * std::pow(light, kDisplayGamma) * kChannelTop;
        EXPECT_NEAR(actual[channel], expected, 2.0) << "channel " << channel;
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

} // namespace
