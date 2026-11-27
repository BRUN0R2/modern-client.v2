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
import cs.server;
import cs.client;

namespace app {

namespace {

constexpr nx::log::Category kLog{"client"};

/// What the menu tells of `refusal` of `team`, in `texts`.
[[nodiscard]] auto refusalText(const cs::TeamMenuTexts& texts, cs::TeamRefusal refusal, cs::Team team)
    -> std::string {
    const bool isTerrorists = team == cs::Team::Terrorists;
    switch (refusal) {
    case cs::TeamRefusal::TeamFull:
        return isTerrorists ? texts.terroristsFull : texts.counterTerroristsFull;
    case cs::TeamRefusal::TeamStacked:
        return isTerrorists ? texts.tooManyTerrorists : texts.tooManyCounterTerrorists;
    case cs::TeamRefusal::OnlyOneChange:
        return texts.onlyOneTeamChange;
    }
    return {};
}

} // namespace

ClientTeamMenu::ClientTeamMenu(const cs::GameContent& game, const cs::Texts& texts, PlayedSession session)
    : content{&game}, written{&texts}, played{std::move(session)}, menu{actions()},
      panel{std::in_place, game.buyMenu} {}

auto ClientTeamMenu::actions() -> cs::TeamMenuActions {
    return cs::TeamMenuActions{
        .chooseTeam = [this](cs::Team team) -> std::optional<std::string> {
            Session* const session = played();
            if (session == nullptr) {
                return std::nullopt;
            }
            if (auto chosen = session->game->chooseTeam(session->player, team); !chosen) {
                return refusalText(written->teamMenu, chosen.error().code, team);
            }
            return std::nullopt;
        },
        .defaultTeam = [this]() -> cs::Team {
            const Session* const session = played();
            return session != nullptr ? session->game->defaultTeam() : cs::Team::CounterTerrorists;
        },
        .chooseCharacter =
            [this](std::uint32_t slot) {
                if (Session* const session = played(); session != nullptr) {
                    session->game->chooseCharacter(session->player, slot);
                }
            }};
}

auto ClientTeamMenu::registerCommands(nx::console::Console& console) -> nx::Result<void, ClientError> {
    auto registered = console.registerCommand(
        {.name = "chooseteam", .description = "Opens the team menu."},
        [this](nx::console::Console& on, const nx::console::CommandLine&)
            -> nx::Result<void, nx::console::ConsoleError> {
            const Session* const session = played();
            if (session == nullptr) {
                on.print("Can't choose a team: no map is played");
                return {};
            }
            // chooseteam: nothing while the appearance menu is open.
            if (!(menu.isOpen() && menu.page() == cs::TeamMenuPage::Character)) {
                menu.open(session->game->hasChosenTeam(session->player));
            }
            return {};
        });
    if (!registered) {
        return nx::fail(ClientError::SettingsUnavailable, std::move(registered.error()));
    }
    return {};
}

void ClientTeamMenu::openForJoining() {
    if (const Session* const session = played();
        session != nullptr && panel && !session->game->hasChosenTeam(session->player)) {
        menu.open(false);
    }
}

auto ClientTeamMenu::isOpen() const noexcept -> bool {
    return menu.isOpen() && panel && played() != nullptr;
}

void ClientTeamMenu::reset() noexcept {
    menu.close();
}

auto ClientTeamMenu::frameOf(const Session& session) const -> cs::TeamMenuFrame {
    return menu.frameOf(written->teamMenu, content->characters, session.game->teamOf(session.player));
}

auto ClientTeamMenu::handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition mouse,
                            nx::rhi::Extent2d size) -> cs::TeamMenuEffect {
    const Session* const session = played();
    if (session == nullptr || !menu.isOpen()) {
        return cs::TeamMenuEffect::None;
    }
    const cs::TeamMenuFrame frame = frameOf(*session);
    return menu.handle(event, mouse, size, frame.rows);
}

void ClientTeamMenu::load(nx::ui::FontCatalog& fonts, const nx::assets::Vfs& vfs,
                          nx::render::ScreenRenderer& screen, nx::rhi::CommandList& list,
                          std::uint32_t screenHeight) {
    if (!panel || !isOpen() || panel->isLoadedFor(screenHeight)) {
        return;
    }
    if (auto loaded = panel->load(fonts, vfs, screen, list, content->characters, screenHeight); !loaded) {
        nx::log::warning(kLog, "the team menu cannot be drawn: {}", nx::describe(loaded.error()));
        panel.reset();
        menu.close(); // a menu that cannot be seen takes no keys
    }
}

void ClientTeamMenu::append(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size) {
    const Session* const session = played();
    if (session == nullptr || !panel || !menu.isOpen()) {
        return;
    }
    panel->append(quads, frameOf(*session), size);
}

} // namespace app
