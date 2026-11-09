// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.rhi;
import nx.platform;
import cs.shared;

namespace cs {

auto mainMenuItemsOf(const MainMenuLook& look, MenuGame game) -> std::vector<MainMenuAction> {
    const auto isListed = [game](const MainMenuItem& item) {
        return item.listed == MainMenuListing::Always || game == MenuGame::Played;
    };
    return look.items | std::views::filter(isListed) | std::views::transform(&MainMenuItem::action) |
           std::ranges::to<std::vector>();
}

MainMenu::MainMenu(const MainMenuLook& listed) noexcept : look{&listed} {}

void MainMenu::open(MenuGame game) {
    const std::vector<MainMenuAction> items = mainMenuItemsOf(*look, game);
    isShown = true;
    selection = items.empty() ? std::nullopt : std::optional{items.front()};
}

void MainMenu::close() noexcept {
    isShown = false;
}

auto MainMenu::handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition cursor,
                      nx::rhi::Extent2d size, MenuGame game) -> std::optional<MainMenuAction> {
    const std::vector<MainMenuAction> items = mainMenuItemsOf(*look, game);
    const auto pointed = [&]() -> std::optional<MainMenuAction> {
        const std::optional<std::size_t> index =
            MainMenuLayout{size, items.size()}.itemAt(cursor.x, cursor.y);
        return index ? std::optional{items[*index]} : std::nullopt;
    };
    if (std::holds_alternative<nx::platform::MouseMoved>(event)) {
        if (const std::optional<MainMenuAction> item = pointed()) {
            selection = item;
        }
        return std::nullopt;
    }
    if (const auto* const button = std::get_if<nx::platform::MouseButtonChanged>(&event)) {
        if (!button->isDown || button->button != nx::platform::MouseButton::Left) {
            return std::nullopt;
        }
        const std::optional<MainMenuAction> item = pointed();
        if (item) {
            selection = item;
        }
        return item;
    }
    const auto* const key = std::get_if<nx::platform::KeyChanged>(&event);
    return key != nullptr && key->isDown ? press(*key, items) : std::nullopt;
}

auto MainMenu::press(const nx::platform::KeyChanged& key, std::span<const MainMenuAction> items)
    -> std::optional<MainMenuAction> {
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
        return !key.isRepeat && selection && std::ranges::contains(items, *selection) ? selection
                                                                                      : std::nullopt;
    case Key::Escape:
        return !key.isRepeat && std::ranges::contains(items, MainMenuAction::ResumeGame)
                   ? std::optional{MainMenuAction::ResumeGame}
                   : std::nullopt;
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

auto MainMenu::frameOf(MenuGame game) const -> MainMenuFrame {
    MainMenuFrame frame{.game = game, .items = mainMenuItemsOf(*look, game)};
    if (selection && std::ranges::contains(frame.items, *selection)) {
        frame.selected = selection;
    }
    return frame;
}

} // namespace cs
