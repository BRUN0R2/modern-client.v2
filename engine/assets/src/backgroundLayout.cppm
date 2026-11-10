// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Background layouts (docs/12-ui-hud-and-console.md, section 4): resource/BackgroundLayout.txt and
/// resource/BackgroundLoadingLayout.txt, which place the pictures the original's menu and its loading
/// screen are made of, on a screen of the layout's own size.
export module nx.assets:backgroundLayout;

import std;
import nx.core;

export namespace nx::assets {

/// What can be wrong with a background layout; the detail has the words.
enum class BackgroundLayoutError : std::uint8_t {
    NoResolution, ///< The layout does not start with `resolution` and a width and a height of its screen.
    BadTile,      ///< A picture is cut short, or how it is sized or where it goes cannot be read.
    TooLarge,     ///< Its screen is of no size or past kMaxBackgroundSide, or a picture starts outside it.
};

/// The most pixels along a side of a background layout's screen: far past the original's 800 by 600
/// and its 1600 by 1200, and what bounds the memory a layout can ask for.
inline constexpr std::uint32_t kMaxBackgroundSide = 4096;

/// A picture of a background: its file among the game's (a TGA), and where its top left goes on the
/// layout's screen.
struct BackgroundTile {
    std::string image{};
    std::uint32_t x = 0;
    std::uint32_t y = 0;

    [[nodiscard]] inline auto operator==(const BackgroundTile&) const noexcept -> bool = default;
};

/// A background: the size of the screen it is laid out on, and its pictures in the layout's order, a
/// later one over an earlier one.
struct BackgroundLayout {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<BackgroundTile> tiles{};
};

/// The background layout `text`, as the original writes it: `resolution`, the width and the height of
/// its screen, then the four words of each picture: its file, how it is sized (`scaled` or `fit`), and
/// the x and y of its top left; whitespace between the words, a `"` around one that holds some, and
/// `//` comments to the end of a line. How a picture is sized is checked and not kept: both words
/// place it the same on the layout's own screen, which is where the pictures are put together.
/// Errors: every BackgroundLayoutError.
[[nodiscard]] auto parseBackgroundLayout(std::string_view text)
    -> Result<BackgroundLayout, BackgroundLayoutError>;

} // namespace nx::assets
