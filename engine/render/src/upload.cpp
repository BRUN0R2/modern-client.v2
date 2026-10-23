// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.render;

import std;
import nx.core;
import nx.rhi;

namespace nx::render {

namespace {

/// The bytes of an RGBA texel, and of its color.
constexpr std::size_t kRgbaBytes = 4;
constexpr std::size_t kColorBytes = 3;
/// The texels a texel of the next mip level is the mean of, two by two, and the bits that divide by
/// them.
constexpr std::uint32_t kMipStep = 2;
constexpr std::uint32_t kMipMeanShift = 2;
constexpr float kMipTexels = static_cast<float>(kMipStep * kMipStep);
/// A channel at its brightest.
constexpr float kChannelTop = 255.0F;
/// The gamma of the display the original's colors are made for (shaders/colorEncoding.slang).
constexpr float kDisplayGamma = 2.2F;
/// sRGB's transfer function (IEC 61966-2-1): a line near black, a power curve above it.
constexpr float kSrgbLinearThreshold = 0.0031308F;
constexpr float kSrgbEncodedThreshold = 0.04045F;
constexpr float kSrgbLinearSlope = 12.92F;
constexpr float kSrgbScale = 1.055F;
constexpr float kSrgbOffset = 0.055F;
constexpr float kSrgbGamma = 2.4F;

/// The four bytes of a channel a texel of the next mip level is made of.
using MipQuad = std::array<std::uint32_t, kMipStep * kMipStep>;

/// Linear light, from 0 to 1, as an sRGB byte.
[[nodiscard]] auto srgbByteOf(float light) -> std::byte {
    const float encoded = light <= kSrgbLinearThreshold
                              ? light * kSrgbLinearSlope
                              : (kSrgbScale * std::pow(light, 1.0F / kSrgbGamma)) - kSrgbOffset;
    return static_cast<std::byte>(std::lround(std::clamp(encoded, 0.0F, 1.0F) * kChannelTop));
}

/// The light of each sRGB byte, and the sRGB byte of each display byte.
struct SrgbTables {
    std::array<float, kChannelValues> lightOfSrgb{};
    std::array<std::byte, kChannelValues> srgbOfDisplay{};
};

[[nodiscard]] auto makeSrgbTables() -> SrgbTables {
    SrgbTables tables;
    for (std::size_t value = 0; value < kChannelValues; ++value) {
        const float share = static_cast<float>(value) / kChannelTop;
        tables.lightOfSrgb[value] = share <= kSrgbEncodedThreshold
                                        ? share / kSrgbLinearSlope
                                        : std::pow((share + kSrgbOffset) / kSrgbScale, kSrgbGamma);
        tables.srgbOfDisplay[value] = srgbByteOf(std::pow(share, kDisplayGamma));
    }
    return tables;
}

[[nodiscard]] auto srgbTables() -> const SrgbTables& {
    static const SrgbTables tables = makeSrgbTables();
    return tables;
}

/// The mean of a channel's bytes, as the original's GL_MipMap takes it.
[[nodiscard]] auto byteMeanOf(std::size_t /*channel*/, const MipQuad& above) -> std::byte {
    return std::byte{narrow<std::uint8_t>((above[0] + above[1] + above[2] + above[3]) >> kMipMeanShift)};
}

/// The mean of the light of sRGB colors, as sRGB; of alpha, a share, as byteMeanOf takes it.
[[nodiscard]] auto lightMeanOf(std::size_t channel, const MipQuad& above) -> std::byte {
    if (channel >= kColorBytes) {
        return byteMeanOf(channel, above);
    }
    const std::array<float, kChannelValues>& light = srgbTables().lightOfSrgb;
    return srgbByteOf((light[above[0]] + light[above[1]] + light[above[2]] + light[above[3]]) / kMipTexels);
}

/// Appends to `chain` the level below the `width` x `height` one that starts at `from`, each channel of
/// each texel `mean(channel, the four bytes above it)`.
template <class ChannelMean>
void addMipLevel(MipChain& chain, std::size_t from, std::uint32_t width, std::uint32_t height,
                 const ChannelMean& mean) {
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
                const MipQuad above{at(x * kMipStep, y * kMipStep, channel),
                                    at((x * kMipStep) + 1, y * kMipStep, channel),
                                    at(x * kMipStep, (y * kMipStep) + 1, channel),
                                    at((x * kMipStep) + 1, (y * kMipStep) + 1, channel)};
                chain.texels.push_back(mean(channel, above));
            }
        }
    }
}

/// `texels`, the first level of a `width` x `height` texture, and its levels below down to one texel,
/// each made by `mean`.
template <class ChannelMean>
[[nodiscard]] auto mipChainMadeBy(std::vector<std::byte> texels, std::uint32_t width, std::uint32_t height,
                                  const ChannelMean& mean) -> MipChain {
    MipChain chain{.texels = std::move(texels), .offsets = {0}};
    const auto levels = static_cast<std::uint32_t>(std::bit_width(std::max(width, height)));
    for (std::uint32_t level = 1; level < levels; ++level) {
        addMipLevel(chain, chain.offsets.back(), std::max(width >> (level - 1), 1U),
                    std::max(height >> (level - 1), 1U), mean);
    }
    return chain;
}

} // namespace

auto mipChainOf(std::vector<std::byte> texels, std::uint32_t width, std::uint32_t height) -> MipChain {
    return mipChainMadeBy(std::move(texels), width, height, byteMeanOf);
}

auto colorFormatOf(ColorEncoding encoding) noexcept -> rhi::Format {
    return encoding == ColorEncoding::Linear ? rhi::Format::Rgba8Srgb : rhi::Format::Rgba8Unorm;
}

auto colorTexelsOf(std::vector<std::byte> texels, ColorEncoding encoding) -> std::vector<std::byte> {
    if (encoding == ColorEncoding::Display) {
        return texels;
    }
    const std::array<std::byte, kChannelValues>& srgb = srgbTables().srgbOfDisplay;
    for (std::size_t at = 0; at < texels.size(); at += kRgbaBytes) {
        for (std::size_t channel = 0; channel < kColorBytes; ++channel) {
            texels[at + channel] = srgb[std::to_integer<std::size_t>(texels[at + channel])];
        }
    }
    return texels;
}

auto colorMipChainOf(std::vector<std::byte> texels, std::uint32_t width, std::uint32_t height,
                     ColorEncoding encoding) -> MipChain {
    if (encoding == ColorEncoding::Display) {
        return mipChainOf(std::move(texels), width, height);
    }
    return mipChainMadeBy(colorTexelsOf(std::move(texels), encoding), width, height, lightMeanOf);
}

Uploader::Uploader(rhi::Device& device, rhi::CommandList& commands) noexcept
    : m_device{&device}, m_commands{&commands} {}

auto Uploader::texture(std::string_view label, rhi::Extent2d extent, std::uint32_t mipLevels,
                       std::uint32_t layers, std::span<const std::byte> texels,
                       std::span<const std::size_t> offsets, rhi::Format format)
    -> Result<rhi::OwnedTexture, rhi::RhiError> {
    auto texture =
        m_device->createTexture({.label = label,
                                 .format = format,
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
