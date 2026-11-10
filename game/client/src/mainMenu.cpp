// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.rhi;
import nx.platform;
import cs.shared;

namespace cs {

namespace {

/// How many rows of the list a notch of the wheel scrolls.
constexpr float kRowsANotch = 3.0F;

} // namespace

auto mainMenuItemsOf(const MainMenuLook& look, MenuGame game) -> std::vector<MainMenuAction> {
    const auto isListed = [game](const MainMenuItem& item) {
        return item.listed == MainMenuListing::Always || game == MenuGame::Played;
    };
    return look.items | std::views::filter(isListed) | std::views::transform(&MainMenuItem::action) |
           std::ranges::to<std::vector>();
}

MainMenu::MainMenu(const MainMenuLook& listed, std::vector<std::string> playable) noexcept
    : look{&listed}, maps{std::move(playable)} {}

void MainMenu::open(MenuGame game) {
    const std::vector<MainMenuAction> items = mainMenuItemsOf(*look, game);
    isShown = true;
    isChoosingMap = false;
    selection = items.empty() ? std::nullopt : std::optional{items.front()};
}

void MainMenu::close() noexcept {
    isShown = false;
    isChoosingMap = false;
}

auto MainMenu::handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition cursor,
                      nx::rhi::Extent2d size, MenuGame game) -> std::optional<MainMenuRequest> {
    const std::vector<MainMenuAction> items = mainMenuItemsOf(*look, game);
    const MainMenuLayout layout{size, items.size()};
    if (std::holds_alternative<nx::platform::MouseMoved>(event)) {
        // The item whose page is open stays the one selected.
        if (const std::optional<std::size_t> item = layout.itemAt(cursor.x, cursor.y);
            item && !isChoosingMap) {
            selection = items[*item];
        }
        return std::nullopt;
    }
    if (const auto* const wheel = std::get_if<nx::platform::MouseWheelMoved>(&event)) {
        if (isChoosingMap) {
            // Away from the user scrolls up, as a list does.
            scrollMaps(-static_cast<std::ptrdiff_t>(std::lround(wheel->dy * kRowsANotch)), layout.listRows());
        }
        return std::nullopt;
    }
    if (const auto* const button = std::get_if<nx::platform::MouseButtonChanged>(&event)) {
        return button->isDown && button->button == nx::platform::MouseButton::Left
                   ? click(cursor, layout, items)
                   : std::nullopt;
    }
    const auto* const key = std::get_if<nx::platform::KeyChanged>(&event);
    if (key == nullptr || !key->isDown) {
        return std::nullopt;
    }
    return isChoosingMap ? pressOnNewGame(*key, layout.listRows()) : pressOnItems(*key, items);
}

auto MainMenu::choose(MainMenuAction action) -> std::optional<MainMenuRequest> {
    selection = action;
    isChoosingMap = action == MainMenuAction::NewGame;
    return isChoosingMap ? std::nullopt : std::optional{MainMenuRequest{.action = action}};
}

auto MainMenu::click(nx::platform::MousePosition cursor, const MainMenuLayout& layout,
                     std::span<const MainMenuAction> items) -> std::optional<MainMenuRequest> {
    if (const std::optional<std::size_t> item = layout.itemAt(cursor.x, cursor.y)) {
        return choose(items[*item]);
    }
    if (!isChoosingMap) {
        return std::nullopt;
    }
    if (const std::optional<std::size_t> row = layout.listRowAt(cursor.x, cursor.y)) {
        if (const std::size_t pressed = firstMapShownOf(layout.listRows()) + *row; pressed < maps.size()) {
            selectedMap = pressed;
        }
        return std::nullopt;
    }
    if (layout.confirmButton().holds(cursor.x, cursor.y)) {
        return playSelectedMap();
    }
    if (layout.backButton().holds(cursor.x, cursor.y)) {
        isChoosingMap = false;
    }
    return std::nullopt;
}

auto MainMenu::pressOnItems(const nx::platform::KeyChanged& key, std::span<const MainMenuAction> items)
    -> std::optional<MainMenuRequest> {
    using nx::platform::Key;
    switch (key.key) {
    case Key::Up:
        select(items, Step::Before);
        return std::nullopt;
    case Key::Down:
        select(items, Step::After);
        return std::nullopt;
    case Key::Enter:
    case Key::KeypadEnter:
        return !key.isRepeat && selection && std::ranges::contains(items, *selection) ? choose(*selection)
                                                                                      : std::nullopt;
    case Key::Escape:
        return !key.isRepeat && std::ranges::contains(items, MainMenuAction::ResumeGame)
                   ? std::optional{MainMenuRequest{.action = MainMenuAction::ResumeGame}}
                   : std::nullopt;
    default:
        return std::nullopt;
    }
}

auto MainMenu::pressOnNewGame(const nx::platform::KeyChanged& key, std::size_t rows)
    -> std::optional<MainMenuRequest> {
    using nx::platform::Key;
    switch (key.key) {
    case Key::Up:
        selectMap(selectedMap - std::min<std::size_t>(selectedMap, 1), rows);
        return std::nullopt;
    case Key::Down:
        selectMap(selectedMap + 1, rows);
        return std::nullopt;
    case Key::PageUp:
        selectMap(selectedMap - std::min(selectedMap, rows), rows);
        return std::nullopt;
    case Key::PageDown:
        selectMap(selectedMap + rows, rows);
        return std::nullopt;
    case Key::Home:
        selectMap(0, rows);
        return std::nullopt;
    case Key::End:
        selectMap(maps.size(), rows);
        return std::nullopt;
    case Key::Enter:
    case Key::KeypadEnter:
        return key.isRepeat ? std::nullopt : playSelectedMap();
    case Key::Escape:
        if (!key.isRepeat) {
            isChoosingMap = false;
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

void MainMenu::select(std::span<const MainMenuAction> items, Step step) {
    if (items.empty()) {
        return;
    }
    const auto found = selection ? std::ranges::find(items, *selection) : items.end();
    if (found == items.end()) {
        selection = step == Step::After ? items.front() : items.back();
        return;
    }
    const auto index = static_cast<std::size_t>(found - items.begin());
    const std::size_t next = step == Step::After ? index + 1 : index + items.size() - 1;
    selection = items[next % items.size()];
}

void MainMenu::selectMap(std::size_t index, std::size_t rows) noexcept {
    if (maps.empty()) {
        return;
    }
    selectedMap = std::min(index, maps.size() - 1);
    if (selectedMap < firstMapShown) {
        firstMapShown = selectedMap;
    } else if (selectedMap >= firstMapShown + rows) {
        firstMapShown = selectedMap + 1 - rows;
    }
}

void MainMenu::scrollMaps(std::ptrdiff_t by, std::size_t rows) noexcept {
    const auto last = static_cast<std::ptrdiff_t>(maps.size() - std::min(maps.size(), rows));
    firstMapShown = static_cast<std::size_t>(
        std::clamp(static_cast<std::ptrdiff_t>(firstMapShown) + by, std::ptrdiff_t{0}, last));
}

auto MainMenu::firstMapShownOf(std::size_t rows) const noexcept -> std::size_t {
    return std::min(firstMapShown, maps.size() - std::min(maps.size(), rows));
}

auto MainMenu::playSelectedMap() const -> std::optional<MainMenuRequest> {
    return selectedMap < maps.size()
               ? std::optional{MainMenuRequest{.action = MainMenuAction::NewGame, .map = maps[selectedMap]}}
               : std::nullopt;
}

auto MainMenu::frameOf(MenuGame game, nx::rhi::Extent2d size) const -> MainMenuFrame {
    MainMenuFrame frame{.game = game, .items = mainMenuItemsOf(*look, game)};
    if (selection && std::ranges::contains(frame.items, *selection)) {
        frame.selected = selection;
    }
    if (isChoosingMap) {
        const std::size_t rows = MainMenuLayout{size, frame.items.size()}.listRows();
        const std::size_t first = firstMapShownOf(rows);
        NewGamePage page{.first = first, .count = maps.size()};
        page.rows.assign_range(maps | std::views::drop(first) | std::views::take(rows));
        if (selectedMap >= first && selectedMap < first + page.rows.size()) {
            page.selectedRow = selectedMap - first;
        }
        frame.newGame = std::move(page);
    }
    return frame;
}

} // namespace cs
