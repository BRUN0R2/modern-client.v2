// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.rhi;
import nx.ui;

namespace cs {

namespace {

// The design, in its units.
constexpr std::int32_t kLeftUnits = 128;
constexpr std::int32_t kTitleTopUnits = 128;
constexpr std::int32_t kItemWideUnits = 440;
constexpr std::int32_t kItemTallUnits = 60;
constexpr std::int32_t kItemGapUnits = 6;
constexpr std::int32_t kUnderItemsUnits = 196; ///< From the last item's foot to the screen's.
constexpr std::int32_t kHintsFootUnits = 96;   ///< From the top of the keys' line to the screen's foot.
// The page beside the items.
constexpr std::int32_t kPageGapUnits = 72; ///< From the items to the page, and from it to the screen's right.
constexpr std::int32_t kMostPageWideUnits = 720;
constexpr std::int32_t kPageInsetUnits = 28;
constexpr std::int32_t kPageTitleTopUnits = 24;
constexpr std::int32_t kListTopUnits = 104; ///< From the page's top to its list's.
constexpr std::int32_t kListRowTallUnits = 44;
constexpr std::int32_t kListFootGapUnits = 20; ///< From the list's foot to the buttons.
constexpr std::int32_t kThumbWideUnits = 4;
constexpr std::int32_t kButtonWideUnits = 200;
constexpr std::int32_t kButtonTallUnits = 56;
constexpr std::int32_t kButtonGapUnits = 16;
/// The sides an inset is left on: left and right.
constexpr std::int32_t kBothSides = 2;

} // namespace

MainMenuLayout::MainMenuLayout(nx::rhi::Extent2d size, std::size_t items) noexcept
    : scale{size.height}, itemCount{items} {
    const nx::Pixels wide{nx::narrow<std::int32_t>(size.width)};
    const nx::Pixels tall{nx::narrow<std::int32_t>(size.height)};
    const nx::Pixels left = pixels(kLeftUnits);
    const auto count = nx::narrow<std::int32_t>(items);
    const nx::Pixels itemTall = pixels(kItemTallUnits);
    const nx::Pixels itemsTall = (count * itemTall) + (std::max(count - 1, 0) * pixels(kItemGapUnits));
    screenRect = {.wide = wide, .tall = tall};
    titlePoint = {.x = left, .y = pixels(kTitleTopUnits)};
    firstItem = {.x = left,
                 .y = tall - pixels(kUnderItemsUnits) - itemsTall,
                 .wide = pixels(kItemWideUnits),
                 .tall = itemTall};
    hintsPoint = {.x = left, .y = tall - pixels(kHintsFootUnits)};
    const nx::Pixels pageX = left + firstItem.wide + pixels(kPageGapUnits);
    pageRect = {.x = pageX,
                .y = titlePoint.y,
                .wide = std::min(pixels(kMostPageWideUnits), wide - pageX - pixels(kPageGapUnits)),
                .tall = tall - pixels(kUnderItemsUnits) - titlePoint.y};
}

auto MainMenuLayout::pageTitle() const noexcept -> nx::ui::PixelPoint {
    return {.x = pageRect.x + pixels(kPageInsetUnits), .y = pageRect.y + pixels(kPageTitleTopUnits)};
}

auto MainMenuLayout::listRows() const noexcept -> std::size_t {
    const nx::Pixels room =
        confirmButton().y - pixels(kListFootGapUnits) - (pageRect.y + pixels(kListTopUnits));
    return nx::narrow<std::size_t>(std::max(room / pixels(kListRowTallUnits), 1));
}

auto MainMenuLayout::listRow(std::size_t row) const noexcept -> nx::ui::PixelRect {
    const nx::Pixels inset = pixels(kPageInsetUnits);
    const nx::Pixels rowTall = pixels(kListRowTallUnits);
    return {.x = pageRect.x + inset,
            .y = pageRect.y + pixels(kListTopUnits) + (nx::narrow<std::int32_t>(row) * rowTall),
            .wide = pageRect.wide - (kBothSides * inset),
            .tall = rowTall};
}

auto MainMenuLayout::listRowAt(float x, float y) const noexcept -> std::optional<std::size_t> {
    for (std::size_t row = 0; row < listRows(); ++row) {
        if (listRow(row).holds(x, y)) {
            return row;
        }
    }
    return std::nullopt;
}

auto MainMenuLayout::listThumb(std::size_t first, std::size_t count) const noexcept
    -> std::optional<nx::ui::PixelRect> {
    const std::size_t rows = listRows();
    if (count <= rows) {
        return std::nullopt;
    }
    const nx::ui::PixelRect top = listRow(0);
    const nx::Pixels track = nx::narrow<std::int32_t>(rows) * top.tall;
    const auto all = nx::narrow<std::int32_t>(count);
    const nx::Pixels wide = pixels(kThumbWideUnits);
    return nx::ui::PixelRect{.x = top.x + top.wide - wide,
                             .y = top.y + (track * nx::narrow<std::int32_t>(first) / all),
                             .wide = wide,
                             .tall = track * nx::narrow<std::int32_t>(rows) / all};
}

auto MainMenuLayout::confirmButton() const noexcept -> nx::ui::PixelRect {
    const nx::Pixels inset = pixels(kPageInsetUnits);
    const nx::Pixels buttonWide = pixels(kButtonWideUnits);
    const nx::Pixels buttonTall = pixels(kButtonTallUnits);
    return {.x = pageRect.x + pageRect.wide - inset - buttonWide,
            .y = pageRect.y + pageRect.tall - inset - buttonTall,
            .wide = buttonWide,
            .tall = buttonTall};
}

auto MainMenuLayout::backButton() const noexcept -> nx::ui::PixelRect {
    nx::ui::PixelRect button = confirmButton();
    button.x -= button.wide + pixels(kButtonGapUnits);
    return button;
}

auto MainMenuLayout::item(std::size_t index) const noexcept -> nx::ui::PixelRect {
    nx::ui::PixelRect rect = firstItem;
    rect.y += nx::narrow<std::int32_t>(index) * (firstItem.tall + pixels(kItemGapUnits));
    return rect;
}

auto MainMenuLayout::itemAt(float x, float y) const noexcept -> std::optional<std::size_t> {
    for (std::size_t index = 0; index < itemCount; ++index) {
        if (item(index).holds(x, y)) {
            return index;
        }
    }
    return std::nullopt;
}

} // namespace cs
