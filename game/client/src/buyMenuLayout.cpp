// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.rhi;
import nx.ui;

namespace cs {

namespace {

// The design, in its units, the HUD's foot left free under the menu.
constexpr std::int32_t kMarginUnits = 64;
constexpr std::int32_t kMostContentUnits = 1792;
constexpr std::int32_t kTopUnits = 44;
constexpr std::int32_t kHeaderTallUnits = 96;
constexpr std::int32_t kTimeBarTallUnits = 2;
constexpr std::int32_t kUnderHeaderUnits = 20;
constexpr std::int32_t kMainTallUnits = 680;
constexpr std::int32_t kUnderMainUnits = 18;
constexpr std::int32_t kCarriedTallUnits = 88;
constexpr std::int32_t kRailWideUnits = 288;
constexpr std::int32_t kDetailWideUnits = 384;
constexpr std::int32_t kColumnGapUnits = 16;
constexpr std::int32_t kTabTallUnits = 72;
constexpr std::int32_t kRowGapUnits = 6;
constexpr std::int32_t kAmmoGapUnits =
    13; ///< From the last tab to the first row of ammo, a line in its middle.
constexpr std::int32_t kAmmoTallUnits = 64;
constexpr std::size_t kLeastColumns = 3;
constexpr std::int32_t kCellGapUnits = 12;
constexpr std::int32_t kMostCellTallUnits = 204;
constexpr std::int32_t kDetailInsetUnits = 20;
constexpr std::int32_t kDetailFootUnits = 18;
constexpr std::int32_t kButtonTallUnits = 56;
constexpr std::int32_t kTileGapUnits = 12;
constexpr std::int32_t kKeysWideUnits = 272; ///< What the keys take of the row of what the player carries.
constexpr std::int32_t kCarriedTiles = 3;

} // namespace

auto barFillOf(nx::Pixels wide, float share) noexcept -> BuyBarFill {
    const float reached = static_cast<float>(wide.value()) * std::clamp(share, 0.0F, 1.0F);
    const float whole = std::floor(reached);
    return {.whole = nx::Pixels{static_cast<std::int32_t>(whole)}, .nextPixel = reached - whole};
}

BuyMenuLayout::BuyMenuLayout(nx::rhi::Extent2d size) noexcept : scale{size.height} {
    const nx::Pixels screenWide{nx::narrow<std::int32_t>(size.width)};
    const nx::Pixels margin = pixels(kMarginUnits);
    const nx::Pixels wide = std::min(screenWide - (2 * margin), pixels(kMostContentUnits));
    const nx::Pixels left = (screenWide - wide) / 2;
    headerRect = {.x = left, .y = pixels(kTopUnits), .wide = wide, .tall = pixels(kHeaderTallUnits)};
    const nx::Pixels mainY = headerRect.y + headerRect.tall + pixels(kUnderHeaderUnits);
    const nx::Pixels mainTall = pixels(kMainTallUnits);
    const nx::Pixels gap = pixels(kColumnGapUnits);
    railRect = {.x = left, .y = mainY, .wide = pixels(kRailWideUnits), .tall = mainTall};
    detailRect = {.x = left + wide - pixels(kDetailWideUnits),
                  .y = mainY,
                  .wide = pixels(kDetailWideUnits),
                  .tall = mainTall};
    gridRect = {.x = railRect.x + railRect.wide + gap,
                .y = mainY,
                .wide = detailRect.x - gap - (railRect.x + railRect.wide + gap),
                .tall = mainTall};
    carriedRect = {.x = left,
                   .y = mainY + mainTall + pixels(kUnderMainUnits),
                   .wide = wide,
                   .tall = pixels(kCarriedTallUnits)};
}

auto BuyMenuLayout::timeBar() const noexcept -> nx::ui::PixelRect {
    const nx::Pixels tall = pixels(kTimeBarTallUnits);
    return {
        .x = headerRect.x, .y = headerRect.y + headerRect.tall - tall, .wide = headerRect.wide, .tall = tall};
}

auto BuyMenuLayout::tab(std::size_t index) const noexcept -> nx::ui::PixelRect {
    const nx::Pixels tall = pixels(kTabTallUnits);
    return {.x = railRect.x,
            .y = railRect.y + (nx::narrow<std::int32_t>(index) * (tall + pixels(kRowGapUnits))),
            .wide = railRect.wide,
            .tall = tall};
}

auto BuyMenuLayout::ammoRow(std::size_t index, std::size_t tabs) const noexcept -> nx::ui::PixelRect {
    const nx::Pixels pitch = pixels(kTabTallUnits) + pixels(kRowGapUnits);
    const nx::Pixels tall = pixels(kAmmoTallUnits);
    const nx::Pixels firstY =
        railRect.y + (nx::narrow<std::int32_t>(tabs) * pitch) - pixels(kRowGapUnits) + pixels(kAmmoGapUnits);
    return {.x = railRect.x,
            .y = firstY + (nx::narrow<std::int32_t>(index) * (tall + pixels(kRowGapUnits))),
            .wide = railRect.wide,
            .tall = tall};
}

auto BuyMenuLayout::cell(std::size_t index, std::size_t count) const noexcept -> nx::ui::PixelRect {
    const auto columns = std::max(
        kLeastColumns,
        static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<float>(std::max<std::size_t>(count, 1))))));
    const std::size_t rows = std::max<std::size_t>((count + columns - 1) / columns, 1);
    const nx::Pixels gap = pixels(kCellGapUnits);
    const auto columnCount = nx::narrow<std::int32_t>(columns);
    const auto rowCount = nx::narrow<std::int32_t>(rows);
    const nx::Pixels wide = (gridRect.wide - ((columnCount - 1) * gap)) / columnCount;
    const nx::Pixels tall =
        std::min(pixels(kMostCellTallUnits), (gridRect.tall - ((rowCount - 1) * gap)) / rowCount);
    return {.x = gridRect.x + (nx::narrow<std::int32_t>(index % columns) * (wide + gap)),
            .y = gridRect.y + (nx::narrow<std::int32_t>(index / columns) * (tall + gap)),
            .wide = wide,
            .tall = tall};
}

auto BuyMenuLayout::buyButton() const noexcept -> nx::ui::PixelRect {
    const nx::Pixels inset = pixels(kDetailInsetUnits);
    const nx::Pixels tall = pixels(kButtonTallUnits);
    return {.x = detailRect.x + inset,
            .y = detailRect.y + detailRect.tall - pixels(kDetailFootUnits) - tall,
            .wide = detailRect.wide - (2 * inset),
            .tall = tall};
}

auto BuyMenuLayout::carriedTile(std::size_t index) const noexcept -> nx::ui::PixelRect {
    // Of what the keys and the gaps leave, the tiles take a share each and the last half as much again.
    constexpr std::int32_t kSharesOfATile = 2;
    constexpr std::int32_t kAllShares = (2 * kCarriedTiles) + 3;
    const nx::Pixels room =
        carriedRect.wide - pixels(kKeysWideUnits) - ((kCarriedTiles + 1) * pixels(kTileGapUnits));
    const nx::Pixels wide = room * kSharesOfATile / kAllShares;
    return {.x = carriedRect.x + (nx::narrow<std::int32_t>(index) * (wide + pixels(kTileGapUnits))),
            .y = carriedRect.y,
            .wide = wide,
            .tall = carriedRect.tall};
}

auto BuyMenuLayout::againTile() const noexcept -> nx::ui::PixelRect {
    const nx::ui::PixelRect after = carriedTile(kCarriedTiles);
    return {.x = after.x, .y = after.y, .wide = after.wide + (after.wide / 2), .tall = after.tall};
}

} // namespace cs
