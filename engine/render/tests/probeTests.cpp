// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The production GGX prefilter reads, averages and stores synthetic directional radiance in linear
// light, including HDR capture texels. No game assets are needed.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.rhi;
import nx.rhi.vulkan;

#include "offscreenTarget.hpp"

namespace {

using nx::rhi::TextureState;
using nx::test::Pixel;

constexpr std::uint32_t kCaptureSize = 32;
constexpr nx::rhi::Extent2d kCaptureExtent{.width = kCaptureSize, .height = kCaptureSize};
constexpr nx::rhi::Extent2d kOnePixel{.width = 1, .height = 1};
constexpr std::uint32_t kUniformBinding = 0;
constexpr std::uint32_t kCaptureBinding = 1;
constexpr std::uint32_t kSamplerBinding = 2;
constexpr std::uint32_t kPassGroup = 0;
constexpr std::uint32_t kCoveringVertices = 3;
constexpr std::size_t kRgbChannels = 3;
constexpr float kChannelTop = 255.0F;
constexpr float kHdrRadiance = 1.5F;
constexpr float kAngularSamplingTolerance = 0.05F;
constexpr Pixel kConstantLinearColor{48, 128, 200, 255};

// The x component of each face's direction, with the +Z/-Z faces oriented toward +X at the top.
[[nodiscard]] auto directionXOf(std::uint32_t side, std::uint32_t x, std::uint32_t y) -> float {
    const float s = (2.0F * ((static_cast<float>(x) + 0.5F) / kCaptureSize)) - 1.0F;
    const float t = 1.0F - (2.0F * ((static_cast<float>(y) + 0.5F) / kCaptureSize));
    switch (side) {
    case 0:
        return 1.0F;
    case 1:
        return -1.0F;
    case 2:
        return s;
    case 3:
        return -s;
    case 4:
        return -t;
    default:
        return t;
    }
}

[[nodiscard]] auto captureTexels(bool halfBright) -> std::vector<std::byte> {
    std::vector<std::byte> texels;
    texels.reserve(std::size_t{nx::rhi::kCubeFaces} * kCaptureSize * kCaptureSize * nx::test::kPixelBytes);
    for (std::uint32_t side = 0; side < nx::rhi::kCubeFaces; ++side) {
        for (std::uint32_t y = 0; y < kCaptureSize; ++y) {
            for (std::uint32_t x = 0; x < kCaptureSize; ++x) {
                const int intensity = directionXOf(side, x, y) >= 0.0F ? 255 : 0;
                const Pixel color =
                    halfBright ? Pixel{intensity, intensity, intensity, 255} : kConstantLinearColor;
                for (const int channel : color) {
                    texels.push_back(static_cast<std::byte>(channel));
                }
            }
        }
    }
    return texels;
}

[[nodiscard]] auto hdrCaptureTexels() -> std::vector<std::byte> {
    // Binary16 encodings: radiance 1.5 and alpha 1.0. Half the sphere emits, half is black.
    constexpr std::uint16_t kBrightHalf = 0x3e00;
    constexpr std::uint16_t kOpaqueHalf = 0x3c00;
    std::vector<std::byte> texels;
    for (std::uint32_t side = 0; side < nx::rhi::kCubeFaces; ++side) {
        for (std::uint32_t y = 0; y < kCaptureSize; ++y) {
            for (std::uint32_t x = 0; x < kCaptureSize; ++x) {
                const std::uint16_t intensity = directionXOf(side, x, y) >= 0.0F ? kBrightHalf : 0;
                const std::array color{intensity, intensity, intensity, kOpaqueHalf};
                const auto bytes = std::as_bytes(std::span{color});
                texels.insert(texels.end(), bytes.begin(), bytes.end());
            }
        }
    }
    return texels;
}

// Matches shaders/probe.slang's Pass. A +Z output texel has equal energy in the two x hemispheres.
struct PassUniform {
    float side = 4.0F;
    float roughness = 1.0F;
    float sampleCount = 256.0F;
    float padding = 0.0F;
};

class ProbePrefilterTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto device =
            nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
        if (!device) {
            const auto code = device.error().code;
            EXPECT_TRUE(code == nx::rhi::RhiError::BackendUnavailable ||
                        code == nx::rhi::RhiError::NoSuitableAdapter)
                << nx::describe(device.error());
            GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
        }
        devicePtr = std::move(*device);
        const auto executable = nx::platform::executablePath();
        ASSERT_TRUE(executable.has_value()) << nx::describe(executable.error());
        const auto code = nx::platform::readFile(executable->parent_path() / "shaders" / "probe.spv");
        ASSERT_TRUE(code.has_value()) << nx::describe(code.error());
        auto shader = devicePtr->createShader({.label = "test.probe", .code = *code});
        ASSERT_TRUE(shader.has_value()) << nx::describe(shader.error());
        shaderPtr = std::move(*shader);
        auto sampler = devicePtr->createSampler({.label = "test.probe.linearClamp",
                                                 .mipmap = nx::rhi::Filter::Nearest,
                                                 .address = nx::rhi::AddressMode::ClampToEdge});
        ASSERT_TRUE(sampler.has_value()) << nx::describe(sampler.error());
        samplerPtr = std::move(*sampler);
        const nx::Flags<nx::rhi::ShaderStage> fragment{nx::rhi::ShaderStage::Fragment};
        const std::array bindings{
            nx::rhi::BindingLayout{
                .binding = kUniformBinding, .type = nx::rhi::BindingType::UniformBuffer, .stages = fragment},
            nx::rhi::BindingLayout{
                .binding = kCaptureBinding, .type = nx::rhi::BindingType::SampledTexture, .stages = fragment},
            nx::rhi::BindingLayout{
                .binding = kSamplerBinding, .type = nx::rhi::BindingType::Sampler, .stages = fragment}};
        auto layout = devicePtr->createBindGroupLayout({.label = "test.probe", .bindings = bindings});
        ASSERT_TRUE(layout.has_value()) << nx::describe(layout.error());
        layoutPtr = std::move(*layout);
        auto uniform = devicePtr->createBuffer({.label = "test.probe.pass",
                                                .size = sizeof(PassUniform),
                                                .usage = {nx::rhi::BufferUsage::Uniform},
                                                .location = nx::rhi::MemoryLocation::Upload});
        ASSERT_TRUE(uniform.has_value()) << nx::describe(uniform.error());
        uniformPtr = std::move(*uniform);
        const std::array targets{nx::rhi::ColorTarget{.format = nx::rhi::Format::Rgba8Unorm}};
        const std::array layouts{layoutPtr.get()};
        auto pipeline = devicePtr->createRenderPipeline(
            {.label = "test.probe.prefilter",
             .vertex = {.shader = shaderPtr.get(), .entryPoint = "fullscreenMain"},
             .fragment = {.shader = shaderPtr.get(), .entryPoint = "prefilterMain"},
             .colorTargets = targets,
             .depth = nx::rhi::DepthState{.isWriteEnabled = false, .compare = nx::rhi::CompareOp::Always},
             .bindGroupLayouts = layouts});
        ASSERT_TRUE(pipeline.has_value()) << nx::describe(pipeline.error());
        pipelinePtr = std::move(*pipeline);
        target.emplace(*devicePtr);
    }

    void TearDown() override {
        if (devicePtr) {
            devicePtr->waitIdle();
            EXPECT_EQ(devicePtr->validationErrors(), 0U);
        }
    }

    void loadCapture(std::span<const std::byte> texels,
                     nx::rhi::Format format = nx::rhi::Format::Rgba8Unorm) {
        auto texture = devicePtr->createTexture(
            {.label = "test.probe.capture",
             .format = format,
             .extent = kCaptureExtent,
             .arrayLayers = nx::rhi::kCubeFaces,
             .usage = {nx::rhi::TextureUsage::Sampled, nx::rhi::TextureUsage::CopyDestination}});
        ASSERT_TRUE(texture.has_value()) << nx::describe(texture.error());
        capturePtr = std::move(*texture);
        auto staging = devicePtr->createBuffer({.label = "test.probe.staging",
                                                .size = texels.size(),
                                                .usage = {nx::rhi::BufferUsage::CopySource},
                                                .location = nx::rhi::MemoryLocation::Upload});
        ASSERT_TRUE(staging.has_value()) << nx::describe(staging.error());
        std::ranges::copy(texels, devicePtr->mappedBytes(staging->get()).begin());
        auto frame = devicePtr->beginFrame();
        ASSERT_TRUE(frame.has_value()) << nx::describe(frame.error());
        auto& commands = frame->get();
        commands.transition(staging->get(), nx::rhi::BufferState::Undefined,
                            nx::rhi::BufferState::CopySource);
        commands.transition(capturePtr.get(), TextureState::Undefined, TextureState::CopyDestination);
        const std::uint64_t sideBytes =
            std::uint64_t{kCaptureSize} * kCaptureSize * nx::rhi::bytesPerTexel(format);
        for (std::uint32_t side = 0; side < nx::rhi::kCubeFaces; ++side) {
            commands.copyBufferToTexture({.buffer = staging->get(),
                                          .bufferOffset = side * sideBytes,
                                          .texture = capturePtr.get(),
                                          .arrayLayer = side,
                                          .extent = kCaptureExtent});
        }
        commands.transition(capturePtr.get(), TextureState::CopyDestination, TextureState::ShaderRead);
        const auto submitted = devicePtr->submitFrame();
        ASSERT_TRUE(submitted.has_value()) << nx::describe(submitted.error());
        devicePtr->waitIdle();
        const std::array bindings{
            nx::rhi::Binding{
                .binding = kUniformBinding,
                .resource = nx::rhi::BufferBinding{.buffer = uniformPtr.get(), .size = sizeof(PassUniform)}},
            nx::rhi::Binding{.binding = kCaptureBinding, .resource = capturePtr.get()},
            nx::rhi::Binding{.binding = kSamplerBinding, .resource = samplerPtr.get()}};
        auto group = devicePtr->createBindGroup(
            {.label = "test.probe.pass", .layout = layoutPtr.get(), .bindings = bindings});
        ASSERT_TRUE(group.has_value()) << nx::describe(group.error());
        groupPtr = std::move(*group);
    }

    [[nodiscard]] auto filtered(float roughness) -> Pixel {
        const PassUniform uniform{.roughness = roughness};
        std::ranges::copy(std::as_bytes(std::span{&uniform, 1}),
                          devicePtr->mappedBytes(uniformPtr.get()).begin());
        const auto pixels = target->draw([&](nx::rhi::CommandList& commands) {
            // A one-texel viewport places its sample exactly at the +Z face's center.
            commands.setViewport({.size = kOnePixel});
            commands.setScissor({.size = kOnePixel});
            commands.setPipeline(pipelinePtr.get());
            commands.setBindGroup(kPassGroup, groupPtr.get());
            commands.draw(kCoveringVertices, 1, 0, 0);
        });
        EXPECT_FALSE(pixels.empty());
        return pixels.empty() ? Pixel{} : nx::test::pixelAt(pixels, 0, 0);
    }

private:
    std::unique_ptr<nx::rhi::Device> devicePtr;
    nx::rhi::OwnedShader shaderPtr;
    nx::rhi::OwnedSampler samplerPtr;
    nx::rhi::OwnedBindGroupLayout layoutPtr;
    nx::rhi::OwnedBuffer uniformPtr;
    nx::rhi::OwnedRenderPipeline pipelinePtr;
    nx::rhi::OwnedTexture capturePtr;
    nx::rhi::OwnedBindGroup groupPtr;
    std::optional<nx::test::OffscreenTarget> target;
};

TEST_F(ProbePrefilterTest, AveragesDirectionalRadianceInLinearLight) {
    ASSERT_NO_FATAL_FAILURE(loadCapture(captureTexels(true)));
    const Pixel pixel = filtered(1.0F);
    constexpr float kExpected = 0.5F * kChannelTop;
    // Finite GGX sampling and bilinear reads across the hemisphere boundary contribute discretization.
    constexpr float kSampleTolerance = kAngularSamplingTolerance * kChannelTop;
    for (std::size_t channel = 0; channel < kRgbChannels; ++channel) {
        EXPECT_NEAR(pixel[channel], kExpected, kSampleTolerance);
    }
    EXPECT_EQ(pixel[3], 255);
}

TEST_F(ProbePrefilterTest, PreservesConstantLinearRadianceAtEveryRoughness) {
    ASSERT_NO_FATAL_FAILURE(loadCapture(captureTexels(false)));
    constexpr std::array kRoughnesses{0.0F, 0.35F, 1.0F};
    constexpr int kByteTolerance = 1;
    for (const float roughness : kRoughnesses) {
        SCOPED_TRACE(roughness);
        const Pixel pixel = filtered(roughness);
        for (std::size_t channel = 0; channel < kRgbChannels; ++channel) {
            EXPECT_NEAR(pixel[channel], kConstantLinearColor[channel], kByteTolerance);
        }
        EXPECT_EQ(pixel[3], 255);
    }
}

TEST_F(ProbePrefilterTest, AveragesHdrRadianceBeforeTheDisplayEncoding) {
    ASSERT_NO_FATAL_FAILURE(loadCapture(hdrCaptureTexels(), nx::rhi::Format::Rgba16Float));
    const Pixel pixel = filtered(1.0F);
    constexpr float kExpected = 0.5F * kHdrRadiance * kChannelTop;
    // The finite sampler has the same angular error as the unit-radiance hemisphere case.
    constexpr float kSampleTolerance = kAngularSamplingTolerance * kHdrRadiance * kChannelTop;
    for (std::size_t channel = 0; channel < kRgbChannels; ++channel) {
        EXPECT_NEAR(pixel[channel], kExpected, kSampleTolerance);
    }
    EXPECT_EQ(pixel[3], 255);
}

} // namespace
