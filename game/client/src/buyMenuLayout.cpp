// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.rhi;

namespace cs {

namespace {

// The design, in its units, the HUD's foot left free under the menu.
constexpr std::int32_t kMarginUnits = 64;
constexpr std::int32_t kMostContentUnits = 1792;
constexpr std::int32_t kTopUnits = 44;
constexpr std::int32_t kHeaderTallUnits = 96;
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
constexpr std::int32_t kTileWideUnits = 330;

} // namespace

BuyMenuLayout::BuyMenuLayout(nx::rhi::Extent2d size) noexcept
    : scale{static_cast<float>(size.height) / kBuyMenuDesignHeight} {
    const auto screenWide = nx::narrow<std::int32_t>(size.width);
    const std::int32_t margin = pixels(kMarginUnits);
    const std::int32_t wide = std::min(screenWide - (2 * margin), pixels(kMostContentUnits));
    const std::int32_t left = (screenWide - wide) / 2;
    headerRect = {.x = left, .y = pixels(kTopUnits), .wide = wide, .tall = pixels(kHeaderTallUnits)};
    const std::int32_t mainY = headerRect.y + headerRect.tall + pixels(kUnderHeaderUnits);
    const std::int32_t mainTall = pixels(kMainTallUnits);
    const std::int32_t gap = pixels(kColumnGapUnits);
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

auto BuyMenuLayout::pixels(std::int32_t units) const noexcept -> std::int32_t {
    return units > 0 ? std::max(nx::narrow<std::int32_t>(std::lround(static_cast<float>(units) * scale)), 1)
                     : 0;
}

auto BuyMenuLayout::tab(std::size_t index) const noexcept -> BuyRect {
    const std::int32_t tall = pixels(kTabTallUnits);
    return {.x = railRect.x,
            .y = railRect.y + (nx::narrow<std::int32_t>(index) * (tall + pixels(kRowGapUnits))),
            .wide = railRect.wide,
            .tall = tall};
}

auto BuyMenuLayout::ammoRow(std::size_t index, std::size_t tabs) const noexcept -> BuyRect {
    const std::int32_t pitch = pixels(kTabTallUnits) + pixels(kRowGapUnits);
    const std::int32_t tall = pixels(kAmmoTallUnits);
    const std::int32_t firstY =
        railRect.y + (nx::narrow<std::int32_t>(tabs) * pitch) - pixels(kRowGapUnits) + pixels(kAmmoGapUnits);
    return {.x = railRect.x,
            .y = firstY + (nx::narrow<std::int32_t>(index) * (tall + pixels(kRowGapUnits))),
            .wide = railRect.wide,
            .tall = tall};
}

auto BuyMenuLayout::cell(std::size_t index, std::size_t count) const noexcept -> BuyRect {
    const auto columns = std::max(
        kLeastColumns,
        static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<float>(std::max<std::size_t>(count, 1))))));
    const std::size_t rows = std::max<std::size_t>((count + columns - 1) / columns, 1);
    const std::int32_t gap = pixels(kCellGapUnits);
    const auto columnCount = nx::narrow<std::int32_t>(columns);
    const auto rowCount = nx::narrow<std::int32_t>(rows);
    const std::int32_t wide = (gridRect.wide - ((columnCount - 1) * gap)) / columnCount;
    const std::int32_t tall =
        std::min(pixels(kMostCellTallUnits), (gridRect.tall - ((rowCount - 1) * gap)) / rowCount);
    return {.x = gridRect.x + (nx::narrow<std::int32_t>(index % columns) * (wide + gap)),
            .y = gridRect.y + (nx::narrow<std::int32_t>(index / columns) * (tall + gap)),
            .wide = wide,
            .tall = tall};
}

auto BuyMenuLayout::buyButton() const noexcept -> BuyRect {
    const std::int32_t inset = pixels(kDetailInsetUnits);
    const std::int32_t tall = pixels(kButtonTallUnits);
    return {.x = detailRect.x + inset,
            .y = detailRect.y + detailRect.tall - pixels(kDetailFootUnits) - tall,
            .wide = detailRect.wide - (2 * inset),
            .tall = tall};
}

auto BuyMenuLayout::carriedTile(std::size_t index) const noexcept -> BuyRect {
    const std::int32_t wide = pixels(kTileWideUnits);
    return {.x = carriedRect.x + (nx::narrow<std::int32_t>(index) * (wide + pixels(kTileGapUnits))),
            .y = carriedRect.y,
            .wide = wide,
            .tall = carriedRect.tall};
}

} // namespace cs
