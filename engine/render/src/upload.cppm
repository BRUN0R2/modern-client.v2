// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the renderer puts on the GPU while it makes what it draws (not exported): textures and
/// buffers in GPU memory, filled by copies recorded in a command list.
export module nx.render:upload;

import std;
import nx.core;
import nx.rhi;

namespace nx::render {

/// Where the texels of a texture of one level and one layer start.
inline constexpr std::array<std::size_t, 1> kOneLevel{0};

/// What is uploaded while something is made, recorded in one command list: each upload's staging
/// buffer is let go when the uploader ends, and the device keeps it until the copies are done.
class Uploader {
public:
    Uploader(rhi::Device& device, rhi::CommandList& commands) noexcept;

    /// A texture for shaders to sample, of `layers` layers of `mipLevels` levels; `offsets` has where
    /// each level of each layer starts in `texels`, layer after layer.
    [[nodiscard]] auto texture(std::string_view label, rhi::Extent2d extent, std::uint32_t mipLevels,
                               std::uint32_t layers, std::span<const std::byte> texels,
                               std::span<const std::size_t> offsets)
        -> Result<rhi::OwnedTexture, rhi::RhiError>;

    /// A buffer in GPU memory for `usage`, holding `bytes`, left in `state`.
    [[nodiscard]] auto buffer(std::string_view label, rhi::BufferUsage usage, rhi::BufferState state,
                              std::span<const std::byte> bytes) -> Result<rhi::OwnedBuffer, rhi::RhiError>;

private:
    /// A new Upload buffer holding `bytes`, to copy from in this frame, kept until the uploader ends.
    [[nodiscard]] auto stage(std::string_view label, std::span<const std::byte> bytes)
        -> Result<rhi::BufferHandle, rhi::RhiError>;

    rhi::Device* m_device;
    rhi::CommandList* m_commands;
    std::vector<rhi::OwnedBuffer> m_staging;
};

} // namespace nx::render
