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

static_assert(nx::EnumArray<OptionCategory, bool>::size() == kPageTabs,
              "a tab of the page for each category");

/// How many rows of the list a notch of the wheel scrolls.
constexpr float kRowsANotch = 3.0F;

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

/// The place of the value of `choice` `steps` from the one chosen, after it above zero and before it below,
/// going around past either end; while none is chosen, from before the first going on or after the last
/// going back.
[[nodiscard]] auto steppedChoiceOf(const OptionChoice& choice, std::int32_t steps) -> std::size_t {
    nx::expects(!choice.values.empty(), "OptionChoice: a value at least");
    const auto count = nx::narrow<std::int64_t>(choice.values.size());
    const std::optional<std::size_t> chosen = choice.chosen();
    // None chosen: as from just before the first going on, or just after the last going back.
    const std::int64_t noneChosen = steps > 0 ? -1 : count;
    const std::int64_t from = chosen ? nx::narrow<std::int64_t>(*chosen) : noneChosen;
    return nx::narrow<std::size_t>((((from + steps) % count) + count) % count);
}

/// How `row` shows, waiting for its key or not.
[[nodiscard]] auto shownOf(const OptionRow& row, bool isWaited) -> ShownOption {
    if (const auto* const slider = std::get_if<OptionSlider>(&row.control)) {
        const float value = slider->value();
        return {.name = row.name,
                .control =
                    ShownSlider{.value = value, .style = slider->style, .share = shareOf(*slider, value)}};
    }
    if (const auto* const keys = std::get_if<OptionKeys>(&row.control)) {
        return {.name = row.name, .control = ShownKeys{.keys = keys->keys(), .isWaited = isWaited}};
    }
    if (const auto* const choice = std::get_if<OptionChoice>(&row.control)) {
        const std::optional<std::size_t> chosen = choice->chosen();
        nx::expects(!chosen || *chosen < choice->values.size(), "OptionChoice: one of its values chosen");
        return {.name = row.name,
                .control = ShownChoice{.chosen = chosen.transform(
                                           [choice](std::size_t index) { return choice->values[index]; })}};
    }
    return {.name = row.name, .control = ShownSwitch{.isOn = std::get<OptionSwitch>(row.control).isOn()}};
}

} // namespace

ShownOption::~ShownOption() = default;

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
    show(OptionCategory::Mouse);
}

void OptionsPage::show(OptionCategory tab) noexcept {
    category = tab;
    selected.reset();
    firstShown = 0;
    draggedRow.reset();
    waitingRow.reset();
}

auto OptionsPage::shownRows() const -> std::vector<const OptionRow*> {
    return rows | std::views::filter([this](const OptionRow& row) { return row.category == category; }) |
           std::views::transform([](const OptionRow& row) { return &row; }) | std::ranges::to<std::vector>();
}

auto OptionsPage::firstShownOf(std::size_t listRows) const -> std::size_t {
    const std::size_t count = shownRows().size();
    return std::min(firstShown, count - std::min(count, listRows));
}

auto OptionsPage::frame(std::size_t listRows) const -> OptionsFrame {
    const std::vector<const OptionRow*> all = shownRows();
    const std::size_t first = firstShownOf(listRows);
    OptionsFrame shown{.category = category, .first = first, .count = all.size()};
    for (std::size_t index = first; index < std::min(all.size(), first + listRows); ++index) {
        shown.rows.push_back(shownOf(*all[index], waitingRow == index));
    }
    if (selected && *selected >= first && *selected < first + shown.rows.size()) {
        shown.selectedRow = *selected - first;
    }
    return shown;
}

void OptionsPage::select(std::size_t index, std::size_t listRows) noexcept {
    selected = index;
    if (index < firstShown) {
        firstShown = index;
    } else if (index >= firstShown + listRows) {
        firstShown = index + 1 - listRows;
    }
}

void OptionsPage::change(std::int32_t steps) {
    const std::vector<const OptionRow*> all = shownRows();
    if (!selected || *selected >= all.size()) {
        return;
    }
    const OptionRow& row = *all[*selected];
    if (const auto* const slider = std::get_if<OptionSlider>(&row.control)) {
        slider->set(steppedValueOf(*slider, slider->value() + (static_cast<float>(steps) * slider->step)));
    } else if (const auto* const toggled = std::get_if<OptionSwitch>(&row.control)) {
        toggled->set(!toggled->isOn());
    } else if (const auto* const choice = std::get_if<OptionChoice>(&row.control)) {
        choice->choose(steppedChoiceOf(*choice, steps));
    }
}

void OptionsPage::activate() {
    const std::vector<const OptionRow*> all = shownRows();
    if (!selected || *selected >= all.size()) {
        return;
    }
    const OptionControl& control = all[*selected]->control;
    if (std::holds_alternative<OptionKeys>(control)) {
        waitingRow = selected;
    } else if (std::holds_alternative<OptionSwitch>(control) ||
               std::holds_alternative<OptionChoice>(control)) {
        change(1);
    }
}

void OptionsPage::drag(const OptionSlider& slider, const nx::ui::PixelRect& track, float x) {
    const float share = std::clamp((x - static_cast<float>(track.x.value())) /
                                       static_cast<float>(std::max(track.wide.value(), 1)),
                                   0.0F, 1.0F);
    slider.set(steppedValueOf(slider, slider.minimum + (share * (slider.maximum - slider.minimum))));
}

void OptionsPage::giveKey(const nx::platform::WindowEvent& event) {
    const std::vector<const OptionRow*> all = shownRows();
    const auto* const key = std::get_if<nx::platform::KeyChanged>(&event);
    const bool isLeft = key != nullptr && key->isDown && key->key == nx::platform::Key::Escape;
    if (isLeft || *waitingRow >= all.size() || std::get<OptionKeys>(all[*waitingRow]->control).bind(event)) {
        waitingRow.reset();
    }
}

auto OptionsPage::handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition cursor,
                         const MainMenuLayout& layout) -> OptionsOutcome {
    if (waitingRow) {
        giveKey(event);
        return OptionsOutcome::Stays;
    }
    const std::vector<const OptionRow*> all = shownRows();
    const std::size_t listRows = layout.listRows();
    const std::size_t first = firstShownOf(listRows);
    if (std::holds_alternative<nx::platform::MouseMoved>(event)) {
        if (draggedRow && *draggedRow >= first && *draggedRow < all.size()) {
            drag(std::get<OptionSlider>(all[*draggedRow]->control), layout.optionTrack(*draggedRow - first),
                 cursor.x);
        } else if (const std::optional<std::size_t> row = layout.listRowAt(cursor.x, cursor.y);
                   row && first + *row < all.size()) {
            selected = first + *row;
        }
        return OptionsOutcome::Stays;
    }
    if (const auto* const wheel = std::get_if<nx::platform::MouseWheelMoved>(&event)) {
        // Away from the user scrolls up, as a list does.
        const auto last = static_cast<std::ptrdiff_t>(all.size() - std::min(all.size(), listRows));
        const auto by = -static_cast<std::ptrdiff_t>(std::lround(wheel->dy * kRowsANotch));
        firstShown = static_cast<std::size_t>(
            std::clamp(static_cast<std::ptrdiff_t>(first) + by, std::ptrdiff_t{0}, last));
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
    return key != nullptr && key->isDown ? press(*key, listRows) : OptionsOutcome::Stays;
}

auto OptionsPage::click(nx::platform::MousePosition cursor, const MainMenuLayout& layout) -> OptionsOutcome {
    if (layout.backButton().holds(cursor.x, cursor.y)) {
        return OptionsOutcome::GoesBack;
    }
    for (const OptionCategory tab : nx::EnumArray<OptionCategory, bool>::keys()) {
        if (layout.optionTab(static_cast<std::size_t>(std::to_underlying(tab))).holds(cursor.x, cursor.y)) {
            show(tab);
            return OptionsOutcome::Stays;
        }
    }
    const std::vector<const OptionRow*> all = shownRows();
    const std::size_t first = firstShownOf(layout.listRows());
    const std::optional<std::size_t> row = layout.listRowAt(cursor.x, cursor.y);
    if (!row || first + *row >= all.size()) {
        return OptionsOutcome::Stays;
    }
    selected = first + *row;
    if (const auto* const slider = std::get_if<OptionSlider>(&all[*selected]->control)) {
        const nx::ui::PixelRect track = layout.optionTrack(*row);
        if (cursor.x >= static_cast<float>(track.x.value())) {
            drag(*slider, track, cursor.x);
            draggedRow = selected;
        }
    } else {
        activate();
    }
    return OptionsOutcome::Stays;
}

auto OptionsPage::press(const nx::platform::KeyChanged& key, std::size_t listRows) -> OptionsOutcome {
    using nx::platform::Key;
    const std::vector<const OptionRow*> all = shownRows();
    constexpr auto kTabs = nx::EnumArray<OptionCategory, bool>::size();
    const auto tab = static_cast<std::size_t>(std::to_underlying(category));
    switch (key.key) {
    case Key::Up:
        // The tabs are above the first setting.
        if (selected && *selected > 0) {
            select(*selected - 1, listRows);
        } else {
            selected.reset();
        }
        break;
    case Key::Down:
        if (!all.empty()) {
            select(selected ? std::min(*selected + 1, all.size() - 1) : 0, listRows);
        }
        break;
    case Key::Left:
        if (selected) {
            change(-1);
        } else {
            show(static_cast<OptionCategory>(tab - std::min<std::size_t>(tab, 1)));
        }
        break;
    case Key::Right:
        if (selected) {
            change(1);
        } else {
            show(static_cast<OptionCategory>(std::min(tab + 1, kTabs - 1)));
        }
        break;
    case Key::Enter:
    case Key::KeypadEnter:
        if (!key.isRepeat) {
            activate();
        }
        break;
    case Key::Delete:
    case Key::Backspace:
        if (selected && *selected < all.size()) {
            if (const auto* const keys = std::get_if<OptionKeys>(&all[*selected]->control)) {
                keys->clear();
            }
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
