// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.render;

import std;
import nx.core;
import nx.rhi;

namespace nx::render {

namespace {

/// The bytes of an RGBA texel.
constexpr std::size_t kRgbaBytes = 4;
/// The texels a texel of the next mip level is the mean of, two by two, and the bits that divide by
/// them.
constexpr std::uint32_t kMipStep = 2;
constexpr std::uint32_t kMipMeanShift = 2;

/// Appends to `chain` the level below the `width` x `height` one that starts at `from`.
void addMipLevel(MipChain& chain, std::size_t from, std::uint32_t width, std::uint32_t height) {
    const std::uint32_t lowerWidth = std::max(width / kMipStep, 1U);
    const std::uint32_t lowerHeight = std::max(height / kMipStep, 1U);
    chain.offsets.push_back(chain.texels.size());
    const auto at = [&](std::uint32_t x, std::uint32_t y, std::size_t channel) {
        const std::size_t texel = (std::size_t{std::min(y, height - 1)} * width) + std::min(x, width - 1);
        return std::to_integer<std::uint32_t>(chain.texels[from + (texel * kRgbaBytes) + channel]);
    };
    for (std::uint32_t y = 0; y < lowerHeight; ++y) {
        for (std::uint32_t x = 0; x < lowerWidth; ++x) {
            for (std::size_t channel = 0; channel < kRgbaBytes; ++channel) {
                const std::uint32_t sum = at(x * kMipStep, y * kMipStep, channel) +
                                          at((x * kMipStep) + 1, y * kMipStep, channel) +
                                          at(x * kMipStep, (y * kMipStep) + 1, channel) +
                                          at((x * kMipStep) + 1, (y * kMipStep) + 1, channel);
                chain.texels.push_back(std::byte{narrow<std::uint8_t>(sum >> kMipMeanShift)});
            }
        }
    }
}

} // namespace

auto mipChainOf(std::vector<std::byte> texels, std::uint32_t width, std::uint32_t height) -> MipChain {
    MipChain chain{.texels = std::move(texels), .offsets = {0}};
    const auto levels = static_cast<std::uint32_t>(std::bit_width(std::max(width, height)));
    for (std::uint32_t level = 1; level < levels; ++level) {
        addMipLevel(chain, chain.offsets.back(), std::max(width >> (level - 1), 1U),
                    std::max(height >> (level - 1), 1U));
    }
    return chain;
}

Uploader::Uploader(rhi::Device& device, rhi::CommandList& commands) noexcept
    : m_device{&device}, m_commands{&commands} {}

auto Uploader::texture(std::string_view label, rhi::Extent2d extent, std::uint32_t mipLevels,
                       std::uint32_t layers, std::span<const std::byte> texels,
                       std::span<const std::size_t> offsets) -> Result<rhi::OwnedTexture, rhi::RhiError> {
    auto texture =
        m_device->createTexture({.label = label,
                                 .format = rhi::Format::Rgba8Unorm,
                                 .extent = extent,
                                 .mipLevels = mipLevels,
                                 .arrayLayers = layers,
                                 .usage = {rhi::TextureUsage::Sampled, rhi::TextureUsage::CopyDestination}});
    if (!texture) {
        return texture;
    }
    auto source = stage(label, texels);
    if (!source) {
        return std::unexpected{std::move(source.error())};
    }
    const rhi::TextureHandle target = texture->get();
    m_commands->transition(target, rhi::TextureState::Undefined, rhi::TextureState::CopyDestination);
    for (std::uint32_t layer = 0; layer < layers; ++layer) {
        for (std::uint32_t level = 0; level < mipLevels; ++level) {
            m_commands->copyBufferToTexture(
                {.buffer = *source,
                 .bufferOffset = offsets[(std::size_t{layer} * mipLevels) + level],
                 .texture = target,
                 .mipLevel = level,
                 .arrayLayer = layer,
                 .extent = {.width = std::max(extent.width >> level, 1U),
                            .height = std::max(extent.height >> level, 1U)}});
        }
    }
    m_commands->transition(target, rhi::TextureState::CopyDestination, rhi::TextureState::ShaderRead);
    return texture;
}

auto Uploader::buffer(std::string_view label, rhi::BufferUsage usage, rhi::BufferState state,
                      std::span<const std::byte> bytes) -> Result<rhi::OwnedBuffer, rhi::RhiError> {
    auto buffer = m_device->createBuffer({.label = label,
                                          .size = std::max<std::uint64_t>(bytes.size(), 1),
                                          .usage = {usage, rhi::BufferUsage::CopyDestination},
                                          .location = rhi::MemoryLocation::Device});
    if (!buffer) {
        return buffer;
    }
    auto source = stage(label, bytes);
    if (!source) {
        return std::unexpected{std::move(source.error())};
    }
    m_commands->transition(buffer->get(), rhi::BufferState::Undefined, rhi::BufferState::CopyDestination);
    m_commands->copyBuffer(*source, 0, buffer->get(), 0, bytes.size());
    m_commands->transition(buffer->get(), rhi::BufferState::CopyDestination, state);
    return buffer;
}

auto Uploader::stage(std::string_view label, std::span<const std::byte> bytes)
    -> Result<rhi::BufferHandle, rhi::RhiError> {
    auto buffer = m_device->createBuffer({.label = label,
                                          .size = std::max<std::uint64_t>(bytes.size(), 1),
                                          .usage = {rhi::BufferUsage::CopySource},
                                          .location = rhi::MemoryLocation::Upload});
    if (!buffer) {
        return std::unexpected{std::move(buffer.error())};
    }
    std::ranges::copy(bytes, m_device->mappedBytes(buffer->get()).begin());
    return m_staging.emplace_back(std::move(*buffer)).get();
}

} // namespace nx::render
