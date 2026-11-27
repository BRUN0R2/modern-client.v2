// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The picture behind the main menu (docs/12-ui-hud-and-console.md, section 4): the active content's own,
/// the pictures its background layout places (the original's resource/BackgroundLayout.txt and its
/// resource/background/ tiles) put together into one, which the menu stretches over the screen as the
/// original.
export module cs.client:menuBackground;

import std;
import nx.core;
import nx.assets;

export namespace cs {

/// Why the menu's background could not be read.
enum class MenuBackgroundError : std::uint8_t {
    Unreadable, ///< The layout or one of its pictures is not among the game's files, or cannot be read
                ///< (cause).
    Invalid,    ///< The layout or one of its pictures cannot be made sense of (cause).
};

/// The pictures `layout` places, `tiles` in its order, put together on its screen: each from its top left,
/// a later one over an earlier one, what reaches past the screen left out, and what none covers black.
[[nodiscard]] auto composeBackground(const nx::assets::BackgroundLayout& layout,
                                     std::span<const nx::assets::RgbaImage> tiles) -> nx::assets::RgbaImage;

/// The background the layout at `layoutPath` among the files of `vfs` places, its pictures read from `vfs`
/// and put together (composeBackground). Errors: every MenuBackgroundError.
[[nodiscard]] auto readMenuBackground(const nx::assets::Vfs& vfs, std::string_view layoutPath)
    -> nx::Result<nx::assets::RgbaImage, MenuBackgroundError>;

} // namespace cs
