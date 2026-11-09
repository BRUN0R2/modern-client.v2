// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.core;
import nx.console;
import nx.rhi;
import nx.render;
import nx.ui;
import cs.shared;
import cs.client;

namespace app {

namespace {

constexpr nx::log::Category kLog{"client"};

} // namespace

void ClientHud::open(const cs::WindowFonts& fonts, nx::ui::FontCatalog& catalog,
                     const cs::GameContent& content, std::uint32_t screenHeight) {
    if (auto label = cs::FpsLabel::open(fonts, screenHeight); label) {
        fpsLabel.emplace(std::move(*label));
    } else {
        nx::log::warning(kLog, "cl_showfps shows nothing: {}", nx::describe(label.error()));
    }
    if (auto numbers = cs::DamageNumbers::open(catalog, content.damageNumbers, screenHeight); numbers) {
        damageNumbers.emplace(std::move(*numbers));
    } else {
        nx::log::warning(kLog, "the damage of the hits shows no numbers: {}", nx::describe(numbers.error()));
    }
    stepIndicator.emplace(content.stepIndicator);
    if (auto feed = cs::KillFeed::open(fonts, screenHeight); feed) {
        killFeed.emplace(std::move(*feed));
    } else {
        nx::log::warning(kLog, "the kill feed shows nothing: {}", nx::describe(feed.error()));
    }
    if (auto board = cs::Scoreboard::open(fonts, screenHeight); board) {
        scoreboard.emplace(std::move(*board));
    } else {
        nx::log::warning(kLog, "the scoreboard shows nothing: {}", nx::describe(board.error()));
    }
}

auto ClientHud::load(const cs::GameContent& content, nx::render::ScreenRenderer& screen,
                     nx::rhi::CommandList& list) -> nx::Result<void, ClientError> {
    if (auto loaded = cs::Hud::load(content.vfs, content.weapons.ammo(), screen, list); loaded) {
        hud.emplace(std::move(*loaded));
    } else {
        nx::log::warning(kLog, "the HUD is not drawn: {}", nx::describe(loaded.error())); // the game plays on
    }
    if (fpsLabel) {
        if (auto loaded = fpsLabel->load(screen, list); !loaded) {
            return nx::fail(ClientError::GpuFailed, std::move(loaded.error()));
        }
    }
    if (damageNumbers) {
        if (auto loaded = damageNumbers->load(screen, list); !loaded) {
            return nx::fail(ClientError::GpuFailed, std::move(loaded.error()));
        }
    }
    if (stepIndicator) {
        if (auto loaded = stepIndicator->load(screen, list); !loaded) {
            return nx::fail(ClientError::GpuFailed, std::move(loaded.error()));
        }
    }
    if (killFeed) {
        if (auto loaded = killFeed->load(content.vfs, screen, list); !loaded) {
            nx::log::warning(kLog, "the kill feed shows nothing: {}", nx::describe(loaded.error()));
            killFeed.reset(); // the game plays on, as without the HUD
        }
    }
    if (scoreboard) {
        if (auto loaded = scoreboard->load(screen, list); !loaded) {
            return nx::fail(ClientError::GpuFailed, std::move(loaded.error()));
        }
    }
    return {};
}

void ClientHud::clear() {
    if (damageNumbers) {
        damageNumbers->clear(); // over the old game's bodies
    }
    if (stepIndicator) {
        stepIndicator->clear(); // the old game's bodies
    }
    if (killFeed) {
        killFeed->clear(); // the old game's deaths
    }
}

auto ClientHud::notices(nx::console::Console& console, const ClientSettings& settings) -> FrameNotices {
    return {.console = console,
            .killFeed = killFeed,
            .damageNumbers = damageNumbers,
            .damageNumberCvars = settings.damageNumbers,
            .stepIndicator = stepIndicator,
            .stepIndicatorCvars = settings.stepIndicator};
}

void ClientHud::appendUnderMenu(std::vector<nx::render::ScreenQuad>& quads, const HudFrame& frame) {
    if (!frame.settings.client.video.hudDraw.value()) {
        return;
    }
    if (damageNumbers && frame.size.height > 0) {
        // Over the scene, walls and all, under the HUD; the view the scene is drawn from.
        const float aspect = static_cast<float>(frame.size.width) / static_cast<float>(frame.size.height);
        damageNumbers->append(quads, nx::render::viewProjectionMatrix(frame.view, aspect), frame.size,
                              frame.now);
    }
    if (stepIndicator) {
        // About the middle of the screen, under the HUD, the way each body heard is from the view.
        stepIndicator->append(quads, frame.view.position, frame.view.angles.yaw, frame.size, frame.now);
    }
}

void ClientHud::appendHud(std::vector<nx::render::ScreenQuad>& quads, const HudFrame& frame,
                          bool isMenuShown) {
    if (!frame.settings.client.video.hudDraw.value()) {
        return;
    }
    const cs::ClientGame& client = frame.session.client;
    if (hud) {
        // The HUD, over the scene, the knife's crosshair in the middle but under the buy menu.
        const cs::PlayerStatus& status = client.status();
        hud->append(
            quads,
            {.health = status.health,
             .armor = status.armor,
             .armorKind = status.armorKind,
             .money = status.money,
             .ammo = cs::hudAmmoOf(client.inventory(), frame.content.weapons),
             .crosshairDistance = isMenuShown ? std::nullopt : std::optional{cs::kKnifeCrosshairDistance},
             .roundTime = client.roundTimeShown()},
            frame.settings.hud.crosshair(), frame.size);
    }
    if (killFeed) {
        killFeed->append(quads, frame.size, frame.now, frame.settings.killFeed.shownFor(),
                         !client.status().isAlive());
    }
}

void ClientHud::appendPanels(std::vector<nx::render::ScreenQuad>& quads, const HudFrame& frame,
                             std::optional<double> framesPerSecond) {
    const cs::ClientGame& client = frame.session.client;
    if (scoreboard && frame.settings.scoreboard.shown.isDown()) {
        // One of the client's panels, over the HUD whether it is drawn or not.
        const std::string hostname = frame.settings.scoreboard.hostname.value();
        scoreboard->append(
            quads,
            {.hostname = hostname, .rows = client.scores(), .self = client.self(), .wins = client.teamWins()},
            frame.size);
    }
    if (fpsLabel && framesPerSecond && frame.settings.client.video.showFps.value()) {
        fpsLabel->append(quads, *framesPerSecond);
    }
}

} // namespace app
