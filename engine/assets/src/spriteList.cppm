// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Sprite lists (docs/12-ui-hud-and-console.md, section 3): sprites/hud.txt and the weapons'
/// sprites/weapon_<name>.txt, which name the rectangles of the HUD's sprites it draws, for each
/// width of screen.
export module nx.assets:spriteList;

import std;
import nx.core;

export namespace nx::assets {

/// What can be wrong with a sprite list; the detail has the entry and the word.
enum class SpriteListError : std::uint8_t {
    NoCount,  ///< The list does not start with the number of its entries.
    BadEntry, ///< An entry is cut short, or a number of it is not a whole number of pixels.
};

/// A rectangle of a sprite's first frame, in pixels from its top left.
struct SpriteRect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] inline auto operator==(const SpriteRect&) const noexcept -> bool = default;
};

/// An entry of a sprite list: a name the HUD asks for, the screens it is for, and the rectangle of
/// the sprite it shows.
struct SpriteListEntry {
    std::string name{};           ///< "number_0", "crosshair".
    std::uint32_t resolution = 0; ///< The screens it is for: 320 or 640, the widths they start at.
    std::string sprite{};         ///< Its sprite: sprites/<sprite>.spr.
    SpriteRect rect{};
};

/// The entries of the sprite list `text`, as the original reads it: their count, then the seven
/// words of each (its name, resolution and sprite, and the x, y, width and height of its
/// rectangle), whitespace between them, a `"` around a word that holds some, and `//` comments to
/// the end of a line. A count above the entries the list has reads those it has, as
/// Counter-Strike's hud.txt needs (it counts 215 for 190); one below reads that many.
/// Errors: NoCount, BadEntry.
[[nodiscard]] auto parseSpriteList(std::string_view text)
    -> Result<std::vector<SpriteListEntry>, SpriteListError>;

} // namespace nx::assets
