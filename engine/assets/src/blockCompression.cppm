// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Block-compressed images (docs/06-assets-and-resources.md, section 3.1): BC3, DirectX's DXT5, the
/// frames of Counter-Strike Online's sprites (nx.assets:spr), decoded to RGBA as Direct3D decodes it.
export module nx.assets:blockCompression;

import std;
import nx.core;
import :image;

export namespace nx::assets {

/// The side of a block, in pixels: a block holds 4x4 of them.
inline constexpr std::uint32_t kBlockSide = 4;
/// The bytes of a BC3 block: its alpha (2 bytes of endpoints, 6 of 3-bit indices), then its color
/// (2 endpoints of 16 bits, 5-6-5, then 4 bytes of 2-bit indices), little-endian.
inline constexpr std::size_t kBc3BlockBytes = 16;

/// The bytes of the BC3 blocks of an image of `width` by `height` pixels: rows of blocks from the top,
/// each block from the left, the last row and column of blocks cut where the image ends.
[[nodiscard]] constexpr auto bc3Bytes(std::uint32_t width, std::uint32_t height) noexcept -> std::uint64_t {
    return std::uint64_t{divideRoundingUp(width, kBlockSide)} * divideRoundingUp(height, kBlockSide) *
           kBc3BlockBytes;
}

/// The image the BC3 `blocks` of `width` by `height` pixels make, which must be bc3Bytes of them: its
/// colors the two endpoints and the two between, at a third and two thirds; its alpha the two
/// endpoints and six between, or, when the first is not above the second, four between, 0 and 255.
/// The interpolations are rounded down.
[[nodiscard]] auto decodeBc3(std::span<const std::byte> blocks, std::uint32_t width, std::uint32_t height)
    -> RgbaImage;

} // namespace nx::assets
