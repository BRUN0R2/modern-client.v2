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

[[nodiscard]] auto readSeed() -> std::vector<std::byte> {
    auto bytes =
        nx::platform::readFile(std::filesystem::path{NX_FUZZ_CORPUS_DIRECTORY} / "bsp" / "boxEmbedded.bsp");
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

/// Renames the texture `name` of the map in `file` to "sky": its 16-byte name field.
void renameToSky(std::vector<std::byte>& file, std::string_view name) {
    const auto bytes = std::as_bytes(std::span{name});
    const auto found = std::ranges::search(file, bytes);
    ASSERT_FALSE(found.empty()) << name;
    const auto field = std::span{file}.subspan(static_cast<std::size_t>(found.begin() - file.begin()), 16);
    std::ranges::fill(field, std::byte{0});
    std::ranges::copy(std::as_bytes(std::span{std::string_view{"sky"}}), field.begin());
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

    /// Draws `world` as `camera` sees it, in a frame of its own, on a target cleared to an alpha of
    /// 0, which the world never writes; then the pixels, RGBA rows from the top.
    [[nodiscard]] auto draw(nx::render::BspWorld& world, const nx::render::Camera& camera)
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
        world.draw(list, camera, 1.0F);
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
    renameToSky(file, "CRATE01");
    renameToSky(file, "{BLUE");
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
