// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;

namespace nx::assets {

auto Palette::read(std::span<const std::byte> bytes) -> Result<Palette, PaletteError> {
    if (bytes.size() != kPaletteBytes) {
        return fail(PaletteError::WrongSize, std::format("{} bytes, not the {} of {} colors", bytes.size(),
                                                         kPaletteBytes, kPaletteColors));
    }
    Palette palette;
    for (std::size_t entry = 0; entry < kPaletteColors; ++entry) {
        const std::span<const std::byte> rgb = bytes.subspan(entry * kRgbBytes, kRgbBytes);
        palette.colors[entry] = PaletteColor{.red = std::to_integer<std::uint8_t>(rgb[0]),
                                             .green = std::to_integer<std::uint8_t>(rgb[1]),
                                             .blue = std::to_integer<std::uint8_t>(rgb[2])};
    }
    return palette;
}

} // namespace nx::assets
