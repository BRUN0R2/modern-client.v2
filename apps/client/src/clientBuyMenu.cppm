// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The buy menu of the map played (docs/12-ui-hud-and-console.md, section 4), as the client runs it.
export module app.client:buyMenu;

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

/// The buy menu of the map played: what it shows (cs::BuyMenu) and what draws it (cs::BuyMenuPanel, none
/// once its fonts or pictures cannot be had), the console's buy commands and its own, and the notice of
/// what was refused. It stays where it is made: its commands point into it.
class ClientBuyMenu {
public:
    /// The menu of what `sold` sells to the player of the session `session` gives, its notices timed by
    /// `clock`, the map's; `sold` and `clock` must outlive it.
    ClientBuyMenu(const cs::GameContent& sold, PlayedSession session, const nx::platform::Stopwatch& clock);

    ClientBuyMenu(const ClientBuyMenu&) = delete;
    auto operator=(const ClientBuyMenu&) -> ClientBuyMenu& = delete;
    ClientBuyMenu(ClientBuyMenu&&) = delete;
    auto operator=(ClientBuyMenu&&) -> ClientBuyMenu& = delete;
    ~ClientBuyMenu() = default;

    /// Registers in `console` the buy commands (cs::registerBuyCommands: `ak47`, `vesthelm`, `rebuy`...)
    /// over the weapons of `weapons`, and the menu's (cs::registerBuyMenuCommands: `buy`, `buyequip`,
    /// `slot1`...). Errors: SettingsUnavailable, with the cause.
    [[nodiscard]] auto registerCommands(nx::console::Console& console, const cs::WeaponCatalog& weapons)
        -> nx::Result<void, ClientError>;

    /// Whether it is open.
    [[nodiscard]] auto isOpen() const noexcept -> bool;
    /// Whether it shows over the frame: open over a map played, with what draws it.
    [[nodiscard]] auto isShown() const noexcept -> bool;
    /// Stops offering what the player may no longer buy, closing once it may buy nothing, over a map played.
    void update();
    /// Whether the menu, open over a map played, takes `event`, the mouse at `mouse` on a screen of `size`.
    [[nodiscard]] auto handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition mouse,
                              nx::rhi::Extent2d size) -> bool;
    /// Closes, and forgets what was refused: another map is played.
    void reset();

    /// Whether what draws it must be put on the GPU before the frame: before it first opens, and while it
    /// is open, for a weapon made since or another height.
    [[nodiscard]] auto needsLoading() const noexcept -> bool;
    /// Puts what draws it on the GPU in `list` with `screen`, its faces from `fonts` at `screenHeight` and
    /// its pictures from `vfs`. One that cannot be loaded is logged, and the menu goes without what draws it.
    void load(nx::ui::FontCatalog& fonts, const nx::assets::Vfs& vfs, nx::render::ScreenRenderer& screen,
              nx::rhi::CommandList& list, std::uint32_t screenHeight);

    /// While it shows: the scene darkened under the HUD, which stays as bright.
    void appendBackdrop(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size) const;
    /// While it shows: the menu, over the HUD.
    void append(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size);
    /// Over a map played: what was refused, for its two seconds, over the menu or over the game.
    void appendNotice(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size);

private:
    /// What the buy commands and the menu do to the game of the session played.
    [[nodiscard]] auto buyActions() -> cs::BuyActions;
    /// What the menu shows of the player of `session`.
    [[nodiscard]] static auto buyerViewOf(const Session& session) -> cs::BuyerView;
    /// The session played, if the menu may show over it. Errors: NoMap, NoMenu.
    [[nodiscard]] auto showable() const -> nx::Result<Session*, cs::BuyRefusal>;
    /// Tells the player on the screen of what was refused.
    void tellRefusal(cs::BuyRefusal refusal);
    [[nodiscard]] auto now() const -> nx::Seconds;

    PlayedSession played;
    const nx::platform::Stopwatch* mapClock;
    cs::BuyMenuContent content;
    cs::BuyMenu menu;
    std::optional<cs::BuyMenuPanel> panel;
};

} // namespace app
