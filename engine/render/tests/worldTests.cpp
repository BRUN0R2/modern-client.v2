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

namespace {

using nx::rhi::BufferState;
using nx::rhi::Format;
using nx::rhi::TextureState;

constexpr nx::rhi::Extent2d kExtent{.width = 32, .height = 32};
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

/// An offscreen target, its depth and the buffer it is read back to.
class OffscreenTarget {
public:
    explicit OffscreenTarget(nx::rhi::Device& device) : m_device{&device} {
        auto color = device.createTexture(
            {.label = "test.target",
             .format = Format::Rgba8Unorm,
             .extent = kExtent,
             .usage = {nx::rhi::TextureUsage::RenderTarget, nx::rhi::TextureUsage::CopySource}});
        auto depth = device.createTexture({.label = "test.depth",
                                           .format = Format::Depth32Float,
                                           .extent = kExtent,
                                           .usage = {nx::rhi::TextureUsage::DepthTarget}});
        auto readback = device.createBuffer({.label = "test.readback",
                                             .size = std::uint64_t{kExtent.width} * kExtent.height * 4,
                                             .usage = {nx::rhi::BufferUsage::CopyDestination},
                                             .location = nx::rhi::MemoryLocation::Readback});
        EXPECT_TRUE(color && depth && readback);
        m_color = color.value_or(nx::rhi::TextureHandle{});
        m_depth = depth.value_or(nx::rhi::TextureHandle{});
        m_readback = readback.value_or(nx::rhi::BufferHandle{});
    }

    /// Draws `world` as `camera` sees it with `styles`, `time` after the map started and its brush
    /// models where `placements` puts them, in a frame of its own, on a target cleared to an alpha of
    /// 0, which the world never writes; then the pixels, RGBA rows from the top.
    [[nodiscard]] auto draw(nx::render::BspWorld& world, const nx::render::Camera& camera,
                            const nx::render::LightStyleValues& styles = normalStyles(),
                            nx::Seconds time = nx::Seconds{0.0F},
                            std::span<const nx::render::ModelPlacement> placements = {})
        -> std::vector<std::byte> {
        auto commands = m_device->beginFrame();
        EXPECT_TRUE(commands.has_value());
        if (!commands) {
            return {};
        }
        nx::rhi::CommandList& list = commands->get();
        list.transition(m_color, TextureState::Undefined, TextureState::RenderTarget);
        list.transition(m_depth, TextureState::Undefined, TextureState::DepthTarget);
        const std::array colors{nx::rhi::ColorAttachment{.texture = m_color, .clear = {.alpha = 0.0F}}};
        list.beginRendering({.colors = colors,
                             .depth = nx::rhi::DepthAttachment{.texture = m_depth, .clear = 0.0F},
                             .area = kExtent});
        world.draw(list, {.camera = camera,
                          .aspect = 1.0F,
                          .time = time,
                          .lightStyles = styles,
                          .placements = placements});
        list.endRendering();
        list.transition(m_color, TextureState::RenderTarget, TextureState::CopySource);
        list.transition(m_readback, BufferState::Undefined, BufferState::CopyDestination);
        list.copyTextureToBuffer({.buffer = m_readback, .texture = m_color, .extent = kExtent});
        list.transition(m_readback, BufferState::CopyDestination, BufferState::HostRead);
        EXPECT_TRUE(m_device->submitFrame().has_value());
        m_device->waitIdle();
        return m_device->mappedBytes(m_readback) | std::ranges::to<std::vector>();
    }

private:
    nx::rhi::Device* m_device;
    nx::rhi::TextureHandle m_color;
    nx::rhi::TextureHandle m_depth;
    nx::rhi::BufferHandle m_readback;
};

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

using Pixel = std::array<int, 4>;

[[nodiscard]] auto pixelAt(std::span<const std::byte> pixels, std::uint32_t x, std::uint32_t y) -> Pixel {
    const std::size_t offset = ((std::size_t{y} * kExtent.width) + x) * 4;
    return {std::to_integer<int>(pixels[offset]), std::to_integer<int>(pixels[offset + 1]),
            std::to_integer<int>(pixels[offset + 2]), std::to_integer<int>(pixels[offset + 3])};
}

TEST(BspWorldTest, DrawsTheFacesOfAMapAroundTheCamera) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
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
        target.draw(*world, {.position = kMiddle, .angles = {.yaw = nx::Degrees{180.0F}}});
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
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
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
        return brightnessOf(target.draw(*world, camera, styles.valuesAt(nx::Seconds{0.0F})));
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
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    const std::vector<std::byte> file = readSeed();
    const auto map = nx::assets::BspView::parse(file);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    const std::vector<std::byte> shader = worldShader();
    OffscreenTarget target{**device};
    auto world =
        createWorld(**device, {.map = *map, .shaderCode = shader, .colorFormat = Format::Rgba8Unorm});
    ASSERT_TRUE(world.has_value()) << nx::describe(world.error());

    std::ignore = target.draw(*world, {.position = kMiddle});
    EXPECT_EQ(world->lastFrame().faces, 6U) << "inside: every wall faces the camera";
    EXPECT_EQ(world->lastFrame().triangles, 12U);

    // Outside, past the -X wall: it turns its back, the five others face this way.
    std::ignore = target.draw(*world, {.position = {.x = -100.0F, .y = 32.0F, .z = 32.0F}});
    EXPECT_EQ(world->lastFrame().faces, 5U);

    const std::vector<std::byte> away = target.draw(
        *world, {.position = {.x = -100.0F, .y = 32.0F, .z = 32.0F}, .angles = {.yaw = nx::Degrees{180.0F}}});
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
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
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
            target.draw(*world, {.position = kMiddle, .angles = {.yaw = nx::Degrees{180.0F}}}, normalStyles(),
                        nx::Seconds{seconds});
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
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
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
    const std::vector<std::byte> atStart = target.draw(*world, camera);
    EXPECT_EQ(target.draw(*world, camera, dark.valuesAt(nx::Seconds{0.0F})), atStart) << "unlit";
    EXPECT_NE(target.draw(*world, camera, normalStyles(), nx::Seconds{0.5F}), atStart) << "warped";
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
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
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
            target.draw(*world, {.position = kMiddle, .angles = {.yaw = nx::Degrees{180.0F}}});
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
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
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

    const std::vector<std::byte> inPlace = target.draw(*world, camera);
    EXPECT_EQ(pixelAt(inPlace, 16, 16), (Pixel{255, 0, 0, 255}));

    const std::array away{nx::render::ModelPlacement{.model = 1, .origin = {.z = 1000.0F}}};
    const std::vector<std::byte> moved = target.draw(*world, camera, normalStyles(), nx::Seconds{0.0F}, away);
    EXPECT_NE(pixelAt(moved, 16, 16), (Pixel{255, 0, 0, 255}));
    EXPECT_EQ(world->lastFrame().faces, 6U) << "moved out of view: the walls alone";

    // Turned half a turn about its origin, at the corner of the box, it is behind the walls.
    const std::array turned{nx::render::ModelPlacement{.model = 1, .angles = {.yaw = nx::Degrees{180.0F}}}};
    EXPECT_NE(pixelAt(target.draw(*world, camera, normalStyles(), nx::Seconds{0.0F}, turned), 16, 16),
              (Pixel{255, 0, 0, 255}));

    const std::array back{nx::render::ModelPlacement{.model = 1}};
    EXPECT_EQ(pixelAt(target.draw(*world, camera, normalStyles(), nx::Seconds{0.0F}, back), 16, 16),
              (Pixel{255, 0, 0, 255}));
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
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
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
            target.draw(*world, {.position = kMiddle, .angles = view.angles});
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

} // namespace
