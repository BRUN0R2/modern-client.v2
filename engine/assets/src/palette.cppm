// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The engine's palette (docs/06-assets-and-resources.md, section 3.1): gfx/palette.lmp, the colors the
/// engine gives what the game names by a palette entry, as the blood of a hit (TE_BLOODSPRITE's color).
export module nx.assets:palette;

import std;
import nx.core;
import :image;

export namespace nx::assets {

/// Where the engine's palette is, in the base game's folder.
inline constexpr std::string_view kPalettePath = "gfx/palette.lmp";

/// Why a palette could not be read.
enum class PaletteError : std::uint8_t {
    WrongSize, ///< Not kPaletteBytes long.
};

/// An entry of a palette: its red, green and blue.
struct PaletteColor {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    [[nodiscard]] constexpr auto operator==(const PaletteColor&) const noexcept -> bool = default;
};

/// A palette of kPaletteColors colors, by their entry.
class Palette {
public:
    /// The palette of `bytes`, a palette.lmp: kPaletteBytes of red, green and blue. Errors: WrongSize.
    [[nodiscard]] static auto read(std::span<const std::byte> bytes) -> Result<Palette, PaletteError>;

    /// The color of `entry`.
    [[nodiscard]] inline auto color(std::uint8_t entry) const noexcept -> PaletteColor {
        return colors[entry];
    }

private:
    std::array<PaletteColor, kPaletteColors> colors{};
};

} // namespace nx::assets
