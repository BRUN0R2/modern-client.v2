// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// TGA (Truevision), the images of the sky (`gfx/env/*.tga`) and of the VGUI menus of GoldSrc, and
/// a texture format of mods (docs/06-assets-and-resources.md, sections 3.1 and 3.2): color-mapped,
/// true-color and grayscale images, raw or run-length encoded, decoded to RGBA.
export module nx.assets:tga;

import std;
import nx.core;
export import :image;

export namespace nx::assets {

/// What can be wrong with a TGA file; the detail has the value.
enum class TgaError : std::uint8_t {
    TooSmall,    ///< Smaller than its header, its ID and its color map.
    Unsupported, ///< An image type, pixel depth or color map entry size that is not decoded.
    BadValue,    ///< A header value outside its domain, or a pixel's color outside the color map.
    Truncated,   ///< The pixels end before the image does.
};

/// The image of a TGA file: color-mapped (8-bit indices into 24- or 32-bit colors), true-color (24
/// or 32 bits) or grayscale (8 bits, or 16 with alpha), raw or run-length encoded, from any corner.
/// The fourth byte of a 32-bit pixel is its alpha whatever the descriptor declares, as the
/// original reads it: the VGUI fonts of GoldSrc declare none. A run-length packet may continue on
/// the next row, and what a packet holds past the last pixel is ignored, as in the original.
/// Errors: every TgaError.
[[nodiscard]] auto readTga(std::span<const std::byte> bytes) -> Result<RgbaImage, TgaError>;

} // namespace nx::assets
