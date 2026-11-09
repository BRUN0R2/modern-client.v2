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
