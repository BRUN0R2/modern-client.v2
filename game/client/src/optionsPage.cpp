// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.platform;
import nx.ui;
import cs.shared;

namespace cs {

namespace {

/// The value of `slider` nearest `value` that is a whole number of its steps from its least, within its
/// range.
[[nodiscard]] auto steppedValueOf(const OptionSlider& slider, float value) noexcept -> float {
    const float steps = std::round((value - slider.minimum) / slider.step);
    return std::clamp(slider.minimum + (steps * slider.step), slider.minimum, slider.maximum);
}

/// The share of its track the value of `slider` is at, from 0 to 1.
[[nodiscard]] auto shareOf(const OptionSlider& slider, float value) noexcept -> float {
    return std::clamp((value - slider.minimum) / (slider.maximum - slider.minimum), 0.0F, 1.0F);
}

[[nodiscard]] auto shownOf(const OptionRow& row) -> ShownOption {
    if (const auto* const slider = std::get_if<OptionSlider>(&row.control)) {
        const float value = slider->value();
        return {.name = row.name,
                .control =
                    ShownSlider{.value = value, .style = slider->style, .share = shareOf(*slider, value)}};
    }
    return {.name = row.name, .control = ShownSwitch{.isOn = std::get<OptionSwitch>(row.control).isOn()}};
}

} // namespace

auto sliderText(const ShownSlider& slider) -> std::string {
    constexpr float kPercentsOfOne = 100.0F;
    switch (slider.style) {
    case OptionValueStyle::Whole:
        return std::format("{}", std::lround(slider.value));
    case OptionValueStyle::Percent:
        return std::format("{}%", std::lround(slider.value * kPercentsOfOne));
    case OptionValueStyle::Tenths:
        break;
    }
    return std::format("{:.1f}", slider.value);
}

OptionsPage::OptionsPage(std::vector<OptionRow> listed) noexcept : rows{std::move(listed)} {}

void OptionsPage::open() noexcept {
    category = OptionCategory::Mouse;
    selectedRow.reset();
    draggedRow.reset();
}

auto OptionsPage::shownRows() const -> std::vector<const OptionRow*> {
    return rows | std::views::filter([this](const OptionRow& row) { return row.category == category; }) |
           std::views::transform([](const OptionRow& row) { return &row; }) | std::ranges::to<std::vector>();
}

auto OptionsPage::frame() const -> OptionsFrame {
    OptionsFrame shown{.category = category, .selectedRow = selectedRow};
    for (const OptionRow* const row : shownRows()) {
        shown.rows.push_back(shownOf(*row));
    }
    return shown;
}

void OptionsPage::change(std::int32_t steps) {
    const std::vector<const OptionRow*> shown = shownRows();
    if (!selectedRow || *selectedRow >= shown.size()) {
        return;
    }
    const OptionRow& row = *shown[*selectedRow];
    if (const auto* const slider = std::get_if<OptionSlider>(&row.control)) {
        slider->set(steppedValueOf(*slider, slider->value() + (static_cast<float>(steps) * slider->step)));
    } else {
        const OptionSwitch& toggled = std::get<OptionSwitch>(row.control);
        toggled.set(!toggled.isOn());
    }
}

void OptionsPage::drag(const OptionSlider& slider, const nx::ui::PixelRect& track, float x) {
    const float share = std::clamp((x - static_cast<float>(track.x.value())) /
                                       static_cast<float>(std::max(track.wide.value(), 1)),
                                   0.0F, 1.0F);
    slider.set(steppedValueOf(slider, slider.minimum + (share * (slider.maximum - slider.minimum))));
}

auto OptionsPage::handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition cursor,
                         const MainMenuLayout& layout) -> OptionsOutcome {
    const std::vector<const OptionRow*> shown = shownRows();
    if (std::holds_alternative<nx::platform::MouseMoved>(event)) {
        if (draggedRow && *draggedRow < shown.size()) {
            drag(std::get<OptionSlider>(shown[*draggedRow]->control), layout.optionTrack(*draggedRow),
                 cursor.x);
        } else if (const std::optional<std::size_t> row = layout.listRowAt(cursor.x, cursor.y);
                   row && *row < shown.size()) {
            selectedRow = row;
        }
        return OptionsOutcome::Stays;
    }
    if (const auto* const button = std::get_if<nx::platform::MouseButtonChanged>(&event)) {
        if (button->button != nx::platform::MouseButton::Left) {
            return OptionsOutcome::Stays;
        }
        if (!button->isDown) {
            draggedRow.reset();
            return OptionsOutcome::Stays;
        }
        return click(cursor, layout);
    }
    const auto* const key = std::get_if<nx::platform::KeyChanged>(&event);
    return key != nullptr && key->isDown ? press(*key) : OptionsOutcome::Stays;
}

auto OptionsPage::click(nx::platform::MousePosition cursor, const MainMenuLayout& layout) -> OptionsOutcome {
    if (layout.backButton().holds(cursor.x, cursor.y)) {
        return OptionsOutcome::GoesBack;
    }
    for (const OptionCategory tab : nx::EnumArray<OptionCategory, bool>::keys()) {
        if (layout.optionTab(static_cast<std::size_t>(std::to_underlying(tab))).holds(cursor.x, cursor.y)) {
            category = tab;
            selectedRow.reset();
            return OptionsOutcome::Stays;
        }
    }
    const std::vector<const OptionRow*> shown = shownRows();
    const std::optional<std::size_t> row = layout.listRowAt(cursor.x, cursor.y);
    if (!row || *row >= shown.size()) {
        return OptionsOutcome::Stays;
    }
    selectedRow = row;
    if (const auto* const slider = std::get_if<OptionSlider>(&shown[*row]->control)) {
        const nx::ui::PixelRect track = layout.optionTrack(*row);
        if (cursor.x >= static_cast<float>(track.x.value())) {
            drag(*slider, track, cursor.x);
            draggedRow = row;
        }
    } else {
        change(1);
    }
    return OptionsOutcome::Stays;
}

auto OptionsPage::press(const nx::platform::KeyChanged& key) -> OptionsOutcome {
    using nx::platform::Key;
    const std::size_t count = shownRows().size();
    constexpr auto kTabs = nx::EnumArray<OptionCategory, bool>::size();
    const auto tab = static_cast<std::size_t>(std::to_underlying(category));
    switch (key.key) {
    case Key::Up:
        // The tabs are above the first row.
        selectedRow = selectedRow && *selectedRow > 0 ? std::optional{*selectedRow - 1} : std::nullopt;
        break;
    case Key::Down:
        if (count > 0) {
            selectedRow = selectedRow ? std::min(*selectedRow + 1, count - 1) : 0;
        }
        break;
    case Key::Left:
        if (selectedRow) {
            change(-1);
        } else {
            category = static_cast<OptionCategory>(tab - std::min<std::size_t>(tab, 1));
        }
        break;
    case Key::Right:
        if (selectedRow) {
            change(1);
        } else {
            category = static_cast<OptionCategory>(std::min(tab + 1, kTabs - 1));
        }
        break;
    case Key::Enter:
    case Key::KeypadEnter:
        if (!key.isRepeat && selectedRow && *selectedRow < count &&
            std::holds_alternative<OptionSwitch>(shownRows()[*selectedRow]->control)) {
            change(1);
        }
        break;
    case Key::Escape:
        return key.isRepeat ? OptionsOutcome::Stays : OptionsOutcome::GoesBack;
    default:
        break;
    }
    return OptionsOutcome::Stays;
}

} // namespace cs
