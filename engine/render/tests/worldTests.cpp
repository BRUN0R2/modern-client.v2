// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The world of a small map, drawn offscreen on whatever Vulkan device the machine has (lavapipe in
// CI), with validation: the BSP seed of the fuzz corpus, a 64-unit box. Three of its walls have
// CRATE01, embedded; the other three {BLUE, whose pixels would be in a WAD.

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

using nx::rhi::Format;
using nx::test::OffscreenTarget;
using nx::test::Pixel;
using nx::test::pixelAt;

constexpr nx::rhi::Extent2d kExtent = nx::test::kTargetExtent;
/// The middle of the box.
constexpr nx::Vec3 kMiddle{.x = 32.0F, .y = 32.0F, .z = 32.0F};

[[nodiscard]] auto readSeed(std::string_view name = "boxEmbedded.bsp") -> std::vector<std::byte> {
    auto bytes = nx::platform::readFile(std::filesystem::path{NX_FUZZ_CORPUS_DIRECTORY} / "bsp" / name);
    EXPECT_TRUE(bytes.has_value()) << nx::describe(bytes.error());
    return bytes.value_or(std::vector<std::byte>{});
}

[[nodiscard]] auto worldShader() -> std::vector<std::byte> {
    const auto executable = nx::platform::executablePath();
    auto code = executable ? nx::platform::readFile(executable->parent_path() / "shaders" / "world.spv")
                           : nx::Result<std::vector<std::byte>, nx::platform::FileError>{};
    EXPECT_TRUE(code.has_value() && !code->empty()) << "bin/shaders/world.spv";
    return code.value_or(std::vector<std::byte>{});
}

/// Renames the texture `name` of the map in `file` to `to`: its 16-byte name field.
void renameTexture(std::vector<std::byte>& file, std::string_view name, std::string_view to) {
    const auto bytes = std::as_bytes(std::span{name});
    const auto found = std::ranges::search(file, bytes);
    ASSERT_FALSE(found.empty()) << name;
    const auto field = std::span{file}.subspan(static_cast<std::size_t>(found.begin() - file.begin()), 16);
    std::ranges::fill(field, std::byte{0});
    std::ranges::copy(std::as_bytes(std::span{to}), field.begin());
}

/// The styles as a map starts: the world's style 0 at 'm', the normal brightness.
[[nodiscard]] auto normalStyles() -> nx::render::LightStyleValues {
    nx::render::LightStyles styles;
    styles.set(0, "m");
    return styles.valuesAt(nx::Seconds{0.0F});
}

/// Draws `world` as `camera` sees it with `styles`, `time` after the map started and its brush models
/// where `placements` puts them, on `target`, an alpha of 0 where it draws nothing.
[[nodiscard]] auto
drawWorld(OffscreenTarget& target, nx::render::BspWorld& world, const nx::render::Camera& camera,
          const nx::render::LightStyleValues& styles = normalStyles(), nx::Seconds time = nx::Seconds{0.0F},
          std::span<const nx::render::ModelPlacement> placements = {}) -> std::vector<std::byte> {
    return target.draw([&](nx::rhi::CommandList& list) {
        world.draw(list, {.camera = camera,
                          .aspect = 1.0F,
                          .time = time,
                          .lightStyles = styles,
                          .placements = placements});
    });
}

/// Builds the world of `desc` in a frame of its own.
[[nodiscard]] auto createWorld(nx::rhi::Device& device, const nx::render::BspWorldDesc& desc)
    -> nx::Result<nx::render::BspWorld, nx::render::WorldError> {
    auto commands = device.beginFrame();
    if (!commands) {
        return nx::fail(nx::render::WorldError::GpuFailed, "beginFrame", std::move(commands.error()));
    }
    auto world = nx::render::BspWorld::create(device, commands->get(), desc);
    EXPECT_TRUE(device.submitFrame().has_value());
    device.waitIdle();
    return world;
}

TEST(BspWorldTest, DrawsTheFacesOfAMapAroundTheCamera) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const std::vector<std::byte> file = readSeed();
    const auto map = nx::assets::BspView::parse(file);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    const std::vector<std::byte> shader = worldShader();
    OffscreenTarget target{**device};

    auto world =
        createWorld(**device, {.map = *map, .shaderCode = shader, .colorFormat = Format::Rgba8Unorm});
    ASSERT_TRUE(world.has_value()) << nx::describe(world.error());
    EXPECT_EQ(world->stats().faces, 6U);
    EXPECT_EQ(world->stats().triangles, 12U);
    EXPECT_EQ(world->stats().missingTextures, std::vector<std::string>{"{BLUE"}) << "no WAD was given";

    // Looking along -X at a wall of CRATE01.
    const std::vector<std::byte> pixels =
        drawWorld(target, *world, {.position = kMiddle, .angles = {.yaw = nx::Degrees{180.0F}}});
    ASSERT_EQ(pixels.size(), std::size_t{kExtent.width} * kExtent.height * 4);
    const auto drawn = static_cast<std::size_t>(std::ranges::count_if(
        std::views::iota(std::size_t{0}, pixels.size() / 4),
        [&pixels](std::size_t pixel) { return pixels[(pixel * 4) + 3] == std::byte{255}; }));
    EXPECT_EQ(drawn, std::size_t{kExtent.width} * kExtent.height) << "a wall covers the whole view";
    // The wall's lightmap rises from 2 to 255: nearly every pixel is lit, so it has color.
    const auto lit = static_cast<std::size_t>(std::ranges::count_if(
        std::views::iota(std::size_t{0}, pixels.size() / 4), [&pixels](std::size_t pixel) {
            return pixels[pixel * 4] != std::byte{0} || pixels[(pixel * 4) + 1] != std::byte{0} ||
                   pixels[(pixel * 4) + 2] != std::byte{0};
        }));
    EXPECT_GE(lit * 10, drawn * 9) << "at least 90% of the wall lit";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

/// The sum of the red, green and blue of every pixel.
[[nodiscard]] auto brightnessOf(std::span<const std::byte> pixels) -> std::uint64_t {
    std::uint64_t sum = 0;
    for (std::size_t pixel = 0; pixel < pixels.size() / 4; ++pixel) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            sum += std::to_integer<std::uint64_t>(pixels[(pixel * 4) + channel]);
        }
    }
    return sum;
}

TEST(BspWorldTest, AFacesLightIsAsBrightAsItsStyle) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const std::vector<std::byte> file = readSeed();
    const auto map = nx::assets::BspView::parse(file);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    const std::vector<std::byte> shader = worldShader();
    OffscreenTarget target{**device};
    auto world =
        createWorld(**device, {.map = *map, .shaderCode = shader, .colorFormat = Format::Rgba8Unorm});
    ASSERT_TRUE(world.has_value()) << nx::describe(world.error());

    // The wall of CRATE01 again, its lightmap of style 0 at 'a', 'm' and 'z'.
    const nx::render::Camera camera{.position = kMiddle, .angles = {.yaw = nx::Degrees{180.0F}}};
    nx::render::LightStyles styles;
    const auto drawnAt = [&](std::string_view pattern) {
        styles.set(0, pattern);
        return brightnessOf(drawWorld(target, *world, camera, styles.valuesAt(nx::Seconds{0.0F})));
    };
    EXPECT_EQ(drawnAt("a"), 0U) << "dark";
    const std::uint64_t normal = drawnAt("m");
    EXPECT_GT(normal, 0U);
    EXPECT_GT(drawnAt("z"), normal) << "brighter than normal";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}
TEST(BspWorldTest, DrawsOnlyWhatTheCameraSees) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const std::vector<std::byte> file = readSeed();
    const auto map = nx::assets::BspView::parse(file);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    const std::vector<std::byte> shader = worldShader();
    OffscreenTarget target{**device};
    auto world =
        createWorld(**device, {.map = *map, .shaderCode = shader, .colorFormat = Format::Rgba8Unorm});
    ASSERT_TRUE(world.has_value()) << nx::describe(world.error());

    std::ignore = drawWorld(target, *world, {.position = kMiddle});
    EXPECT_EQ(world->lastFrame().faces, 6U) << "inside: every wall faces the camera";
    EXPECT_EQ(world->lastFrame().triangles, 12U);

    // Outside, past the -X wall: it turns its back, the five others face this way.
    std::ignore = drawWorld(target, *world, {.position = {.x = -100.0F, .y = 32.0F, .z = 32.0F}});
    EXPECT_EQ(world->lastFrame().faces, 5U);

    const std::vector<std::byte> away = drawWorld(
        target, *world,
        {.position = {.x = -100.0F, .y = 32.0F, .z = 32.0F}, .angles = {.yaw = nx::Degrees{180.0F}}});
    EXPECT_EQ(world->lastFrame().leaves, 0U) << "the box is behind the camera";
    EXPECT_EQ(world->lastFrame().faces, 0U);
    EXPECT_TRUE(
        std::ranges::all_of(std::views::iota(std::size_t{0}, away.size() / 4),
                            [&away](std::size_t pixel) { return away[(pixel * 4) + 3] == std::byte{0}; }))
        << "nothing drawn";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}
TEST(BspWorldTest, AnAnimatedTextureShowsItsFramesTenASecond) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    // Frame 0 the crate, frame 1 a texture no WAD has: the checkerboard, magenta and black.
    std::vector<std::byte> file = readSeed();
    renameTexture(file, "CRATE01", "+0crate");
    renameTexture(file, "{BLUE", "+1crate");
    const auto map = nx::assets::BspView::parse(file);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    const std::vector<std::byte> shader = worldShader();
    OffscreenTarget target{**device};
    auto world =
        createWorld(**device, {.map = *map, .shaderCode = shader, .colorFormat = Format::Rgba8Unorm});
    ASSERT_TRUE(world.has_value()) << nx::describe(world.error());
    EXPECT_EQ(world->stats().missingTextures, std::vector<std::string>{"+1crate"});

    const auto greenPixelsAt = [&](float seconds) {
        const std::vector<std::byte> pixels =
            drawWorld(target, *world, {.position = kMiddle, .angles = {.yaw = nx::Degrees{180.0F}}},
                      normalStyles(), nx::Seconds{seconds});
        return std::ranges::count_if(
            std::views::iota(std::size_t{0}, pixels.size() / 4),
            [&pixels](std::size_t pixel) { return pixels[(pixel * 4) + 1] != std::byte{0}; });
    };
    EXPECT_GT(greenPixelsAt(0.05F), 0) << "frame 0: the crate";
    EXPECT_EQ(greenPixelsAt(0.15F), 0) << "frame 1: the checkerboard has no green";
    EXPECT_GT(greenPixelsAt(0.25F), 0) << "frame 0 again";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}
TEST(BspWorldTest, AWaterFaceWarpsAndIsNotLit) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    std::vector<std::byte> file = readSeed();
    renameTexture(file, "CRATE01", "!crate");
    const auto map = nx::assets::BspView::parse(file);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    const std::vector<std::byte> shader = worldShader();
    OffscreenTarget target{**device};
    auto world =
        createWorld(**device, {.map = *map, .shaderCode = shader, .colorFormat = Format::Rgba8Unorm});
    ASSERT_TRUE(world.has_value()) << nx::describe(world.error());

    // The -X wall, now water: its light, unlike the lightmap of the crate it was, is the same at
    // every style's brightness, and its texture moves.
    const nx::render::Camera camera{.position = kMiddle, .angles = {.yaw = nx::Degrees{180.0F}}};
    nx::render::LightStyles dark;
    dark.set(0, "a");
    const std::vector<std::byte> atStart = drawWorld(target, *world, camera);
    EXPECT_EQ(drawWorld(target, *world, camera, dark.valuesAt(nx::Seconds{0.0F})), atStart) << "unlit";
    EXPECT_NE(drawWorld(target, *world, camera, normalStyles(), nx::Seconds{0.5F}), atStart) << "warped";
    EXPECT_TRUE(std::ranges::all_of(
        std::views::iota(std::size_t{0}, atStart.size() / 4),
        [&atStart](std::size_t pixel) { return atStart[(pixel * 4) + 3] == std::byte{255}; }))
        << "the whole wall drawn";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}
/// Overwrites the first `from` in the entity text of `file` with `to`, of the same length.
void replaceText(std::vector<std::byte>& file, std::string_view from, std::string_view to) {
    ASSERT_EQ(from.size(), to.size());
    const auto found = std::ranges::search(file, std::as_bytes(std::span{from}));
    ASSERT_FALSE(found.empty()) << from;
    std::ranges::copy(std::as_bytes(std::span{to}), found.begin());
}

TEST(BspWorldTest, ABrushEntityIsDrawnAsItsRenderModeSays) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const std::vector<std::byte> shader = worldShader();
    OffscreenTarget target{**device};
    // The CRATE01 wall, through a square of CRATE01 that covers the view: a func_wall drawn as its
    // rendermode says, at a renderamt of 255 but where a test says otherwise.
    struct Drawn {
        std::vector<std::byte> pixels;
        std::size_t faces = 0;
    };
    const auto drawnAs = [&](std::string_view mode, std::string_view amount) -> Drawn {
        std::vector<std::byte> file = readSeed("boxWithEntity.bsp");
        replaceText(file, R"("rendermode" "1")", std::format(R"("rendermode" "{}")", mode));
        replaceText(file, R"("renderamt" "255")", std::format(R"("renderamt" "{}")", amount));
        const auto map = nx::assets::BspView::parse(file);
        EXPECT_TRUE(map.has_value());
        auto world =
            createWorld(**device, {.map = *map, .shaderCode = shader, .colorFormat = Format::Rgba8Unorm});
        EXPECT_TRUE(world.has_value());
        if (!map || !world) {
            return {};
        }
        std::vector<std::byte> pixels =
            drawWorld(target, *world, {.position = kMiddle, .angles = {.yaw = nx::Degrees{180.0F}}});
        return {.pixels = std::move(pixels), .faces = world->lastFrame().faces};
    };

    const Drawn hidden = drawnAs("2", "000");
    EXPECT_EQ(hidden.faces, 6U) << "a blended entity at a renderamt of 0 is not drawn: the walls";
    EXPECT_EQ(drawnAs("0", "000").faces, 7U) << "a normal one is, whatever its renderamt";

    const Drawn red = drawnAs("1", "255");
    ASSERT_EQ(red.pixels.size(), hidden.pixels.size());
    EXPECT_TRUE(std::ranges::all_of(std::views::iota(std::size_t{0}, red.pixels.size() / 4),
                                    [&red](std::size_t pixel) {
                                        return pixelAt(red.pixels,
                                                       static_cast<std::uint32_t>(pixel % kExtent.width),
                                                       static_cast<std::uint32_t>(pixel / kExtent.width)) ==
                                               Pixel{255, 0, 0, 255};
                                    }))
        << "TransColor: rendercolor, opaque at 255";

    // Half its color over the wall, as alpha blending gives it.
    const Drawn halfRed = drawnAs("1", "128");
    for (const std::uint32_t y : {0U, 16U, 31U}) {
        const Pixel wall = pixelAt(hidden.pixels, 16, y);
        const Pixel blended = pixelAt(halfRed.pixels, 16, y);
        const float alpha = 128.0F / 255.0F;
        EXPECT_NEAR(blended[0], (255.0F * alpha) + (static_cast<float>(wall[0]) * (1.0F - alpha)), 2.0F);
        EXPECT_NEAR(blended[1], static_cast<float>(wall[1]) * (1.0F - alpha), 2.0F);
    }

    // TransAdd: the square's texture added to the wall: nowhere darker, and brighter overall.
    const Drawn added = drawnAs("5", "255");
    EXPECT_TRUE(
        std::ranges::all_of(std::views::iota(std::size_t{0}, added.pixels.size()),
                            [&](std::size_t byte) { return added.pixels[byte] >= hidden.pixels[byte]; }));
    EXPECT_GT(brightnessOf(added.pixels), brightnessOf(hidden.pixels));
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(BspWorldTest, ABrushEntityIsDrawnWhereTheGamePutsAndTurnsIt) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const std::vector<std::byte> shader = worldShader();
    OffscreenTarget target{**device};
    // The square of the seed's func_wall, TransColor red at a renderamt of 255, covers the view.
    const std::vector<std::byte> file = readSeed("boxWithEntity.bsp");
    const auto map = nx::assets::BspView::parse(file);
    ASSERT_TRUE(map.has_value());
    auto world =
        createWorld(**device, {.map = *map, .shaderCode = shader, .colorFormat = Format::Rgba8Unorm});
    ASSERT_TRUE(world.has_value());
    const nx::render::Camera camera{.position = kMiddle, .angles = {.yaw = nx::Degrees{180.0F}}};

    const std::vector<std::byte> inPlace = drawWorld(target, *world, camera);
    EXPECT_EQ(pixelAt(inPlace, 16, 16), (Pixel{255, 0, 0, 255}));

    const std::array away{nx::render::ModelPlacement{.model = {1}, .origin = {.z = 1000.0F}}};
    const std::vector<std::byte> moved =
        drawWorld(target, *world, camera, normalStyles(), nx::Seconds{0.0F}, away);
    EXPECT_NE(pixelAt(moved, 16, 16), (Pixel{255, 0, 0, 255}));
    EXPECT_EQ(world->lastFrame().faces, 6U) << "moved out of view: the walls alone";

    // Turned half a turn about its origin, at the corner of the box, it is behind the walls.
    const std::array turned{nx::render::ModelPlacement{.model = {1}, .angles = {.yaw = nx::Degrees{180.0F}}}};
    EXPECT_NE(pixelAt(drawWorld(target, *world, camera, normalStyles(), nx::Seconds{0.0F}, turned), 16, 16),
              (Pixel{255, 0, 0, 255}));

    const std::array back{nx::render::ModelPlacement{.model = {1}}};
    EXPECT_EQ(pixelAt(drawWorld(target, *world, camera, normalStyles(), nx::Seconds{0.0F}, back), 16, 16),
              (Pixel{255, 0, 0, 255}));
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(BspWorldTest, ABrushEntityWhoseFrameIsNotZeroShowsItsAlternateTextures) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const std::vector<std::byte> shader = worldShader();
    OffscreenTarget target{**device};
    // The square of the seed's func_wall, drawn normally, covers the view: CRATE01 renamed the first
    // frame of a sequence whose alternate is {BLUE, renamed, which no WAD has: a checkerboard.
    std::vector<std::byte> file = readSeed("boxWithEntity.bsp");
    replaceText(file, R"("rendermode" "1")", R"("rendermode" "0")");
    renameTexture(file, "CRATE01", "+0crate");
    renameTexture(file, "{BLUE", "+acrate");
    const auto map = nx::assets::BspView::parse(file);
    ASSERT_TRUE(map.has_value());
    auto world =
        createWorld(**device, {.map = *map, .shaderCode = shader, .colorFormat = Format::Rgba8Unorm});
    ASSERT_TRUE(world.has_value());
    const nx::render::Camera camera{.position = kMiddle, .angles = {.yaw = nx::Degrees{180.0F}}};
    const auto drawnWith = [&](bool showsAlternates) {
        const std::array placements{
            nx::render::ModelPlacement{.model = {1}, .showsAlternateTextures = showsAlternates}};
        return drawWorld(target, *world, camera, normalStyles(), nx::Seconds{0.0F}, placements);
    };

    const std::vector<std::byte> usual = drawnWith(false);
    const std::vector<std::byte> alternate = drawnWith(true);
    EXPECT_NE(alternate, usual);
    // The checkerboard's magenta, which the crate has nowhere.
    EXPECT_TRUE(
        std::ranges::any_of(std::views::iota(std::size_t{0}, alternate.size() / 4), [&](std::size_t pixel) {
            const Pixel color = pixelAt(alternate, static_cast<std::uint32_t>(pixel % kExtent.width),
                                        static_cast<std::uint32_t>(pixel / kExtent.width));
            return color[1] == 0 && color[0] > 0 && color[0] == color[2];
        }));
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(BspWorldTest, TheWaterOfABrushEntityRisesAndFallsInWaves) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const std::vector<std::byte> shader = worldShader();
    OffscreenTarget target{**device};
    // The square of the seed's func_wall, drawn normally, covers the view: its corners raised by waves
    // of `waveHeight`, its texture with them, when it is water.
    const auto drawnWith = [&](bool isWater, float waveHeight) {
        std::vector<std::byte> file = readSeed("boxWithEntity.bsp");
        replaceText(file, R"("rendermode" "1")", R"("rendermode" "0")");
        if (isWater) {
            renameTexture(file, "CRATE01", "!crate");
        }
        const auto map = nx::assets::BspView::parse(file);
        EXPECT_TRUE(map.has_value());
        auto world =
            createWorld(**device, {.map = *map, .shaderCode = shader, .colorFormat = Format::Rgba8Unorm});
        EXPECT_TRUE(world.has_value());
        if (!map || !world) {
            return std::vector<std::byte>{};
        }
        const std::array placements{
            nx::render::ModelPlacement{.model = {1}, .waveHeight = nx::HammerUnits{waveHeight}}};
        return drawWorld(target, *world, {.position = kMiddle, .angles = {.yaw = nx::Degrees{180.0F}}},
                         normalStyles(), nx::Seconds{0.0F}, placements);
    };

    constexpr float kWaveHeight = 0.25F;
    EXPECT_NE(drawnWith(true, kWaveHeight), drawnWith(true, 0.0F)) << "water rises and falls";
    EXPECT_EQ(drawnWith(false, kWaveHeight), drawnWith(false, 0.0F)) << "what is not water does not";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}
/// The color of quadrant `quadrant` (top-left, top-right, bottom-left, bottom-right) of sky side
/// `side` (in the order of kSkySuffixes).
[[nodiscard]] constexpr auto skyColor(std::size_t side, std::size_t quadrant) -> Pixel {
    return {static_cast<int>(30 + (side * 30)), static_cast<int>(40 + (quadrant * 50)), 77, 255};
}

TEST(BspWorldTest, EachSideOfTheSkyIsSeenUprightTowardItsAxis) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    // Every wall of the box a face of the sky.
    std::vector<std::byte> file = readSeed();
    renameTexture(file, "CRATE01", "sky");
    renameTexture(file, "{BLUE", "sky");
    const auto map = nx::assets::BspView::parse(file);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    const std::vector<std::byte> shader = worldShader();
    std::vector<nx::assets::RgbaImage> sky;
    for (std::size_t side = 0; side < nx::render::kSkySuffixes.size(); ++side) {
        nx::assets::RgbaImage image{.width = 2, .height = 2};
        for (std::size_t quadrant = 0; quadrant < 4; ++quadrant) {
            for (const int channel : skyColor(side, quadrant)) {
                image.pixels.push_back(static_cast<std::byte>(channel));
            }
        }
        sky.push_back(std::move(image));
    }
    OffscreenTarget target{**device};
    auto world = createWorld(
        **device, {.map = *map, .shaderCode = shader, .colorFormat = Format::Rgba8Unorm, .sky = sky});
    ASSERT_TRUE(world.has_value()) << nx::describe(world.error());
    EXPECT_TRUE(world->stats().missingTextures.empty());

    // From the middle of the sky, as the original projects it, the side toward each axis is seen
    // upright and unmirrored; the top and bottom ones facing +X.
    struct View {
        nx::EulerAngles angles;
        std::size_t side; ///< In the order of kSkySuffixes: rt, bk, lf, ft, up, dn.
    };
    const std::array views{
        View{.angles = {.yaw = nx::Degrees{0.0F}}, .side = 0},     // +X: rt
        View{.angles = {.yaw = nx::Degrees{90.0F}}, .side = 1},    // +Y: bk
        View{.angles = {.yaw = nx::Degrees{180.0F}}, .side = 2},   // -X: lf
        View{.angles = {.yaw = nx::Degrees{270.0F}}, .side = 3},   // -Y: ft
        View{.angles = {.pitch = nx::Degrees{-90.0F}}, .side = 4}, // up
        View{.angles = {.pitch = nx::Degrees{90.0F}}, .side = 5},  // down
    };
    for (const View& view : views) {
        const std::vector<std::byte> pixels =
            drawWorld(target, *world, {.position = kMiddle, .angles = view.angles});
        ASSERT_EQ(pixels.size(), std::size_t{kExtent.width} * kExtent.height * 4);
        const std::uint32_t last = kExtent.width - 1;
        EXPECT_EQ(pixelAt(pixels, 0, 0), skyColor(view.side, 0)) << "side " << view.side << ", top-left";
        EXPECT_EQ(pixelAt(pixels, last, 0), skyColor(view.side, 1)) << "side " << view.side << ", top-right";
        EXPECT_EQ(pixelAt(pixels, 0, last), skyColor(view.side, 2))
            << "side " << view.side << ", bottom-left";
        EXPECT_EQ(pixelAt(pixels, last, last), skyColor(view.side, 3))
            << "side " << view.side << ", bottom-right";
    }

    sky.pop_back();
    auto refused = createWorld(
        **device, {.map = *map, .shaderCode = shader, .colorFormat = Format::Rgba8Unorm, .sky = sky});
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, nx::render::WorldError::BadSky) << "five sides";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

/// A bullet hole's like: 16 x 16 texels, black, nearly opaque throughout (index 250 of 255).
class HoleDecal {
public:
    static constexpr std::uint32_t kSide = 16;
    static constexpr float kOpacity = 250.0F / 255.0F;

    [[nodiscard]] auto textures() const -> std::array<nx::assets::MipTexture, 1> {
        return {nx::assets::MipTexture{.name = "{shot1",
                                       .width = kSide,
                                       .height = kSide,
                                       .levels = {m_indices, m_indices, m_indices, m_indices},
                                       .palette = m_palette}};
    }

private:
    std::vector<std::byte> m_indices = std::vector<std::byte>(std::size_t{kSide} * kSide, std::byte{250});
    std::vector<std::byte> m_palette = std::vector<std::byte>(std::size_t{256} * 3, std::byte{0});
};

[[nodiscard]] auto sumOf(const Pixel& pixel) -> int {
    return pixel[0] + pixel[1] + pixel[2];
}

TEST(BspWorldTest, ADecalDarkensTheFaceItLiesOnLitAsItIs) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const std::vector<std::byte> file = readSeed();
    const auto map = nx::assets::BspView::parse(file);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    const std::vector<std::byte> shader = worldShader();
    OffscreenTarget target{**device};
    const HoleDecal hole;
    const auto decals = hole.textures();
    auto world = createWorld(
        **device, {.map = *map, .shaderCode = shader, .colorFormat = Format::Rgba8Unorm, .decals = decals});
    ASSERT_TRUE(world.has_value()) << nx::describe(world.error());

    // Looking down at the floor of CRATE01 from the middle: 2 units a pixel, the decal 8 pixels wide.
    const nx::render::Camera camera{.position = kMiddle, .angles = {.pitch = nx::Degrees{90.0F}}};
    const std::vector<std::byte> before = drawWorld(target, *world, camera);
    EXPECT_EQ(world->shootDecal(nx::render::DecalImageIndex{0}, {.x = 32.0F, .y = 32.0F, .z = 0.0F}), 1U);
    EXPECT_EQ(world->decals().decalsOn(nx::assets::FaceIndex{0}), 1U);
    const std::vector<std::byte> after = drawWorld(target, *world, camera);
    const Pixel lit = pixelAt(before, kExtent.width / 2, kExtent.height / 2);
    const Pixel holed = pixelAt(after, kExtent.width / 2, kExtent.height / 2);
    EXPECT_GT(sumOf(lit), 0);
    EXPECT_LT(sumOf(holed) * 10, sumOf(lit)) << "under the decal, the floor nearly black";
    EXPECT_EQ(pixelAt(after, 2, 2), pixelAt(before, 2, 2)) << "away from it, as it was";
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

/// The seed with the square of its func_wall, model 1 at x = 16, drawn in `mode` at `amount`, and the
/// hole decal; the views point into it, which stays where it is made.
struct WallWithDecals {
    std::vector<std::byte> file;
    std::optional<nx::assets::BspView> map;
    HoleDecal hole;
    std::array<nx::assets::MipTexture, 1> decals;

    WallWithDecals(std::string_view mode, std::string_view amount)
        : file{readSeed("boxWithEntity.bsp")}, decals{hole.textures()} {
        replaceText(file, R"("rendermode" "1")", std::format(R"("rendermode" "{}")", mode));
        replaceText(file, R"("renderamt" "255")", std::format(R"("renderamt" "{}")", amount));
        auto parsed = nx::assets::BspView::parse(file);
        EXPECT_TRUE(parsed.has_value());
        if (parsed) {
            map.emplace(std::move(*parsed));
        }
    }
    WallWithDecals(const WallWithDecals&) = delete;
    WallWithDecals(WallWithDecals&&) = delete;
    auto operator=(const WallWithDecals&) -> WallWithDecals& = delete;
    auto operator=(WallWithDecals&&) -> WallWithDecals& = delete;
    ~WallWithDecals() = default;
};

/// Where the decals of the tests below are stamped: the middle of the square.
constexpr nx::Vec3 kSquareMiddle{.x = 16.0F, .y = 32.0F, .z = 32.0F};
constexpr nx::render::DecalTarget kOnTheWall{.model = {1}};

TEST(BspWorldTest, ADecalOnABrushEntityGoesWhereTheGameMovesIt) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const std::vector<std::byte> shader = worldShader();
    OffscreenTarget target{**device};
    // The square covers the view, 0.75 units a pixel: the decal 21 pixels wide about the middle.
    const WallWithDecals wall{"0", "255"};
    ASSERT_TRUE(wall.map.has_value());
    auto world = createWorld(
        **device,
        {.map = *wall.map, .shaderCode = shader, .colorFormat = Format::Rgba8Unorm, .decals = wall.decals});
    ASSERT_TRUE(world.has_value()) << nx::describe(world.error());
    const nx::render::Camera camera{.position = kMiddle, .angles = {.yaw = nx::Degrees{180.0F}}};
    // Moved 8 units to the right of the view: 10.7 pixels.
    const std::array moved{nx::render::ModelPlacement{.model = {1}, .origin = {.y = 8.0F}}};
    const auto drawn = [&](std::span<const nx::render::ModelPlacement> placements) {
        return drawWorld(target, *world, camera, normalStyles(), nx::Seconds{0.0F}, placements);
    };
    const std::vector<std::byte> plain = drawn({});
    const std::vector<std::byte> movedPlain = drawn(moved);

    ASSERT_EQ(world->shootDecal(nx::render::DecalImageIndex{0}, kSquareMiddle, kOnTheWall), 1U);
    const std::vector<std::byte> holed = drawn({});
    const std::vector<std::byte> movedHoled = drawn(moved);
    constexpr std::uint32_t kRow = 16;
    constexpr std::uint32_t kLeft = 8;
    constexpr std::uint32_t kRight = 29;
    EXPECT_LT(sumOf(pixelAt(holed, kLeft, kRow)) * 10, sumOf(pixelAt(plain, kLeft, kRow)));
    EXPECT_EQ(pixelAt(holed, kRight, kRow), pixelAt(plain, kRight, kRow));
    EXPECT_EQ(pixelAt(movedHoled, kLeft, kRow), pixelAt(movedPlain, kLeft, kRow)) << "it went with the wall";
    EXPECT_LT(sumOf(pixelAt(movedHoled, kRight, kRow)) * 10, sumOf(pixelAt(movedPlain, kRight, kRow)));
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(BspWorldTest, ADecalOnABrushEntityIsDrawnAsItsRenderModeDrawsIt) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    const std::vector<std::byte> shader = worldShader();
    OffscreenTarget target{**device};
    const nx::render::Camera inFront{.position = kMiddle, .angles = {.yaw = nx::Degrees{180.0F}}};
    const nx::render::Camera behind{.position = {.x = 8.0F, .y = 32.0F, .z = 32.0F}};
    struct Drawn {
        std::vector<std::byte> plain{};
        std::vector<std::byte> holed{}; ///< Drawn again once the decal is stamped.
    };
    const auto drawnAs = [&](std::string_view mode, std::string_view amount,
                             const nx::render::Camera& camera) {
        const WallWithDecals wall{mode, amount};
        EXPECT_TRUE(wall.map.has_value());
        auto world = createWorld(**device, {.map = *wall.map,
                                            .shaderCode = shader,
                                            .colorFormat = Format::Rgba8Unorm,
                                            .decals = wall.decals});
        EXPECT_TRUE(world.has_value());
        if (!wall.map || !world) {
            return Drawn{};
        }
        Drawn drawn{.plain = drawWorld(target, *world, camera)};
        EXPECT_EQ(world->shootDecal(nx::render::DecalImageIndex{0}, kSquareMiddle, kOnTheWall), 1U);
        drawn.holed = drawWorld(target, *world, camera);
        return drawn;
    };

    // TransTexture at half: the decal over the square half as opaque as it is, as renderamt makes it.
    const Drawn half = drawnAs("2", "128", inFront);
    ASSERT_FALSE(half.plain.empty());
    const Pixel under = pixelAt(half.plain, kExtent.width / 2, kExtent.height / 2);
    const Pixel over = pixelAt(half.holed, kExtent.width / 2, kExtent.height / 2);
    const float kept = 1.0F - (HoleDecal::kOpacity * (128.0F / 255.0F));
    for (std::size_t channel = 0; channel < 3; ++channel) {
        EXPECT_NEAR(static_cast<float>(over.at(channel)), static_cast<float>(under.at(channel)) * kept, 2.0F)
            << "channel " << channel;
    }

    // From behind, a normal entity's face is not drawn, nor its decals; a TransTexture one's decals
    // are, its face culled, as the original keeps such a face for them.
    const Drawn normal = drawnAs("0", "255", behind);
    EXPECT_EQ(normal.holed, normal.plain);
    const Drawn glass = drawnAs("2", "255", behind);
    EXPECT_LT(brightnessOf(glass.holed) * 2, brightnessOf(glass.plain));
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

void paintTextures(std::vector<std::byte>& file, std::array<int, 3> color) {
    const auto map = nx::assets::BspView::parse(file);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    for (const auto& texture : map->textures()) {
        if (!texture || !texture->hasPixels()) {
            continue;
        }
        const auto first = static_cast<std::size_t>(texture->palette.data() - file.data());
        for (std::size_t offset = 0; offset < texture->palette.size(); ++offset) {
            file[first + offset] = static_cast<std::byte>(color[offset % color.size()]);
        }
    }
}

TEST(BspWorldTest, DrawnAsLinearLightAFaceIsItsTextureTimesItsLightAsLight) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    std::vector<std::byte> file = readSeed();
    paintTextures(file, {70, 40, 20});
    const auto map = nx::assets::BspView::parse(file);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    const auto shader = worldShader();
    auto legacy =
        createWorld(**device, {.map = *map, .shaderCode = shader, .colorFormat = Format::Rgba8Unorm});
    auto linear = createWorld(**device, {.map = *map,
                                         .shaderCode = shader,
                                         .colorFormat = Format::Rgba8Unorm,
                                         .encoding = nx::render::ColorEncoding::Linear});
    ASSERT_TRUE(legacy.has_value()) << nx::describe(legacy.error());
    ASSERT_TRUE(linear.has_value()) << nx::describe(linear.error());
    OffscreenTarget target{**device};
    const nx::render::Camera camera{.position = kMiddle, .angles = {.yaw = nx::Degrees{180.0F}}};
    const auto displayPixels = drawWorld(target, *legacy, camera);
    const auto linearPixels = drawWorld(target, *linear, camera);
    ASSERT_EQ(displayPixels.size(), linearPixels.size());
    constexpr std::array kSampleCoordinates{8U, 16U, 24U};
    constexpr std::size_t kRgbChannels = 3;
    constexpr double kChannelTop = 255.0;
    constexpr double kDisplayGamma = 2.2;
    for (const auto y : kSampleCoordinates) {
        for (const auto x : kSampleCoordinates) {
            const Pixel display = pixelAt(displayPixels, x, y);
            const Pixel actual = pixelAt(linearPixels, x, y);
            for (std::size_t channel = 0; channel < kRgbChannels; ++channel) {
                const double expected = std::pow(display[channel] / kChannelTop, kDisplayGamma) * kChannelTop;
                EXPECT_NEAR(actual[channel], expected, 2.0) << x << ", " << y << ", channel " << channel;
            }
            EXPECT_EQ(actual[3], display[3]);
        }
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(BspWorldTest, DrawnAsLinearLightTheSkyIsFilteredAsLight) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    std::vector<std::byte> file = readSeed();
    renameTexture(file, "CRATE01", "sky");
    renameTexture(file, "{BLUE", "sky");
    const auto map = nx::assets::BspView::parse(file);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    const auto shader = worldShader();
    // The center of each two-texel side is an equal mixture of black and white radiance.
    const nx::assets::RgbaImage side{.width = 2,
                                     .height = 1,
                                     .pixels = {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255},
                                                std::byte{255}, std::byte{255}, std::byte{255},
                                                std::byte{255}}};
    const std::vector sky(nx::render::kSkySuffixes.size(), side);
    auto world = createWorld(**device, {.map = *map,
                                        .shaderCode = shader,
                                        .colorFormat = Format::Rgba8Unorm,
                                        .sky = sky,
                                        .encoding = nx::render::ColorEncoding::Linear});
    ASSERT_TRUE(world.has_value()) << nx::describe(world.error());
    OffscreenTarget target{**device};
    const auto pixels = drawWorld(target, *world, {.position = kMiddle});
    constexpr std::uint32_t kPixelMiddle = kExtent.width / 2;
    const Pixel left = pixelAt(pixels, kPixelMiddle - 1, kPixelMiddle);
    const Pixel right = pixelAt(pixels, kPixelMiddle, kPixelMiddle);
    constexpr double kHalfWhite = 127.5;
    constexpr std::size_t kRgbChannels = 3;
    for (std::size_t channel = 0; channel < kRgbChannels; ++channel) {
        EXPECT_NEAR((left[channel] + right[channel]) / 2.0, kHalfWhite, 2.0);
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

TEST(BspWorldTest, DrawnAsLinearLightABrushsAmountScalesItsLight) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        ASSERT_TRUE(nx::test::isVulkanUnavailable(device.error().code)) << nx::describe(device.error());
        GTEST_SKIP() << nx::test::kNoVulkan;
    }
    constexpr std::array kColor{70, 40, 20};
    const auto shader = worldShader();
    OffscreenTarget target{**device};
    const auto drawnAs = [&](std::string_view mode, std::string_view amount) -> std::vector<std::byte> {
        auto file = readSeed("boxWithEntity.bsp");
        paintTextures(file, kColor);
        replaceText(file, R"("rendermode" "1")", std::format(R"("rendermode" "{}")", mode));
        replaceText(file, R"("renderamt" "255")", std::format(R"("renderamt" "{}")", amount));
        const auto map = nx::assets::BspView::parse(file);
        EXPECT_TRUE(map.has_value());
        if (!map) {
            return {};
        }
        auto world = createWorld(**device, {.map = *map,
                                            .shaderCode = shader,
                                            .colorFormat = Format::Rgba8Unorm,
                                            .encoding = nx::render::ColorEncoding::Linear});
        EXPECT_TRUE(world.has_value()) << (world ? "" : nx::describe(world.error()));
        if (!world) {
            return {};
        }
        return drawWorld(target, *world, {.position = kMiddle, .angles = {.yaw = nx::Degrees{180.0F}}});
    };
    const auto hiddenPixels = drawnAs("2", "000");
    ASSERT_FALSE(hiddenPixels.empty());
    constexpr std::uint32_t kPixelMiddle = kExtent.width / 2;
    const Pixel background = pixelAt(hiddenPixels, kPixelMiddle, kPixelMiddle);
    const auto gamma = nx::render::textureGammaTable({});
    constexpr double kChannelTop = 255.0;
    constexpr double kDisplayGamma = 2.2;
    constexpr double kAmount = 128.0 / kChannelTop;
    for (const std::string_view mode : {"2", "5"}) {
        const auto pixels = drawnAs(mode, "128");
        ASSERT_EQ(pixels.size(), hiddenPixels.size());
        const Pixel actual = pixelAt(pixels, kPixelMiddle, kPixelMiddle);
        const double backgroundShare = mode == "5" ? 1.0 : 1.0 - kAmount;
        for (std::size_t channel = 0; channel < kColor.size(); ++channel) {
            const auto sourceByte = gamma[static_cast<std::size_t>(kColor[channel])];
            const double source = std::pow(sourceByte / kChannelTop, kDisplayGamma);
            const double expected = source * kAmount * kChannelTop + background[channel] * backgroundShare;
            EXPECT_NEAR(actual[channel], expected, 3.0) << "mode " << mode << ", channel " << channel;
        }
    }
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

} // namespace
