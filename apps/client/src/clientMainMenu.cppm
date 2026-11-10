// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The main menu (docs/12-ui-hud-and-console.md, section 4), as the client runs it.
export module app.client:mainMenu;

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

namespace app {

/// The main menu: what it shows (cs::MainMenu), what draws it (cs::MainMenuPanel), and what its items do,
/// by the console's commands as the original's menu does.
class ClientMainMenu {
public:
    /// The menu of what the main menu of `content` lists, a new game played on one of its maps, written in
    /// `texts`; both must outlive it.
    ClientMainMenu(const cs::GameContent& content, const cs::MainMenuTexts& texts);

    [[nodiscard]] auto isOpen() const noexcept -> bool;
    /// Opens over the map played, or over none (`game`).
    void open(cs::MenuGame game);
    void close() noexcept;

    /// What `event` does to the menu, open, while `game`, the mouse at `mouse` on a screen of `size`: the
    /// item it chooses goes back to the map played, or queues its command in `console` (`disconnect`,
    /// `quit`, `map <name>` for a new game on the map chosen), which runs it in its turn.
    void handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition mouse,
                nx::rhi::Extent2d size, cs::MenuGame game, nx::console::Console& console);

    /// Puts what draws it on the GPU in `list` with `screen`, its faces from `fonts`, for a screen of `size`,
    /// when they are not there for that height, and its background the first time. Errors: ContentUnavailable
    /// (the project's fonts cannot be had), GpuFailed, with the cause.
    [[nodiscard]] auto load(nx::ui::FontCatalog& fonts, nx::render::ScreenRenderer& screen,
                            nx::rhi::CommandList& list, nx::rhi::Extent2d size)
        -> nx::Result<void, ClientError>;

    /// While it is open: the menu over the frame, of the `size` it was loaded for, while `game`.
    void append(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size, cs::MenuGame game);

    /// What tells over the frame, of the `size` it was loaded for, that the map `map` is loading.
    void appendLoading(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size,
                       std::string_view map);

private:
    const nx::assets::Vfs* files; ///< The game's, which the picture behind the menu is read from.
    cs::MainMenu menu;
    cs::MainMenuPanel panel;
};

} // namespace app
