// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The team and appearance menus over a map played (docs/12-ui-hud-and-console.md, section 4): what they show
/// (cs::TeamMenu), what draws them (cs::TeamMenuPanel), and what their choices ask of the game, its player
/// choosing its team (cs::Game::chooseTeam) and its character (cs::Game::chooseCharacter); opened as a map
/// starts, as the original asks a player joining, and by `chooseteam`.
export module app.client:teamMenu;

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
import :client;
import :session;

namespace app {

class ClientTeamMenu {
public:
    /// The menus of the characters of `content` for the player of the session `session` gives, writing
    /// `texts`; `content` and `texts` must outlive it.
    ClientTeamMenu(const cs::GameContent& content, const cs::Texts& texts, PlayedSession session);

    ClientTeamMenu(const ClientTeamMenu&) = delete;
    auto operator=(const ClientTeamMenu&) -> ClientTeamMenu& = delete;
    ClientTeamMenu(ClientTeamMenu&&) = delete;
    auto operator=(ClientTeamMenu&&) -> ClientTeamMenu& = delete;
    ~ClientTeamMenu() = default;

    /// Writes `texts` from now on, which must outlive it.
    inline void writeIn(const cs::Texts& texts) noexcept { written = &texts; }

    /// Registers `chooseteam` in `console`: opens the team menu over a map played, with 0 to cancel once the
    /// player chose a team. Errors: SettingsUnavailable, with the cause.
    [[nodiscard]] auto registerCommands(nx::console::Console& console) -> nx::Result<void, ClientError>;

    /// Opens the team menu for the player of the map just started, who has not chosen a team.
    void openForJoining();
    [[nodiscard]] auto isOpen() const noexcept -> bool;
    /// Closes: another map is played, or none.
    void reset() noexcept;

    /// What `event` does to the menu, open over a map played, the mouse at `mouse` on a screen of `size`; it
    /// takes every event while open.
    [[nodiscard]] auto handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition mouse,
                              nx::rhi::Extent2d size) -> cs::TeamMenuEffect;

    /// Puts what draws it on the GPU in `list` with `screen`, its faces from `fonts` at `screenHeight` and its
    /// pictures from `vfs`, while it is open. One that cannot be loaded is logged, and the menu goes without
    /// what draws it.
    void load(nx::ui::FontCatalog& fonts, const nx::assets::Vfs& vfs, nx::render::ScreenRenderer& screen,
              nx::rhi::CommandList& list, std::uint32_t screenHeight);
    /// While it is open over a map played: the menu, over the HUD.
    void append(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size);

private:
    [[nodiscard]] auto actions() -> cs::TeamMenuActions;
    /// What it shows of the player of `session`.
    [[nodiscard]] auto frameOf(const Session& session) const -> cs::TeamMenuFrame;

    const cs::GameContent* content;
    const cs::Texts* written;
    PlayedSession played;
    cs::TeamMenu menu;
    std::optional<cs::TeamMenuPanel> panel;
};

} // namespace app
