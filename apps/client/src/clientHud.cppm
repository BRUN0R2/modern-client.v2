// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the client draws over the scene of the map played (docs/12-ui-hud-and-console.md, section 3).
export module app.client:hud;

import std;
import nx.core;
import nx.console;
import nx.rhi;
import nx.render;
import nx.ui;
import cs.shared;
import cs.client;
import :client;
import :session;
import :sessionFrame;
import :settings;

namespace app {

/// What the HUD shows of a frame of the map played.
struct HudFrame {
    const Session& session;
    const cs::GameContent& content;
    const ClientSettings& settings;
    nx::render::Camera view; ///< The player's, turned by the kick its weapon gave it.
    nx::Seconds now;         ///< Since the map started.
    nx::rhi::Extent2d size;  ///< Of the screen.
};

/// What the client draws over the scene of the map played: the HUD (health, armor, money, ammo, the
/// crosshair and the round's time), the damage numbers, the step indicator, the kill feed, the scoreboard
/// and cl_showfps's label. One it cannot have is logged, and the game plays on without it.
class ClientHud {
public:
    /// Opens what writes in the fonts of the installation's windows, `fonts`, and of `catalog`, for a screen
    /// `screenHeight` tall, in the looks of `content`.
    void open(const cs::WindowFonts& fonts, nx::ui::FontCatalog& catalog, const cs::GameContent& content,
              std::uint32_t screenHeight);
    /// Puts on the GPU in `list` with `screen` the HUD's pictures of `content`, and what opened. Errors:
    /// GpuFailed, with the cause.
    [[nodiscard]] auto load(const cs::GameContent& content, nx::render::ScreenRenderer& screen,
                            nx::rhi::CommandList& list) -> nx::Result<void, ClientError>;
    /// Forgets the bodies and the deaths of the map played before.
    void clear();
    /// What tells the player, with `console`, what happened in the frame played, as `settings` say.
    [[nodiscard]] auto notices(nx::console::Console& console, const ClientSettings& settings) -> FrameNotices;

    /// Under the buy menu and the HUD: the damage numbers over the bodies hit, walls and all, and the step
    /// indicator, while `hud_draw` draws.
    void appendUnderMenu(std::vector<nx::render::ScreenQuad>& quads, const HudFrame& frame);
    /// The HUD, its crosshair but while the buy menu `isMenuShown`, and the kill feed, while `hud_draw`
    /// draws.
    void appendHud(std::vector<nx::render::ScreenQuad>& quads, const HudFrame& frame, bool isMenuShown);
    /// Over the buy menu, whether `hud_draw` draws or not: the scoreboard while `+showscores` is held, and
    /// cl_showfps's label telling `framesPerSecond`, as the original's engine writes it over its client's
    /// HUD.
    void appendPanels(std::vector<nx::render::ScreenQuad>& quads, const HudFrame& frame,
                      std::optional<double> framesPerSecond);

private:
    std::optional<cs::Hud> hud;                     ///< None when its pictures could not be read.
    std::optional<cs::FpsLabel> fpsLabel;           ///< None when its font could not be had.
    std::optional<cs::DamageNumbers> damageNumbers; ///< None when its font could not be had.
    std::optional<cs::StepIndicator> stepIndicator; ///< Once the window's text is open.
    std::optional<cs::KillFeed> killFeed;           ///< None when its font or its pictures could not be had.
    std::optional<cs::Scoreboard> scoreboard;       ///< None when its font could not be had.
};

} // namespace app
