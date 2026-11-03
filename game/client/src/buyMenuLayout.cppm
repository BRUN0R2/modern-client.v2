// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Where the buy menu's parts go on a screen (docs/12-ui-hud-and-console.md, section 4): what its keys and
/// its mouse reach and what draws it read one layout, a design 1080 lines tall made as large as the screen,
/// its content no wider than the design's and in the middle of a wider screen.
export module cs.client:buyMenuLayout;

import std;
import nx.core;
import nx.rhi;

export namespace cs {

/// How many lines tall the screen is that the menu is designed on.
inline constexpr float kBuyMenuDesignHeight = 1080.0F;

/// A rectangle of the screen, in pixels from its top left.
struct BuyRect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t wide = 0;
    std::int32_t tall = 0;

    [[nodiscard]] inline auto operator==(const BuyRect&) const noexcept -> bool = default;

    /// Whether the point at `pointX`, `pointY` is in it.
    [[nodiscard]] constexpr auto holds(float pointX, float pointY) const noexcept -> bool {
        return pointX >= static_cast<float>(x) && pointX < static_cast<float>(x + wide) &&
               pointY >= static_cast<float>(y) && pointY < static_cast<float>(y + tall);
    }
};

/// Whether an item's price takes a row of its own under its name, `nameRows` rows of which `nameWide` is the
/// widest: when its name wrapped, or when `priceWide` more and `gap` between them do not fit in `room`.
[[nodiscard]] constexpr auto isPriceApart(std::size_t nameRows, std::int32_t nameWide, std::int32_t priceWide,
                                          std::int32_t gap, std::int32_t room) noexcept -> bool {
    return nameRows > 1 || nameWide + gap + priceWide > room;
}

/// The buy menu's parts on a screen.
class BuyMenuLayout {
public:
    explicit BuyMenuLayout(nx::rhi::Extent2d size) noexcept;

    /// `units` of the design's on this screen, to the nearest pixel, at least one of some.
    [[nodiscard]] auto pixels(std::int32_t units) const noexcept -> std::int32_t;

    /// What heads the menu: its name, the team, the time left and the money.
    [[nodiscard]] inline auto header() const noexcept -> const BuyRect& { return headerRect; }
    /// The tabs and the ammo under them.
    [[nodiscard]] inline auto rail() const noexcept -> const BuyRect& { return railRect; }
    /// The items of the tab shown.
    [[nodiscard]] inline auto grid() const noexcept -> const BuyRect& { return gridRect; }
    /// What the item selected is.
    [[nodiscard]] inline auto detail() const noexcept -> const BuyRect& { return detailRect; }
    /// What the player carries, and the keys.
    [[nodiscard]] inline auto carried() const noexcept -> const BuyRect& { return carriedRect; }

    /// The tab at `index` on the rail.
    [[nodiscard]] auto tab(std::size_t index) const noexcept -> BuyRect;
    /// The row of ammo at `index` under `tabs` tabs.
    [[nodiscard]] auto ammoRow(std::size_t index, std::size_t tabs) const noexcept -> BuyRect;
    /// The cell of the item at `index` of `count` on the grid: three columns, more when they would make
    /// rows lower than wide, however many a mod sells on one tab.
    [[nodiscard]] auto cell(std::size_t index, std::size_t count) const noexcept -> BuyRect;
    /// What buys the item selected, at the foot of what it is.
    [[nodiscard]] auto buyButton() const noexcept -> BuyRect;
    /// What the player carries at `index`: its primary weapon, its pistol and its armor.
    [[nodiscard]] auto carriedTile(std::size_t index) const noexcept -> BuyRect;
    /// What buys again what the player last had, half as wide again as one of those, after them.
    [[nodiscard]] auto againTile() const noexcept -> BuyRect;

private:
    float scale = 1.0F; ///< Pixels of the screen for each unit of the design.
    BuyRect headerRect{};
    BuyRect railRect{};
    BuyRect gridRect{};
    BuyRect detailRect{};
    BuyRect carriedRect{};
};

} // namespace cs
