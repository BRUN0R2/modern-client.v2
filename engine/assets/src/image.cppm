// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The pixels the readers decode pictures to and the renderers upload: 8-bit channels, colors of
/// red, green and blue, texels with an alpha after them (docs/06-assets-and-resources.md).
export module nx.assets:image;

import std;

export namespace nx::assets {

/// The bytes of a color, red, green and blue: a palette's entry, a lightmap's sample.
inline constexpr std::size_t kRgbBytes = 3;
/// The bytes of a texel, red, green, blue and alpha, its alpha after its color.
inline constexpr std::size_t kRgbaBytes = 4;
/// A channel at its most: a color at its brightest, an alpha that hides what is behind.
inline constexpr std::byte kFullChannel{255};

/// An image of 8-bit red, green, blue and alpha, row by row from the top-left corner.
struct RgbaImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::byte> pixels{}; ///< width * height * kRgbaBytes bytes.
};

} // namespace nx::assets
