// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Where the buy menu's parts go on a screen (docs/12-ui-hud-and-console.md, section 4): what its keys and
/// its mouse reach and what draws it read one layout, the menus' design (cs.client:menuDesign) on the screen,
/// its content no wider than the design's and in the middle of a wider screen.
export module cs.client:buyMenuLayout;

import std;
import nx.core;
import nx.rhi;
import nx.ui;
import :menuDesign;

export namespace cs {

/// Whether an item's price takes a row of its own under its name, `nameRows` rows of which `nameWide` is the
/// widest: when its name wrapped, or when `priceWide` more and `gap` between them do not fit in `room`.
[[nodiscard]] constexpr auto isPriceApart(std::size_t nameRows, nx::Pixels nameWide, nx::Pixels priceWide,
                                          nx::Pixels gap, nx::Pixels room) noexcept -> bool {
    return nameRows > 1 || nameWide + gap + priceWide > room;
}

/// How far a bar shows a share of itself: the pixels it covers whole, and how much of the next one, from 0 to
/// 1, so that it moves by less than a pixel at a time.
struct BuyBarFill {
    nx::Pixels whole{};
    float nextPixel = 0.0F;

    [[nodiscard]] inline auto operator==(const BuyBarFill&) const noexcept -> bool = default;
};

/// How far a bar `wide` long shows `share` of itself, from 0 to 1.
[[nodiscard]] auto barFillOf(nx::Pixels wide, float share) noexcept -> BuyBarFill;

/// The buy menu's parts on a screen.
class BuyMenuLayout {
public:
    explicit BuyMenuLayout(nx::rhi::Extent2d size) noexcept;

    /// `units` of the design's on this screen (MenuScale::pixels).
    [[nodiscard]] inline auto pixels(std::int32_t units) const noexcept -> nx::Pixels {
        return scale.pixels(units);
    }

    /// What heads the menu: its name, the team, the time left and the money.
    [[nodiscard]] inline auto header() const noexcept -> const nx::ui::PixelRect& { return headerRect; }
    /// The line that runs down with the time to buy, at the foot of what heads the menu.
    [[nodiscard]] auto timeBar() const noexcept -> nx::ui::PixelRect;
    /// The tabs and the ammo under them.
    [[nodiscard]] inline auto rail() const noexcept -> const nx::ui::PixelRect& { return railRect; }
    /// The items of the tab shown.
    [[nodiscard]] inline auto grid() const noexcept -> const nx::ui::PixelRect& { return gridRect; }
    /// What the item selected is.
    [[nodiscard]] inline auto detail() const noexcept -> const nx::ui::PixelRect& { return detailRect; }
    /// What the player carries, and the keys.
    [[nodiscard]] inline auto carried() const noexcept -> const nx::ui::PixelRect& { return carriedRect; }

    /// The tab at `index` on the rail.
    [[nodiscard]] auto tab(std::size_t index) const noexcept -> nx::ui::PixelRect;
    /// The row of ammo at `index` under `tabs` tabs.
    [[nodiscard]] auto ammoRow(std::size_t index, std::size_t tabs) const noexcept -> nx::ui::PixelRect;
    /// The cell of the item at `index` of `count` on the grid: three columns, more when they would make
    /// rows lower than wide, however many a mod sells on one tab.
    [[nodiscard]] auto cell(std::size_t index, std::size_t count) const noexcept -> nx::ui::PixelRect;
    /// What buys the item selected, at the foot of what it is.
    [[nodiscard]] auto buyButton() const noexcept -> nx::ui::PixelRect;
    /// What the player carries at `index`: its primary weapon, its pistol and its armor.
    [[nodiscard]] auto carriedTile(std::size_t index) const noexcept -> nx::ui::PixelRect;
    /// What buys again what the player last had, half as wide again as one of those, after them.
    [[nodiscard]] auto againTile() const noexcept -> nx::ui::PixelRect;

private:
    MenuScale scale;
    nx::ui::PixelRect headerRect{};
    nx::ui::PixelRect railRect{};
    nx::ui::PixelRect gridRect{};
    nx::ui::PixelRect detailRect{};
    nx::ui::PixelRect carriedRect{};
};

} // namespace cs
