// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Test helper: an offscreen target of the tests that draw (the renderers', the interface's), its
// depth and the buffer it is read back to, and what is drawn on it read back. Include after
// <gtest/gtest.h>, `import std;`, `import nx.core;` and `import nx.rhi;`.

namespace nx::test {

/// The size of the target.
inline constexpr nx::rhi::Extent2d kTargetExtent{.width = 32, .height = 32};
/// The bytes of a pixel read back: red, green, blue and alpha.
inline constexpr std::size_t kPixelBytes = 4;

/// A pixel read back, each channel from 0 to 255.
using Pixel = std::array<int, kPixelBytes>;

/// The pixel at `x`, `y` of `pixels`, read back from the target.
[[nodiscard]] inline auto pixelAt(std::span<const std::byte> pixels, std::uint32_t x, std::uint32_t y)
    -> Pixel {
    const std::size_t offset = ((std::size_t{y} * kTargetExtent.width) + x) * kPixelBytes;
    return {std::to_integer<int>(pixels[offset]), std::to_integer<int>(pixels[offset + 1]),
            std::to_integer<int>(pixels[offset + 2]), std::to_integer<int>(pixels[offset + 3])};
}

/// Runs `record` in a frame of its own and waits for the GPU: what makes resources and uploads them.
template <class Record>
[[nodiscard]] auto inFrameOfItsOwn(nx::rhi::Device& device, const Record& record) {
    auto commands = device.beginFrame();
    EXPECT_TRUE(commands.has_value());
    auto made = record(commands->get());
    EXPECT_TRUE(device.submitFrame().has_value());
    device.waitIdle();
    return made;
}

class OffscreenTarget {
public:
    explicit OffscreenTarget(nx::rhi::Device& device) : m_device{&device} {
        auto color = device.createTexture(
            {.label = "test.target",
             .format = nx::rhi::Format::Rgba8Unorm,
             .extent = kTargetExtent,
             .usage = {nx::rhi::TextureUsage::RenderTarget, nx::rhi::TextureUsage::CopySource}});
        auto depth = device.createTexture({.label = "test.depth",
                                           .format = nx::rhi::Format::Depth32Float,
                                           .extent = kTargetExtent,
                                           .usage = {nx::rhi::TextureUsage::DepthTarget}});
        auto readback = device.createBuffer(
            {.label = "test.readback",
             .size = std::uint64_t{kTargetExtent.width} * kTargetExtent.height * kPixelBytes,
             .usage = {nx::rhi::BufferUsage::CopyDestination},
             .location = nx::rhi::MemoryLocation::Readback});
        EXPECT_TRUE(color && depth && readback);
        if (color && depth && readback) {
            m_color = std::move(*color);
            m_depth = std::move(*depth);
            m_readback = std::move(*readback);
        }
    }

    /// Records what `record` draws in a frame of its own, in a render pass on the target cleared to an
    /// alpha of 0 and its depth to 0, the far end of reversed depth; then the pixels, RGBA rows from
    /// the top.
    [[nodiscard]] auto draw(const std::function<void(nx::rhi::CommandList&)>& record)
        -> std::vector<std::byte> {
        using nx::rhi::BufferState;
        using nx::rhi::TextureState;
        auto commands = m_device->beginFrame();
        EXPECT_TRUE(commands.has_value());
        if (!commands) {
            return {};
        }
        nx::rhi::CommandList& list = commands->get();
        list.transition(m_color.get(), TextureState::Undefined, TextureState::RenderTarget);
        list.transition(m_depth.get(), TextureState::Undefined, TextureState::DepthTarget);
        const std::array colors{nx::rhi::ColorAttachment{.texture = m_color.get(), .clear = {.alpha = 0.0F}}};
        list.beginRendering({.colors = colors,
                             .depth = nx::rhi::DepthAttachment{.texture = m_depth.get(), .clear = 0.0F},
                             .area = kTargetExtent});
        record(list);
        list.endRendering();
        list.transition(m_color.get(), TextureState::RenderTarget, TextureState::CopySource);
        list.transition(m_readback.get(), BufferState::Undefined, BufferState::CopyDestination);
        list.copyTextureToBuffer(
            {.buffer = m_readback.get(), .texture = m_color.get(), .extent = kTargetExtent});
        list.transition(m_readback.get(), BufferState::CopyDestination, BufferState::HostRead);
        EXPECT_TRUE(m_device->submitFrame().has_value());
        m_device->waitIdle();
        return m_device->mappedBytes(m_readback.get()) | std::ranges::to<std::vector>();
    }

private:
    nx::rhi::Device* m_device;
    nx::rhi::OwnedTexture m_color;
    nx::rhi::OwnedTexture m_depth;
    nx::rhi::OwnedBuffer m_readback;
};

} // namespace nx::test
