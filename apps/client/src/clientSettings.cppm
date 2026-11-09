// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the client registers in its console (docs/12-ui-hud-and-console.md, section 5.2): the player's
/// cvars, buttons and binds, and the cvars of the game's rules and of its bots, which the player's files,
/// the command line and the console set.
export module app.client:settings;

import std;
import nx.core;
import nx.console;
import nx.settings;
import cs.shared;
import cs.server;
import cs.client;
import cs.bots;
import :client;

export namespace app {

/// The client's cvars and buttons, registered in its console, each read where its consumer reads it.
struct ClientSettings {
    nx::settings::ClientCvars client;     ///< Video, audio, mouse, keyboard and the player's.
    cs::PlayerInput input;                ///< The `+` buttons the binds hold, and the moves they make.
    cs::HudCvars hud;                     ///< The crosshair and the hand the weapons are held in.
    cs::FogCvars fog;                     ///< `fog_*`.
    cs::CorpseCvars corpses;              ///< `cl_corpsestay`.
    cs::DamageNumberCvars damageNumbers;  ///< `hud_damagenumbers`.
    cs::StepIndicatorCvars stepIndicator; ///< `hud_stepindicator`.
    cs::KillFeedCvars killFeed;           ///< `hud_deathnotice_time`.
    cs::ScoreboardControls scoreboard;    ///< `+showscores` and `hostname`.
    cs::GameCvars game;                   ///< The rules the local game plays by until a server comes.
    cs::BotCvars bots;                    ///< `bot_*`.
};

/// Registers the client's cvars and buttons in `console`, and Counter-Strike's binds in `bindings`, with
/// `noclip` on a key it leaves free, and the commands that change them (`bind`, `unbind`...), until the
/// player's files say otherwise. Errors: SettingsUnavailable, with the cause.
[[nodiscard]] auto registerClientSettings(nx::console::Console& console, nx::settings::KeyBindings& bindings)
    -> nx::Result<ClientSettings, ClientError>;

} // namespace app
