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

/// The console's command each item runs; none for the one that goes back to the map played.
constexpr nx::EnumArray<cs::MainMenuAction, std::optional<std::string_view>> kCommands{
    nx::EnumArray<cs::MainMenuAction, std::optional<std::string_view>>::Storage{std::nullopt, "disconnect",
                                                                                "quit"}};

} // namespace

ClientMainMenu::ClientMainMenu(const cs::MainMenuLook& listed) noexcept : menu{listed}, panel{listed} {}

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
    const std::optional<cs::MainMenuAction> chosen = menu.handle(event, mouse, size, game);
    if (!chosen) {
        return;
    }
    if (const std::optional<std::string_view> command = kCommands[*chosen]) {
        console.addText(*command);
    } else {
        menu.close();
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
        panel.append(quads, menu.frameOf(game), size);
    }
}

} // namespace app
