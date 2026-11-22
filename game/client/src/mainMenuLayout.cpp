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
constexpr MenuUnits kLeftUnits{84};
constexpr MenuUnits kLogoAboveItems{102};
constexpr MenuUnits kItemWideUnits{320};
constexpr MenuUnits kItemTallUnits{42};
constexpr MenuUnits kItemGapUnits{2};
constexpr MenuUnits kUnderItemsUnits{122}; ///< From the last item's foot to the screen's.
constexpr MenuUnits kHintsFootUnits{74};   ///< From the top of the keys' line to the screen's foot.
// The page beside the items.
constexpr MenuUnits kPageMarginUnits{52};
constexpr MenuUnits kMostPageWideUnits{940};
constexpr MenuUnits kPageInsetUnits{28};
constexpr MenuUnits kPageTitleTopUnits{24};
constexpr MenuUnits kListTopUnits{152}; ///< From the page's top to its list's.
constexpr MenuUnits kListRowTallUnits{44};
constexpr MenuUnits kListFootGapUnits{20}; ///< From the list's foot to the buttons.
constexpr MenuUnits kThumbWideUnits{4};
constexpr MenuUnits kButtonWideUnits{200};
constexpr MenuUnits kButtonTallUnits{56};
constexpr MenuUnits kButtonGapUnits{16};
constexpr MenuUnits kTabsTopUnits{67}; ///< From the page's top to its tabs'.
constexpr MenuUnits kTabTallUnits{38};
constexpr MenuUnits kTabGapUnits{4};
constexpr MenuUnits kPreviewWideUnits{280};
constexpr MenuUnits kPreviewGapUnits{12};
constexpr std::int32_t kPreviewRowWidthDivisor = 3;
constexpr MenuUnits kControlWideUnits{300};
constexpr MenuUnits kFieldMargin{6};
constexpr std::size_t kMostChoiceRows = 6;
constexpr MenuUnits kTrackTallUnits{6};
constexpr MenuUnits kValueWideUnits{76}; ///< What a slider's value is written in, after its track.
/// What the room left is divided by to put a thing halfway into it.
constexpr std::int32_t kMiddleDivisor = 2;
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
    firstItem = {.x = left,
                 .y = tall - pixels(kUnderItemsUnits) - itemsTall,
                 .wide = pixels(kItemWideUnits),
                 .tall = itemTall};
    titlePoint = {.x = left, .y = firstItem.y - pixels(kLogoAboveItems)};
    hintsPoint = {.x = left, .y = tall - pixels(kHintsFootUnits)};
    const nx::Pixels margin = pixels(kPageMarginUnits);
    const nx::Pixels pageWide = std::min(pixels(kMostPageWideUnits), wide - (kBothSides * margin));
    pageRect = {.x = (wide - pageWide) / kMiddleDivisor,
                .y = margin,
                .wide = pageWide,
                .tall = tall - (kBothSides * margin)};
}

auto MainMenuLayout::pageTitle() const noexcept -> nx::ui::PixelPoint {
    return {.x = pageRect.x + pixels(kPageInsetUnits), .y = pageRect.y + pixels(kPageTitleTopUnits)};
}

auto MainMenuLayout::listRows() const noexcept -> std::size_t {
    const nx::Pixels room =
        confirmButton().y - pixels(kListFootGapUnits) - (pageRect.y + pixels(kListTopUnits));
    return nx::narrow<std::size_t>(std::max(room / pixels(kListRowTallUnits), 1));
}

auto MainMenuLayout::withCrosshairPreview() const noexcept -> MainMenuLayout {
    MainMenuLayout layout = *this;
    layout.previewSpace = std::min(pixels(kPreviewWideUnits), listRow(0).wide / kPreviewRowWidthDivisor);
    return layout;
}

auto MainMenuLayout::crosshairPreview() const noexcept -> nx::ui::PixelRect {
    const nx::ui::PixelRect first = listRow(0);
    const nx::ui::PixelRect last = listRow(listRows() - 1);
    const nx::Pixels gap = pixels(kPreviewGapUnits);
    return {.x = first.x + first.wide + gap,
            .y = first.y,
            .wide = previewSpace - gap,
            .tall = last.y + last.tall - first.y};
}

auto MainMenuLayout::listRow(std::size_t row) const noexcept -> nx::ui::PixelRect {
    const nx::Pixels inset = pixels(kPageInsetUnits);
    const nx::Pixels rowTall = pixels(kListRowTallUnits);
    return {.x = pageRect.x + inset,
            .y = pageRect.y + pixels(kListTopUnits) + (nx::narrow<std::int32_t>(row) * rowTall),
            .wide = pageRect.wide - (kBothSides * inset) - previewSpace,
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

auto MainMenuLayout::optionTab(std::size_t index) const noexcept -> nx::ui::PixelRect {
    const nx::Pixels inset = pixels(kPageInsetUnits);
    const nx::Pixels gap = pixels(kTabGapUnits);
    const auto tabs = nx::narrow<std::int32_t>(kPageTabs);
    const nx::Pixels tabWide = (pageRect.wide - (kBothSides * inset) - ((tabs - 1) * gap)) / tabs;
    return {.x = pageRect.x + inset + (nx::narrow<std::int32_t>(index) * (tabWide + gap)),
            .y = pageRect.y + pixels(kTabsTopUnits),
            .wide = tabWide,
            .tall = pixels(kTabTallUnits)};
}

auto MainMenuLayout::optionControl(std::size_t row) const noexcept -> nx::ui::PixelRect {
    nx::ui::PixelRect control = listRow(row);
    const nx::Pixels controlWide = std::min(pixels(kControlWideUnits), control.wide / kMiddleDivisor);
    control.x += control.wide - controlWide - pixels(kPageInsetUnits) / kMiddleDivisor;
    control.wide = controlWide;
    return control;
}

auto MainMenuLayout::optionField(std::size_t row) const noexcept -> nx::ui::PixelRect {
    nx::ui::PixelRect field = optionControl(row);
    const nx::Pixels margin = pixels(kFieldMargin);
    field.y += margin;
    field.tall -= kBothSides * margin;
    return field;
}

auto MainMenuLayout::choiceMenu(std::size_t row, std::size_t count) const noexcept -> ChoiceMenuLayout {
    const nx::ui::PixelRect field = optionField(row);
    const nx::Pixels rowTall = listRow(row).tall;
    const nx::Pixels bottom = field.y + field.tall;
    const auto below = nx::narrow<std::size_t>(std::max((screenRect.tall - bottom) / rowTall, 0));
    const auto above = nx::narrow<std::size_t>(std::max(field.y / rowTall, 0));
    const std::size_t wanted = std::min(count, kMostChoiceRows);
    const bool opensDown = below >= wanted || below >= above;
    const std::size_t rows = std::min(wanted, opensDown ? below : above);
    const nx::Pixels tall = nx::narrow<std::int32_t>(rows) * rowTall;
    return {
        .bounds = {.x = field.x, .y = opensDown ? bottom : field.y - tall, .wide = field.wide, .tall = tall},
        .rows = rows};
}

auto ChoiceMenuLayout::item(std::size_t index) const noexcept -> nx::ui::PixelRect {
    nx::ui::PixelRect rect = bounds;
    rect.tall /= nx::narrow<std::int32_t>(rows);
    rect.y += nx::narrow<std::int32_t>(index) * rect.tall;
    return rect;
}

auto MainMenuLayout::optionTrack(std::size_t row) const noexcept -> nx::ui::PixelRect {
    const nx::ui::PixelRect control = optionControl(row);
    const nx::Pixels trackTall = pixels(kTrackTallUnits);
    return {.x = control.x,
            .y = control.y + ((control.tall - trackTall) / kMiddleDivisor),
            .wide = control.wide - pixels(kValueWideUnits),
            .tall = trackTall};
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
