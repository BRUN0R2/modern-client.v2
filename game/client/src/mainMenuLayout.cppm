// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Where the main menu's parts go on a screen (docs/12-ui-hud-and-console.md, section 4): what its mouse
/// reaches and what draws it read one layout, the menus' design (cs.client:menuDesign) on the screen, the
/// game's name at its top left and the items down its left, the last a fixed way over its foot, where the
/// original lists its menu's; and beside them the page an item opens, where the original opens a window.
export module cs.client:mainMenuLayout;

import std;
import nx.core;
import nx.rhi;
import nx.ui;
import :menuDesign;

export namespace cs {

/// How many tabs a page of the menu has across it: the options'.
inline constexpr std::size_t kPageTabs = 5;

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

    /// The page an item opens, beside the items: as tall as from the game's name to the last item's foot,
    /// no wider than the screen leaves it.
    [[nodiscard]] inline auto page() const noexcept -> const nx::ui::PixelRect& { return pageRect; }
    /// Where the page's name is written: its line's top left.
    [[nodiscard]] auto pageTitle() const noexcept -> nx::ui::PixelPoint;
    /// How many rows of a list the page shows at once, one at least.
    [[nodiscard]] auto listRows() const noexcept -> std::size_t;
    /// The row at `row` of those the page's list shows, from the top.
    [[nodiscard]] auto listRow(std::size_t row) const noexcept -> nx::ui::PixelRect;
    /// The row of the list the point at `x`, `y` is over (as itemAt); none when it is over none.
    [[nodiscard]] auto listRowAt(float x, float y) const noexcept -> std::optional<std::size_t>;
    /// What shows how far the list is scrolled, down its right: the part of its track that stands for the
    /// rows shown, from `first`, of `count`; none when the list shows them all.
    [[nodiscard]] auto listThumb(std::size_t first, std::size_t count) const noexcept
        -> std::optional<nx::ui::PixelRect>;
    /// The tab at `index` of `kPageTabs`, across the page between its name and its list.
    [[nodiscard]] auto optionTab(std::size_t index) const noexcept -> nx::ui::PixelRect;
    /// What changes the setting on the row at `row` of the list: the right of the row.
    [[nodiscard]] auto optionControl(std::size_t row) const noexcept -> nx::ui::PixelRect;
    /// The track of a slider on the row at `row`: the left of what changes its setting, its value written
    /// after it.
    [[nodiscard]] auto optionTrack(std::size_t row) const noexcept -> nx::ui::PixelRect;
    /// Where the page tells about what is selected: a line as wide as its list, between it and its buttons.
    [[nodiscard]] auto pageNote() const noexcept -> nx::ui::PixelRect;
    /// What does what the page is for, at its foot on the right.
    [[nodiscard]] auto confirmButton() const noexcept -> nx::ui::PixelRect;
    /// What goes back to the items, before it.
    [[nodiscard]] auto backButton() const noexcept -> nx::ui::PixelRect;

private:
    MenuScale scale;
    std::size_t itemCount = 0;
    nx::ui::PixelRect screenRect{};
    nx::ui::PixelPoint titlePoint{};
    nx::ui::PixelRect firstItem{};
    nx::ui::PixelPoint hintsPoint{};
    nx::ui::PixelRect pageRect{};
};

} // namespace cs
