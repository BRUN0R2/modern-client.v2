// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.core;
import nx.platform;
import nx.console;
import nx.rhi;
import nx.render;
import nx.ui;
import cs.shared;
import cs.client;

namespace app {

namespace {

/// The console's command each item runs, a new game's followed by its map; none for the one that goes back
/// to the map played.
constexpr nx::EnumArray<cs::MainMenuAction, std::optional<std::string_view>> kCommands{
    nx::EnumArray<cs::MainMenuAction, std::optional<std::string_view>>::Storage{std::nullopt, "disconnect",
                                                                                "map", "quit"}};
/// What quotes an argument of a command, which no argument holds.
constexpr char kQuote = '"';

/// The maps of `content` the console's `map` can be told to play.
[[nodiscard]] auto playableMapsOf(const cs::GameContent& content) -> std::vector<std::string> {
    std::vector<std::string> maps = cs::mapNamesOf(content.vfs, content.gameMounts);
    std::erase_if(maps, [](const std::string& name) { return name.contains(kQuote); });
    return maps;
}

} // namespace

ClientMainMenu::ClientMainMenu(const cs::GameContent& content)
    : menu{content.mainMenu, playableMapsOf(content)}, panel{content.mainMenu} {}

auto ClientMainMenu::isOpen() const noexcept -> bool {
    return menu.isOpen();
}

void ClientMainMenu::open(cs::MenuGame game) {
    menu.open(game);
}

void ClientMainMenu::close() noexcept {
    menu.close();
}

void ClientMainMenu::handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition mouse,
                            nx::rhi::Extent2d size, cs::MenuGame game, nx::console::Console& console) {
    const std::optional<cs::MainMenuRequest> asked = menu.handle(event, mouse, size, game);
    if (!asked) {
        return;
    }
    const std::optional<std::string_view> command = kCommands[asked->action];
    if (!command) {
        menu.close();
    } else if (asked->map.empty()) {
        console.addText(*command);
    } else {
        console.addText(std::format("{} {}{}{}", *command, kQuote, asked->map, kQuote));
    }
}

auto ClientMainMenu::load(nx::ui::FontCatalog& fonts, nx::render::ScreenRenderer& screen,
                          nx::rhi::CommandList& list, nx::rhi::Extent2d size)
    -> nx::Result<void, ClientError> {
    if (panel.isLoadedFor(size.height)) {
        return {};
    }
    if (auto loaded = panel.load(fonts, screen, list, size.height); !loaded) {
        const ClientError code = loaded.error().code == cs::MenuPanelError::GpuFailed
                                     ? ClientError::GpuFailed
                                     : ClientError::ContentUnavailable;
        return nx::fail(code, "the main menu", std::move(loaded.error()));
    }
    return {};
}

void ClientMainMenu::append(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size,
                            cs::MenuGame game) {
    if (menu.isOpen()) {
        panel.append(quads, menu.frameOf(game, size), size);
    }
}

} // namespace app
