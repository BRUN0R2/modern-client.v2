// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.render;

import std;
import nx.core;
import nx.rhi;

namespace nx::render {

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
