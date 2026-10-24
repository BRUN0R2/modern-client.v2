// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// BMP (Windows bitmap), the textures studio models read from beside them (Counter-Strike Online's
/// `models/texture/#WWWHHH*.bmp`) and a texture format of mods (docs/06-assets-and-resources.md,
/// sections 3.1 and 3.2): uncompressed images of 8-bit palette indices or of 24- or 32-bit colors,
/// rows from the bottom or from the top, decoded to RGBA.
export module nx.assets:bmp;

import std;
import nx.core;
import :tga;

export namespace nx::assets {

/// What can be wrong with a BMP file; the detail has the value.
enum class BmpError : std::uint8_t {
    TooSmall,    ///< Smaller than its headers and its palette.
    Unsupported, ///< A compression or a pixel depth that is not decoded.
    BadValue,    ///< A header value outside its domain, or a pixel's index outside the palette.
    Truncated,   ///< The pixels end before the image does.
};

/// The image of a BMP file of a BITMAPINFOHEADER or a later one: 8-bit indices into its palette, or
/// 24- or 32-bit colors, uncompressed (BI_RGB), each row padded to four bytes, from the bottom row up
/// unless its height is negative. Opaque: the fourth byte of a 32-bit pixel is reserved in BI_RGB.
/// Errors: every BmpError.
[[nodiscard]] auto readBmp(std::span<const std::byte> bytes) -> Result<RgbaImage, BmpError>;

} // namespace nx::assets
