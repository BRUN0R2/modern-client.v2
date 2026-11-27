// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.core;
import nx.platform;
import nx.console;
import nx.assets;
import nx.rhi;
import nx.render;
import nx.ui;
import cs.shared;
import cs.client;

namespace app {

namespace {

/// The console's command each item runs, a new game's followed by its map; none for the one that goes back
/// to the map played, and for the options, which the menu shows itself and never asks for.
constexpr nx::EnumArray<cs::MainMenuAction, std::optional<std::string_view>> kCommands{
    nx::EnumArray<cs::MainMenuAction, std::optional<std::string_view>>::Storage{std::nullopt, "disconnect",
                                                                                "map", std::nullopt, "quit"}};
/// What quotes an argument of a command, which no argument holds.
constexpr char kQuote = '"';

/// The maps of `content` the console's `map` can be told to play.
[[nodiscard]] auto playableMapsOf(const cs::GameContent& content) -> std::vector<std::string> {
    std::vector<std::string> maps = cs::mapNamesOf(content.vfs, content.gameMounts);
    std::erase_if(maps, [](const std::string& name) { return name.contains(kQuote); });
    return maps;
}

/// What changed between `before` and `after`, two frames of the menu an event came between, sounds: a page or
/// a tab opened or closed, or a request, a click; another item, row or value armed, a rollover.
[[nodiscard]] auto feedbackOf(const cs::MainMenuFrame& before, const cs::MainMenuFrame& after, bool asked)
    -> MenuFeedback {
    const bool isOptionsOpen = after.options.has_value();
    const bool isPageChanged = before.newGame.has_value() != after.newGame.has_value() ||
                               before.options.has_value() != isOptionsOpen;
    const bool isTabChanged =
        isOptionsOpen && before.options && before.options->category != after.options->category;
    const bool isChoiceMenuChanged =
        isOptionsOpen && before.options &&
        before.options->choiceMenu.has_value() != after.options->choiceMenu.has_value();
    if (asked || isPageChanged || isTabChanged || isChoiceMenuChanged) {
        return MenuFeedback::Click;
    }
    const auto armedOf = [](const cs::MainMenuFrame& frame) {
        std::optional<std::size_t> row;
        std::optional<std::size_t> value;
        if (frame.newGame) {
            row = frame.newGame->selectedRow;
        } else if (frame.options) {
            row = frame.options->selectedRow;
            if (frame.options->choiceMenu) {
                value = frame.options->choiceMenu->highlighted;
            }
        }
        return std::tuple{frame.selected, row, value};
    };
    const auto armed = armedOf(after);
    const bool isSomethingArmed = std::get<0>(armed) || std::get<1>(armed) || std::get<2>(armed);
    return isSomethingArmed && armed != armedOf(before) ? MenuFeedback::Rollover : MenuFeedback::None;
}

} // namespace

auto menuSoundOf(MenuFeedback feedback) noexcept -> std::optional<std::string_view> {
    switch (feedback) {
    case MenuFeedback::Rollover:
        return "UI/buttonrollover.wav";
    case MenuFeedback::Click:
        return "UI/buttonclick.wav";
    case MenuFeedback::None:
        break;
    }
    return std::nullopt;
}

ClientMainMenu::ClientMainMenu(const cs::GameContent& content, const cs::Texts& texts,
                               std::vector<cs::OptionRow> settings, cs::CrosshairEditor crosshair)
    : files{&content.vfs},
      menu{content.mainMenu, playableMapsOf(content), std::move(settings), std::move(crosshair)},
      panel{content.mainMenu, texts} {}

void ClientMainMenu::writeIn(const cs::Texts& texts) noexcept {
    panel.writeIn(texts);
}

auto ClientMainMenu::isOpen() const noexcept -> bool {
    return menu.isOpen();
}

auto ClientMainMenu::takesEveryKey() const noexcept -> bool {
    return menu.takesEveryKey();
}

auto ClientMainMenu::isTyping() const noexcept -> bool {
    return menu.isTyping();
}

void ClientMainMenu::open(cs::MenuGame game) {
    menu.open(game);
}

void ClientMainMenu::close() noexcept {
    menu.close();
}

auto ClientMainMenu::handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition mouse,
                            nx::rhi::Extent2d size, cs::MenuGame game, nx::console::Console& console)
    -> MenuFeedback {
    const cs::MainMenuFrame before = menu.frameOf(game, size);
    const std::optional<cs::MainMenuRequest> asked = menu.handle(event, mouse, size, game);
    const MenuFeedback feedback = feedbackOf(before, menu.frameOf(game, size), asked.has_value());
    if (!asked) {
        return feedback;
    }
    const std::optional<std::string_view> command = kCommands[asked->action];
    if (!command) {
        menu.close();
    } else if (asked->map.empty()) {
        console.addText(*command);
    } else {
        console.addText(std::format("{} {}{}{}", *command, kQuote, asked->map, kQuote));
    }
    return feedback;
}

auto ClientMainMenu::load(nx::ui::FontCatalog& fonts, nx::render::ScreenRenderer& screen,
                          nx::rhi::CommandList& list, nx::rhi::Extent2d size,
                          const std::filesystem::path& data) -> nx::Result<void, ClientError> {
    if (panel.isLoadedFor(size.height)) {
        return {};
    }
    const auto logo = data / "cstrike" / "modernClientLogo.tga";
    if (auto loaded = panel.load(fonts, *files, screen, list, logo, size.height); !loaded) {
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

void ClientMainMenu::appendLoading(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size,
                                   std::string_view map) {
    panel.appendLoading(quads, map, size);
}

} // namespace app
