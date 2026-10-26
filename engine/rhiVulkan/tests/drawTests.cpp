// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Draws into offscreen targets and reads their pixels back: buffers, textures and their uploads,
// samplers, bind groups, pipelines, depth, and the RHI's clip space (y up, the viewport from the
// top-left corner). The shaders are the build's own (shaders/test, in bin/shaders/test).

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.rhi;
import nx.rhi.vulkan;

#include "testDevice.hpp"

namespace {

using nx::rhi::BufferState;
using nx::rhi::BufferUsage;
using nx::rhi::CommandList;
using nx::rhi::Device;
using nx::rhi::Extent2d;
using nx::rhi::Format;
using nx::rhi::MemoryLocation;
using nx::rhi::OwnedBuffer;
using nx::rhi::OwnedTexture;
using nx::rhi::TextureHandle;
using nx::rhi::TextureState;
using nx::rhi::TextureUsage;

using Pixel = std::array<std::uint8_t, 4>;

constexpr Pixel kBlack{0, 0, 0, 255};
constexpr Pixel kRed{255, 0, 0, 255};
constexpr Pixel kGreen{0, 255, 0, 255};
constexpr Pixel kBlue{0, 0, 255, 255};
constexpr Pixel kWhite{255, 255, 255, 255};

[[nodiscard]] auto bytesOf(std::span<const Pixel> pixels) -> std::vector<std::byte> {
    return std::as_bytes(pixels) | std::ranges::to<std::vector>();
}

[[nodiscard]] auto shaderCode(std::string_view name) -> std::vector<std::byte> {
    const auto executable = nx::platform::executablePath();
    if (!executable) {
        ADD_FAILURE() << nx::describe(executable.error());
        return {};
    }
    auto code = nx::platform::readFile(executable->parent_path() / "shaders" / "test" / name);
    EXPECT_TRUE(code.has_value()) << nx::describe(code.error());
    return code.value_or(std::vector<std::byte>{});
}

class DrawTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto opened = nx::test::openTestDevice();
        if (!opened) {
            ASSERT_TRUE(nx::test::isVulkanUnavailable(opened.error().code)) << nx::describe(opened.error());
            GTEST_SKIP() << nx::test::kNoVulkan;
        }
        m_device = std::move(*opened);
    }

    void TearDown() override {
        if (m_device) {
            m_device->waitIdle();
            EXPECT_EQ(m_device->validationErrors(), 0U);
        }
    }

    [[nodiscard]] auto device() -> Device& { return *m_device; }

    /// A target of `extent` to draw to and read back.
    [[nodiscard]] auto createTarget(Extent2d extent) -> nx::Result<OwnedTexture, nx::rhi::RhiError> {
        return device().createTexture({.label = "test.target",
                                       .format = Format::Rgba8Unorm,
                                       .extent = extent,
                                       .usage = {TextureUsage::RenderTarget, TextureUsage::CopySource}});
    }

    /// An Upload buffer holding `bytes`.
    [[nodiscard]] auto upload(std::string_view label, nx::Flags<BufferUsage> usage,
                              std::span<const std::byte> bytes)
        -> nx::Result<OwnedBuffer, nx::rhi::RhiError> {
        auto buffer = device().createBuffer(
            {.label = label, .size = bytes.size(), .usage = usage, .location = MemoryLocation::Upload});
        if (buffer) {
            std::ranges::copy(bytes, device().mappedBytes(buffer->get()).begin());
        }
        return buffer;
    }

    /// Records `record` as a frame, submits it and waits for it.
    void runFrame(const std::function<void(CommandList&)>& record) {
        auto commands = device().beginFrame();
        ASSERT_TRUE(commands.has_value()) << nx::describe(commands.error());
        record(commands->get());
        const auto submitted = device().submitFrame();
        ASSERT_TRUE(submitted.has_value()) << nx::describe(submitted.error());
        device().waitIdle();
    }

    /// Draws with `record` into a target of `extent` cleared to black, and returns its pixels, row
    /// after row from the top.
    [[nodiscard]] auto drawAndRead(Extent2d extent, const std::function<void(CommandList&)>& record,
                                   std::optional<TextureHandle> depth = std::nullopt) -> std::vector<Pixel> {
        auto target = createTarget(extent);
        const std::uint64_t size = std::uint64_t{extent.width} * extent.height * sizeof(Pixel);
        auto readback = device().createBuffer({.label = "test.readback",
                                               .size = size,
                                               .usage = {BufferUsage::CopyDestination},
                                               .location = MemoryLocation::Readback});
        if (!target || !readback) {
            ADD_FAILURE() << "cannot create the target or its readback buffer";
            return {};
        }
        runFrame([&](CommandList& commands) {
            commands.transition(target->get(), TextureState::Undefined, TextureState::RenderTarget);
            if (depth) {
                commands.transition(*depth, TextureState::Undefined, TextureState::DepthTarget);
            }
            const std::array colors{
                nx::rhi::ColorAttachment{.texture = target->get(), .clear = {.red = 0.0F}}};
            commands.beginRendering(
                {.colors = colors,
                 .depth = depth ? std::optional{nx::rhi::DepthAttachment{.texture = *depth}} : std::nullopt,
                 .area = extent});
            record(commands);
            commands.endRendering();
            commands.transition(target->get(), TextureState::RenderTarget, TextureState::CopySource);
            commands.transition(readback->get(), BufferState::Undefined, BufferState::CopyDestination);
            commands.copyTextureToBuffer(
                {.buffer = readback->get(), .texture = target->get(), .extent = extent});
            commands.transition(readback->get(), BufferState::CopyDestination, BufferState::HostRead);
        });
        const std::span<const std::byte> bytes = device().mappedBytes(readback->get());
        std::vector<Pixel> pixels(nx::narrow<std::size_t>(size / sizeof(Pixel)));
        std::ranges::copy(bytes, std::as_writable_bytes(std::span{pixels}).begin());
        return pixels;
    }

    /// Uploads `texels`, row after row from the top, to `layer` of `texture`, and makes the texture
    /// ready to sample. Transitions cover every layer: the texture is undefined before its first upload.
    void uploadTexels(TextureHandle texture, std::uint32_t layer, Extent2d extent,
                      std::span<const Pixel> texels) {
        auto staging = upload("test.staging", {BufferUsage::CopySource}, bytesOf(texels));
        ASSERT_TRUE(staging.has_value()) << nx::describe(staging.error());
        runFrame([&](CommandList& commands) {
            commands.transition(texture, layer == 0 ? TextureState::Undefined : TextureState::ShaderRead,
                                TextureState::CopyDestination);
            commands.copyBufferToTexture(
                {.buffer = staging->get(), .texture = texture, .arrayLayer = layer, .extent = extent});
            commands.transition(texture, TextureState::CopyDestination, TextureState::ShaderRead);
        });
    }

    /// Draws a covering triangle with the entry points of `shaderFile`, which samples `texture`
    /// (binding 0) with a point sampler (binding 1), into a target of `extent`, and reads it back.
    [[nodiscard]] auto drawSampled(std::string_view shaderFile, TextureHandle texture, Extent2d extent)
        -> std::vector<Pixel> {
        auto sampler = device().createSampler({.label = "test.point",
                                               .magnify = nx::rhi::Filter::Nearest,
                                               .minify = nx::rhi::Filter::Nearest,
                                               .mipmap = nx::rhi::Filter::Nearest,
                                               .address = nx::rhi::AddressMode::ClampToEdge});
        const std::vector<std::byte> code = shaderCode(shaderFile);
        auto shader = device().createShader({.label = "test.sample", .code = code});
        const std::array bindings{nx::rhi::BindingLayout{.binding = 0,
                                                         .type = nx::rhi::BindingType::SampledTexture,
                                                         .stages = {nx::rhi::ShaderStage::Fragment}},
                                  nx::rhi::BindingLayout{.binding = 1,
                                                         .type = nx::rhi::BindingType::Sampler,
                                                         .stages = {nx::rhi::ShaderStage::Fragment}}};
        auto layout = device().createBindGroupLayout({.label = "test.sample", .bindings = bindings});
        if (!sampler || !shader || !layout) {
            ADD_FAILURE() << "cannot create the sampler, the shader or the layout";
            return {};
        }
        const std::array resources{nx::rhi::Binding{.binding = 0, .resource = texture},
                                   nx::rhi::Binding{.binding = 1, .resource = sampler->get()}};
        auto group = device().createBindGroup(
            {.label = "test.sample", .layout = layout->get(), .bindings = resources});
        const std::array targets{nx::rhi::ColorTarget{.format = Format::Rgba8Unorm}};
        const std::array layouts{layout->get()};
        auto pipeline = device().createRenderPipeline(
            {.label = "test.sample",
             .vertex = {.shader = shader->get(), .entryPoint = "vertexMain"},
             .fragment = {.shader = shader->get(), .entryPoint = "fragmentMain"},
             .colorTargets = targets,
             .bindGroupLayouts = layouts});
        if (!group || !pipeline) {
            ADD_FAILURE() << "cannot create the bind group or the pipeline";
            return {};
        }
        return drawAndRead(extent, [&](CommandList& commands) {
            commands.setPipeline(pipeline->get());
            commands.setBindGroup(0, group->get());
            commands.draw(3, 1, 0, 0);
        });
    }

private:
    std::unique_ptr<Device> m_device;
};

TEST_F(DrawTest, ClearsATargetAndReadsItBack) {
    const auto pixels = drawAndRead({.width = 4, .height = 4}, [](CommandList&) {});
    EXPECT_EQ(pixels, std::vector<Pixel>(16, kBlack));
}

/// The fill pipeline: a triangle from a vertex buffer, in the color of a uniform buffer.
struct FillPipeline {
    nx::rhi::OwnedShader shader;
    nx::rhi::OwnedBindGroupLayout layout;
    nx::rhi::OwnedRenderPipeline pipeline;
};

[[nodiscard]] auto createFillPipeline(Device& device, std::optional<nx::rhi::DepthState> depth)
    -> std::optional<FillPipeline> {
    const std::vector<std::byte> code = shaderCode("fill.spv");
    auto shader = device.createShader({.label = "test.fill", .code = code});
    const std::array bindings{nx::rhi::BindingLayout{.binding = 0,
                                                     .type = nx::rhi::BindingType::UniformBuffer,
                                                     .stages = {nx::rhi::ShaderStage::Fragment}}};
    auto layout = device.createBindGroupLayout({.label = "test.fill", .bindings = bindings});
    if (!shader || !layout) {
        ADD_FAILURE() << "cannot create the fill shader or its layout";
        return std::nullopt;
    }
    const std::array attributes{
        nx::rhi::VertexAttribute{.location = 0, .format = nx::rhi::VertexFormat::Float2, .offset = 0}};
    const std::array vertexBuffers{
        nx::rhi::VertexBufferLayout{.stride = 2 * sizeof(float), .attributes = attributes}};
    const std::array targets{nx::rhi::ColorTarget{.format = Format::Rgba8Unorm}};
    const std::array layouts{layout->get()};
    auto pipeline =
        device.createRenderPipeline({.label = "test.fill",
                                     .vertex = {.shader = shader->get(), .entryPoint = "vertexMain"},
                                     .fragment = {.shader = shader->get(), .entryPoint = "fragmentMain"},
                                     .vertexBuffers = vertexBuffers,
                                     .colorTargets = targets,
                                     .depth = depth,
                                     .bindGroupLayouts = layouts});
    if (!pipeline) {
        ADD_FAILURE() << nx::describe(pipeline.error());
        return std::nullopt;
    }
    return FillPipeline{
        .shader = std::move(*shader), .layout = std::move(*layout), .pipeline = std::move(*pipeline)};
}

/// A uniform buffer of a color, and the bind group of the fill pipeline that binds it, which needs the
/// buffer for as long as it is drawn with.
struct FillGroup {
    OwnedBuffer color;
    nx::rhi::OwnedBindGroup group;
};

[[nodiscard]] auto fillGroup(Device& device, const FillPipeline& fill, std::array<float, 4> color)
    -> std::optional<FillGroup> {
    auto buffer = device.createBuffer({.label = "test.fill.color",
                                       .size = sizeof(color),
                                       .usage = {BufferUsage::Uniform},
                                       .location = MemoryLocation::Upload});
    if (!buffer) {
        ADD_FAILURE() << nx::describe(buffer.error());
        return std::nullopt;
    }
    std::ranges::copy(std::as_bytes(std::span{color}), device.mappedBytes(buffer->get()).begin());
    const std::array bindings{
        nx::rhi::Binding{.binding = 0, .resource = nx::rhi::BufferBinding{.buffer = buffer->get()}}};
    auto group = device.createBindGroup(
        {.label = "test.fill.color", .layout = fill.layout.get(), .bindings = bindings});
    if (!group) {
        ADD_FAILURE() << nx::describe(group.error());
        return std::nullopt;
    }
    return FillGroup{.color = std::move(*buffer), .group = std::move(*group)};
}

/// A triangle over the whole target: clip space (-1, -1), (3, -1), (-1, 3).
constexpr std::array<float, 6> kCoveringTriangle{-1.0F, -1.0F, 3.0F, -1.0F, -1.0F, 3.0F};

TEST_F(DrawTest, DrawsAVertexBufferInAUniformColor) {
    const auto fill = createFillPipeline(device(), std::nullopt);
    ASSERT_TRUE(fill.has_value());
    const auto group = fillGroup(device(), *fill, {0.0F, 1.0F, 0.0F, 1.0F});
    auto vertices =
        upload("test.vertices", {BufferUsage::Vertex}, std::as_bytes(std::span{kCoveringTriangle}));
    ASSERT_TRUE(group.has_value());
    ASSERT_TRUE(vertices.has_value()) << nx::describe(vertices.error());

    const auto pixels = drawAndRead({.width = 4, .height = 4}, [&](CommandList& commands) {
        commands.setPipeline(fill->pipeline.get());
        commands.setBindGroup(0, group->group.get());
        commands.setVertexBuffer(0, vertices->get(), 0);
        commands.draw(3, 1, 0, 0);
    });
    EXPECT_EQ(pixels, std::vector<Pixel>(16, kGreen));
}

TEST_F(DrawTest, ClipSpaceYPointsUp) {
    const auto fill = createFillPipeline(device(), std::nullopt);
    ASSERT_TRUE(fill.has_value());
    const auto group = fillGroup(device(), *fill, {1.0F, 1.0F, 1.0F, 1.0F});
    // The upper half of clip space, y from 0 to 1.
    constexpr std::array<float, 12> kUpperHalf{-1.0F, 0.0F, 1.0F, 0.0F, 1.0F,  1.0F,
                                               -1.0F, 0.0F, 1.0F, 1.0F, -1.0F, 1.0F};
    auto vertices = upload("test.vertices", {BufferUsage::Vertex}, std::as_bytes(std::span{kUpperHalf}));
    ASSERT_TRUE(group.has_value());
    ASSERT_TRUE(vertices.has_value());

    const auto pixels = drawAndRead({.width = 2, .height = 2}, [&](CommandList& commands) {
        commands.setPipeline(fill->pipeline.get());
        commands.setBindGroup(0, group->group.get());
        commands.setVertexBuffer(0, vertices->get(), 0);
        commands.draw(6, 1, 0, 0);
    });
    EXPECT_EQ(pixels, (std::vector<Pixel>{kWhite, kWhite, kBlack, kBlack})) << "the top row is drawn";
}

TEST_F(DrawTest, SamplesAnUploadedTexture) {
    constexpr Extent2d kExtent{.width = 2, .height = 2};
    const std::array texels{kRed, kGreen, kBlue, kWhite}; // row after row, from the top
    auto texture = device().createTexture({.label = "test.texture",
                                           .format = Format::Rgba8Unorm,
                                           .extent = kExtent,
                                           .usage = {TextureUsage::Sampled, TextureUsage::CopyDestination}});
    ASSERT_TRUE(texture.has_value()) << nx::describe(texture.error());
    uploadTexels(texture->get(), 0, kExtent, texels);

    EXPECT_EQ(drawSampled("sample.spv", texture->get(), kExtent),
              (std::vector<Pixel>{texels.begin(), texels.end()}))
        << "one texel per pixel, top row first";
}

TEST_F(DrawTest, SamplesALayerOfATextureArray) {
    constexpr Extent2d kExtent{.width = 2, .height = 2};
    const std::array firstLayer{kRed, kRed, kRed, kRed};
    const std::array secondLayer{kGreen, kBlue, kWhite, kBlack};
    auto texture = device().createTexture({.label = "test.layers",
                                           .format = Format::Rgba8Unorm,
                                           .extent = kExtent,
                                           .arrayLayers = 2,
                                           .usage = {TextureUsage::Sampled, TextureUsage::CopyDestination}});
    ASSERT_TRUE(texture.has_value()) << nx::describe(texture.error());
    uploadTexels(texture->get(), 0, kExtent, firstLayer);
    uploadTexels(texture->get(), 1, kExtent, secondLayer);

    EXPECT_EQ(drawSampled("sampleLayer.spv", texture->get(), kExtent),
              (std::vector<Pixel>{secondLayer.begin(), secondLayer.end()}))
        << "layer 1, one texel per pixel";
}

TEST_F(DrawTest, TheDepthTestKeepsTheFirstOfTwoEqualDepths) {
    constexpr Extent2d kExtent{.width = 4, .height = 4};
    const auto fill = createFillPipeline(
        device(), nx::rhi::DepthState{.isWriteEnabled = true, .compare = nx::rhi::CompareOp::Less});
    ASSERT_TRUE(fill.has_value());
    const auto red = fillGroup(device(), *fill, {1.0F, 0.0F, 0.0F, 1.0F});
    const auto blue = fillGroup(device(), *fill, {0.0F, 0.0F, 1.0F, 1.0F});
    auto vertices =
        upload("test.vertices", {BufferUsage::Vertex}, std::as_bytes(std::span{kCoveringTriangle}));
    auto depth = device().createTexture({.label = "test.depth",
                                         .format = Format::Depth32Float,
                                         .extent = kExtent,
                                         .usage = {TextureUsage::DepthTarget}});
    ASSERT_TRUE(red && blue && vertices && depth);

    const auto pixels = drawAndRead(
        kExtent,
        [&](CommandList& commands) {
            commands.setPipeline(fill->pipeline.get());
            commands.setVertexBuffer(0, vertices->get(), 0);
            commands.setBindGroup(0, red->group.get());
            commands.draw(3, 1, 0, 0); // depth 0 passes the cleared 1
            commands.setBindGroup(0, blue->group.get());
            commands.draw(3, 1, 0, 0); // depth 0 is not less than 0
        },
        depth->get());
    EXPECT_EQ(pixels, std::vector<Pixel>(16, kRed));
}

/// The float a half float's bits are.
[[nodiscard]] auto floatOfHalf(std::uint16_t half) -> float {
    constexpr int kMantissaBits = 10;
    constexpr std::uint16_t kExponentMask = 0x1F;
    constexpr std::uint16_t kMantissaMask = 0x3FF;
    constexpr int kExponentBias = 15;
    const int exponent = (half >> kMantissaBits) & kExponentMask;
    const auto mantissa = static_cast<float>(half & kMantissaMask) / static_cast<float>(1 << kMantissaBits);
    const float magnitude = exponent == 0 ? std::ldexp(mantissa, 1 - kExponentBias)
                                          : std::ldexp(1.0F + mantissa, exponent - kExponentBias);
    constexpr std::uint16_t kSignBit = 0x8000;
    return (half & kSignBit) != 0 ? -magnitude : magnitude;
}

/// The color a pass clears `layer`'s `mip` of a cube texture of half floats to, read back.
TEST_F(DrawTest, APassDrawsToAMipLevelOfALayerOfACube) {
    constexpr Extent2d kFace{.width = 4, .height = 4};
    constexpr Extent2d kMip{.width = 2, .height = 2};
    constexpr std::uint32_t kLayer = 3;
    auto cube = device().createTexture(
        {.label = "test.cube",
         .format = Format::Rgba16Float,
         .extent = kFace,
         .mipLevels = 2,
         .arrayLayers = nx::rhi::kCubeFaces,
         .usage = {TextureUsage::Sampled, TextureUsage::RenderTarget, TextureUsage::CopySource},
         .isCube = true});
    ASSERT_TRUE(cube.has_value()) << nx::describe(cube.error());
    const std::uint64_t size =
        std::uint64_t{kMip.width} * kMip.height * nx::rhi::bytesPerTexel(Format::Rgba16Float);
    auto readback = device().createBuffer({.label = "test.readback",
                                           .size = size,
                                           .usage = {BufferUsage::CopyDestination},
                                           .location = MemoryLocation::Readback});
    ASSERT_TRUE(readback.has_value()) << nx::describe(readback.error());
    runFrame([&](CommandList& commands) {
        commands.transition(cube->get(), TextureState::Undefined, TextureState::RenderTarget);
        const std::array colors{
            nx::rhi::ColorAttachment{.texture = cube->get(),
                                     .clear = {.red = 4.0F, .green = 1.5F, .blue = 0.25F, .alpha = 1.0F},
                                     .mipLevel = 1,
                                     .arrayLayer = kLayer}};
        commands.beginRendering({.colors = colors, .area = kMip});
        commands.endRendering();
        commands.transition(cube->get(), TextureState::RenderTarget, TextureState::CopySource);
        commands.transition(readback->get(), BufferState::Undefined, BufferState::CopyDestination);
        commands.copyTextureToBuffer({.buffer = readback->get(),
                                      .texture = cube->get(),
                                      .mipLevel = 1,
                                      .arrayLayer = kLayer,
                                      .extent = kMip});
        commands.transition(readback->get(), BufferState::CopyDestination, BufferState::HostRead);
    });
    const std::span<const std::byte> bytes = device().mappedBytes(readback->get());
    std::vector<std::uint16_t> halves(nx::narrow<std::size_t>(size / sizeof(std::uint16_t)));
    std::ranges::copy(bytes, std::as_writable_bytes(std::span{halves}).begin());
    for (std::size_t texel = 0; texel < halves.size(); texel += 4) {
        EXPECT_EQ(floatOfHalf(halves[texel]), 4.0F) << "light beyond 1 is kept";
        EXPECT_EQ(floatOfHalf(halves[texel + 1]), 1.5F);
        EXPECT_EQ(floatOfHalf(halves[texel + 2]), 0.25F);
        EXPECT_EQ(floatOfHalf(halves[texel + 3]), 1.0F);
    }
}

TEST_F(DrawTest, ACubeIsSquareItsLayersSixBySixAndAnHdrTargetIsBlendable) {
    auto target = device().createTexture({.label = "test.hdr",
                                          .format = Format::Rg11b10Ufloat,
                                          .extent = {.width = 8, .height = 8},
                                          .usage = {TextureUsage::Sampled, TextureUsage::RenderTarget}});
    EXPECT_TRUE(target.has_value()) << "every desktop GPU blends to it: " << nx::describe(target.error());
    auto cubes = device().createTexture({.label = "test.cubes",
                                         .format = Format::Rg11b10Ufloat,
                                         .extent = {.width = 8, .height = 8},
                                         .mipLevels = 4,
                                         .arrayLayers = 2 * nx::rhi::kCubeFaces,
                                         .usage = {TextureUsage::Sampled, TextureUsage::RenderTarget},
                                         .isCube = true});
    EXPECT_TRUE(cubes.has_value()) << "a cube array: " << nx::describe(cubes.error());
}

TEST_F(DrawTest, ShaderCodeThatIsNotSpirvIsRefused) {
    const std::array code{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    const auto shader = device().createShader({.label = "test.garbage", .code = code});
    ASSERT_FALSE(shader.has_value());
    EXPECT_EQ(shader.error().code, nx::rhi::RhiError::InvalidShader);
}

} // namespace
