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

    auto target = (*device)->createTexture(
        {.label = "test.target",
         .format = Format::Rgba8Unorm,
         .extent = kExtent,
         .usage = {nx::rhi::TextureUsage::RenderTarget, nx::rhi::TextureUsage::CopySource}});
    auto depth = (*device)->createTexture({.label = "test.depth",
                                           .format = Format::Depth32Float,
                                           .extent = kExtent,
                                           .usage = {nx::rhi::TextureUsage::DepthTarget}});
    auto readback = (*device)->createBuffer({.label = "test.readback",
                                             .size = std::uint64_t{kExtent.width} * kExtent.height * 4,
                                             .usage = {nx::rhi::BufferUsage::CopyDestination},
                                             .location = nx::rhi::MemoryLocation::Readback});
    ASSERT_TRUE(target && depth && readback);

    auto commands = (*device)->beginFrame();
    ASSERT_TRUE(commands.has_value());
    nx::rhi::CommandList& list = commands->get();
    auto world = nx::render::BspWorld::create(
        **device, list, {.map = *map, .shaderCode = shader, .colorFormat = Format::Rgba8Unorm});
    ASSERT_TRUE(world.has_value()) << nx::describe(world.error());
    EXPECT_EQ(world->stats().faces, 6U);
    EXPECT_EQ(world->stats().triangles, 12U);
    EXPECT_EQ(world->stats().missingTextures, std::vector<std::string>{"{BLUE"}) << "no WAD was given";

    // From the middle of the box, looking along -X at a wall of CRATE01.
    const nx::render::Camera camera{.position = {.x = 32.0F, .y = 32.0F, .z = 32.0F},
                                    .angles = {.yaw = nx::Degrees{180.0F}}};
    list.transition(*target, TextureState::Undefined, TextureState::RenderTarget);
    list.transition(*depth, TextureState::Undefined, TextureState::DepthTarget);
    // Cleared to an alpha of 0, which the world shader never writes.
    const std::array colors{nx::rhi::ColorAttachment{.texture = *target, .clear = {.alpha = 0.0F}}};
    list.beginRendering({.colors = colors,
                         .depth = nx::rhi::DepthAttachment{.texture = *depth, .clear = 0.0F},
                         .area = kExtent});
    world->draw(list, nx::render::viewProjectionMatrix(camera, 1.0F));
    list.endRendering();
    list.transition(*target, TextureState::RenderTarget, TextureState::CopySource);
    list.transition(*readback, BufferState::Undefined, BufferState::CopyDestination);
    list.copyTextureToBuffer({.buffer = *readback, .texture = *target, .extent = kExtent});
    list.transition(*readback, BufferState::CopyDestination, BufferState::HostRead);
    ASSERT_TRUE((*device)->submitFrame().has_value());
    (*device)->waitIdle();

    const std::span<const std::byte> pixels = (*device)->mappedBytes(*readback);
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

} // namespace
