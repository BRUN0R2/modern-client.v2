// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Where the main menu's parts go on a screen (docs/12-ui-hud-and-console.md, section 4): what its mouse
/// reaches and what draws it read one layout, the menus' design (cs.client:menuDesign) on the screen, the
/// game's name at its top left and the items down its left, the last a fixed way over its foot, where the
/// original lists its menu's.
export module cs.client:mainMenuLayout;

import std;
import nx.core;
import nx.rhi;
import nx.ui;
import :menuDesign;

export namespace cs {

/// The main menu's parts on a screen.
class MainMenuLayout {
public:
    /// The menu of `items` items on a screen of `size`.
    MainMenuLayout(nx::rhi::Extent2d size, std::size_t items) noexcept;

    /// `units` of the design's on this screen (MenuScale::pixels).
    [[nodiscard]] inline auto pixels(std::int32_t units) const noexcept -> nx::Pixels {
        return scale.pixels(units);
    }

    /// The whole screen, which the menu covers.
    [[nodiscard]] inline auto screen() const noexcept -> const nx::ui::PixelRect& { return screenRect; }
    /// Where the game's name is written: its line's top left.
    [[nodiscard]] inline auto title() const noexcept -> const nx::ui::PixelPoint& { return titlePoint; }
    /// The item at `index`, from the top.
    [[nodiscard]] auto item(std::size_t index) const noexcept -> nx::ui::PixelRect;
    /// The item the point at `x`, `y` is over, in fractions of a pixel from the screen's top left (where the
    /// mouse is); none when it is over none.
    [[nodiscard]] auto itemAt(float x, float y) const noexcept -> std::optional<std::size_t>;
    /// Where the keys are told: their line's top left.
    [[nodiscard]] inline auto hints() const noexcept -> const nx::ui::PixelPoint& { return hintsPoint; }

private:
    MenuScale scale;
    std::size_t itemCount = 0;
    nx::ui::PixelRect screenRect{};
    nx::ui::PixelPoint titlePoint{};
    nx::ui::PixelRect firstItem{};
    nx::ui::PixelPoint hintsPoint{};
};

} // namespace cs
