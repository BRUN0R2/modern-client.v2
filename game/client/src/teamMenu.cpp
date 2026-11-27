// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.rhi;
import nx.ui;
import nx.platform;
import cs.shared;

namespace cs {

namespace {

// The design, in its units: as the buy menu's.
constexpr MenuUnits kMarginUnits{64};
constexpr MenuUnits kMostContentUnits{1280};
constexpr MenuUnits kTopUnits{120};
constexpr MenuUnits kHeaderTallUnits{96};
constexpr MenuUnits kRuleTallUnits{2};
constexpr MenuUnits kUnderHeaderUnits{24};
constexpr MenuUnits kRailWideUnits{520};
constexpr MenuUnits kColumnGapUnits{16};
constexpr MenuUnits kRowTallUnits{84};
constexpr MenuUnits kRowGapUnits{8};
constexpr std::size_t kMostRows = 6;
constexpr MenuUnits kRefusalGapUnits{20};
constexpr MenuUnits kRefusalTallUnits{40};
/// What the room left is divided by to put a thing halfway into it.
constexpr std::int32_t kMiddleDivisor = 2;

/// The digit keys and the number each stands for.
constexpr std::array<std::pair<nx::platform::Key, std::uint32_t>, 10> kDigitKeys{{
    {nx::platform::Key::Digit1, 1},
    {nx::platform::Key::Digit2, 2},
    {nx::platform::Key::Digit3, 3},
    {nx::platform::Key::Digit4, 4},
    {nx::platform::Key::Digit5, 5},
    {nx::platform::Key::Digit6, 6},
    {nx::platform::Key::Digit7, 7},
    {nx::platform::Key::Digit8, 8},
    {nx::platform::Key::Digit9, 9},
    {nx::platform::Key::Digit0, 0},
}};

/// The picture of each team's character `name` on the appearance menu, and of auto-select.
[[nodiscard]] auto characterPicture(std::string_view name) -> std::string {
    return std::format("gfx/vgui/{}.tga", name);
}
constexpr nx::EnumArray<Team, std::string_view> kRandomPictures{
    nx::EnumArray<Team, std::string_view>::Storage{"gfx/vgui/t_random.tga", "gfx/vgui/ct_random.tga"}};

} // namespace

TeamMenuLayout::TeamMenuLayout(nx::rhi::Extent2d size) noexcept : scale{size.height} {
    const nx::Pixels wide{nx::narrow<std::int32_t>(size.width)};
    const nx::Pixels contentWide =
        std::min(pixels(kMostContentUnits), wide - (kMiddleDivisor * pixels(kMarginUnits)));
    const nx::Pixels left = (wide - contentWide) / kMiddleDivisor;
    headerRect = {.x = left, .y = pixels(kTopUnits), .wide = contentWide, .tall = pixels(kHeaderTallUnits)};
    const nx::Pixels top = headerRect.y + headerRect.tall + pixels(kUnderHeaderUnits);
    const auto rows = nx::narrow<std::int32_t>(kMostRows);
    const nx::Pixels railTall = (rows * pixels(kRowTallUnits)) + ((rows - 1) * pixels(kRowGapUnits));
    railRect = {.x = left, .y = top, .wide = pixels(kRailWideUnits), .tall = railTall};
    const nx::Pixels detailX = railRect.x + railRect.wide + pixels(kColumnGapUnits);
    detailRect = {.x = detailX, .y = top, .wide = left + contentWide - detailX, .tall = railTall};
}

auto TeamMenuLayout::rule() const noexcept -> nx::ui::PixelRect {
    const nx::Pixels tall = pixels(kRuleTallUnits);
    return {.x = headerRect.x, .y = headerRect.y + headerRect.tall - tall, .wide = headerRect.wide, .tall = tall};
}

auto TeamMenuLayout::row(std::size_t index) const noexcept -> nx::ui::PixelRect {
    const nx::Pixels tall = pixels(kRowTallUnits);
    return {.x = railRect.x,
            .y = railRect.y + (nx::narrow<std::int32_t>(index) * (tall + pixels(kRowGapUnits))),
            .wide = railRect.wide,
            .tall = tall};
}

auto TeamMenuLayout::rowAt(float x, float y, std::size_t count) const noexcept -> std::optional<std::size_t> {
    for (std::size_t index = 0; index < count; ++index) {
        if (row(index).holds(x, y)) {
            return index;
        }
    }
    return std::nullopt;
}

auto TeamMenuLayout::refusal() const noexcept -> nx::ui::PixelRect {
    return {.x = railRect.x,
            .y = railRect.y + railRect.tall + pixels(kRefusalGapUnits),
            .wide = headerRect.wide,
            .tall = pixels(kRefusalTallUnits)};
}

TeamMenu::TeamMenu(TeamMenuActions asked) noexcept : actions{std::move(asked)} {}

void TeamMenu::open(bool canCancel) {
    isShown = true;
    isCancelable = canCancel;
    shownPage = TeamMenuPage::Team;
    selection = 0;
    refused.clear();
}

void TeamMenu::close() noexcept {
    isShown = false;
    refused.clear();
}

auto TeamMenu::choose(std::uint32_t key, std::span<const TeamMenuRow> rows) -> TeamMenuEffect {
    if (!std::ranges::contains(rows, key, &TeamMenuRow::key)) {
        return TeamMenuEffect::None;
    }
    if (shownPage == TeamMenuPage::Character) {
        actions.chooseCharacter(key);
        close();
        return TeamMenuEffect::Click;
    }
    if (key == kCancelKey) {
        close();
        return TeamMenuEffect::Click;
    }
    const Team team = key == kTerroristsKey          ? Team::Terrorists
                      : key == kCounterTerroristsKey ? Team::CounterTerrorists
                                                     : actions.defaultTeam();
    if (std::optional<std::string> refusal = actions.chooseTeam(team)) {
        refused = std::move(*refusal);
        return TeamMenuEffect::Click;
    }
    refused.clear();
    shownPage = TeamMenuPage::Character;
    characterTeam = team;
    selection = 0;
    return TeamMenuEffect::Click;
}

auto TeamMenu::handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition cursor,
                      nx::rhi::Extent2d size, std::span<const TeamMenuRow> rows) -> TeamMenuEffect {
    using nx::platform::Key;
    if (!isShown || rows.empty()) {
        return TeamMenuEffect::None;
    }
    const auto select = [&](std::optional<std::size_t> row) {
        if (!row || row == selection) {
            return TeamMenuEffect::None;
        }
        selection = row;
        return TeamMenuEffect::Rollover;
    };
    if (const auto* key = std::get_if<nx::platform::KeyChanged>(&event); key != nullptr && key->isDown) {
        for (const auto& [digit, number] : kDigitKeys) {
            if (key->key == digit) {
                return key->isRepeat ? TeamMenuEffect::None : choose(number, rows);
            }
        }
        const std::size_t last = rows.size() - 1;
        switch (key->key) {
        case Key::Up:
            return select(selection && *selection > 0 ? *selection - 1 : last);
        case Key::Down:
            return select(selection && *selection < last ? *selection + 1 : 0);
        case Key::Enter:
        case Key::KeypadEnter:
            return selection && *selection < rows.size() ? choose(rows[*selection].key, rows)
                                                         : TeamMenuEffect::None;
        case Key::Escape:
            if (shownPage == TeamMenuPage::Character) {
                return choose(kAutoSelectKey, rows);
            }
            return isCancelable ? choose(kCancelKey, rows) : TeamMenuEffect::None;
        default:
            return TeamMenuEffect::None;
        }
    }
    const TeamMenuLayout layout{size};
    if (std::holds_alternative<nx::platform::MouseMoved>(event)) {
        return select(layout.rowAt(cursor.x, cursor.y, rows.size()));
    }
    if (const auto* button = std::get_if<nx::platform::MouseButtonChanged>(&event);
        button != nullptr && button->isDown && button->button == nx::platform::MouseButton::Left) {
        if (const std::optional<std::size_t> row = layout.rowAt(cursor.x, cursor.y, rows.size())) {
            selection = row;
            return choose(rows[*row].key, rows);
        }
    }
    return TeamMenuEffect::None;
}

auto TeamMenu::frameOf(const TeamMenuTexts& texts, const Characters& characters, Team team) const
    -> TeamMenuFrame {
    TeamMenuFrame frame{.page = shownPage, .team = team, .selected = selection, .refusal = refused};
    if (shownPage == TeamMenuPage::Team) {
        frame.title = texts.chooseTeam;
        frame.rows = {
            TeamMenuRow{.key = kTerroristsKey,
                        .label = texts.terrorists,
                        .picture = characterPicture("terror"),
                        .accent = Team::Terrorists},
            TeamMenuRow{.key = kCounterTerroristsKey,
                        .label = texts.counterTerrorists,
                        .picture = characterPicture("urban"),
                        .accent = Team::CounterTerrorists},
            TeamMenuRow{.key = kAutoSelectKey, .label = texts.autoSelect, .accent = team},
        };
        if (isCancelable) {
            frame.rows.push_back(TeamMenuRow{.key = kCancelKey, .label = texts.cancel, .accent = team});
        }
    } else {
        frame.team = characterTeam;
        frame.title = texts.chooseCharacter;
        std::uint32_t key = 1;
        for (const CharacterIndex index : characters.ofTeam(characterTeam)) {
            const Character& character = characters.character(index);
            frame.rows.push_back(TeamMenuRow{.key = key++,
                                             .label = std::string{character.shownName()},
                                             .picture = characterPicture(character.name),
                                             .accent = characterTeam});
        }
        frame.rows.push_back(TeamMenuRow{.key = kAutoSelectKey,
                                         .label = texts.autoSelect,
                                         .picture = std::string{kRandomPictures[characterTeam]},
                                         .accent = characterTeam});
    }
    if (frame.selected && *frame.selected >= frame.rows.size()) {
        frame.selected.reset();
    }
    return frame;
}

} // namespace cs
