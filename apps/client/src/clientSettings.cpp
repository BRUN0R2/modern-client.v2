// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.core;
import nx.console;
import nx.settings;
import cs.shared;
import cs.server;
import cs.client;
import cs.bots;

namespace app {

namespace {

/// The key `next` binds `noclip` to, which Counter-Strike leaves free.
constexpr std::string_view kNoclipKey = "v";

/// What `registered` registered, or why the client's settings are unavailable.
template <class Value, nx::ErrorCode Code>
[[nodiscard]] auto settingOf(nx::Result<Value, Code> registered) -> nx::Result<Value, ClientError> {
    if (!registered) {
        return nx::fail(ClientError::SettingsUnavailable, std::move(registered.error()));
    }
    return std::move(*registered);
}

} // namespace

auto registerClientSettings(nx::console::Console& console, nx::settings::KeyBindings& bindings)
    -> nx::Result<ClientSettings, ClientError> {
    // In the order their cvars are listed and written to config.cfg.
    auto client = settingOf(nx::settings::registerClientCvars(console));
    if (!client) {
        return std::unexpected{std::move(client.error())};
    }
    auto input = settingOf(cs::PlayerInput::registerIn(console));
    if (!input) {
        return std::unexpected{std::move(input.error())};
    }
    auto hud = settingOf(cs::HudCvars::registerIn(console));
    if (!hud) {
        return std::unexpected{std::move(hud.error())};
    }
    auto fog = settingOf(cs::FogCvars::registerIn(console));
    if (!fog) {
        return std::unexpected{std::move(fog.error())};
    }
    auto corpses = settingOf(cs::CorpseCvars::registerIn(console));
    if (!corpses) {
        return std::unexpected{std::move(corpses.error())};
    }
    auto damageNumbers = settingOf(cs::DamageNumberCvars::registerIn(console));
    if (!damageNumbers) {
        return std::unexpected{std::move(damageNumbers.error())};
    }
    auto stepIndicator = settingOf(cs::StepIndicatorCvars::registerIn(console));
    if (!stepIndicator) {
        return std::unexpected{std::move(stepIndicator.error())};
    }
    auto killFeed = settingOf(cs::KillFeedCvars::registerIn(console));
    if (!killFeed) {
        return std::unexpected{std::move(killFeed.error())};
    }
    auto scoreboard = settingOf(cs::ScoreboardControls::registerIn(console));
    if (!scoreboard) {
        return std::unexpected{std::move(scoreboard.error())};
    }
    auto game = settingOf(cs::GameCvars::registerIn(console));
    if (!game) {
        return std::unexpected{std::move(game.error())};
    }
    auto bots = settingOf(cs::BotCvars::registerIn(console));
    if (!bots) {
        return std::unexpected{std::move(bots.error())};
    }
    // Counter-Strike's binds, and noclip on a key it leaves free, until config.cfg says otherwise.
    const std::optional<nx::console::KeyId> noclipKey = nx::settings::keyNamed(kNoclipKey);
    nx::invariant(noclipKey.has_value(), "registerClientSettings: the noclip key has a name");
    bindings.bind(*noclipKey, "noclip");
    if (auto registered = nx::settings::registerBindCommands(console, bindings); !registered) {
        return nx::fail(ClientError::SettingsUnavailable, std::move(registered.error()));
    }
    return ClientSettings{.client = std::move(*client),
                          .input = std::move(*input),
                          .hud = std::move(*hud),
                          .fog = std::move(*fog),
                          .corpses = std::move(*corpses),
                          .damageNumbers = std::move(*damageNumbers),
                          .stepIndicator = std::move(*stepIndicator),
                          .killFeed = std::move(*killFeed),
                          .scoreboard = std::move(*scoreboard),
                          .game = std::move(*game),
                          .bots = std::move(*bots)};
}

} // namespace app
