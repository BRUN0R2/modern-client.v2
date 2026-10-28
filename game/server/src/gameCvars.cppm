// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The cvars of the game's rules (docs/12-ui-hud-and-console.md, section 5.2), by the original's
/// names, which a server sets and tells its players of: numbers, any of them, as the original's are;
/// the game holds them as it reads them, where the original does, and sets them back as it does. Their
/// values and when and how the game reads them are ReGameDLL_CS's (game.cpp, ReadMultiplayCvars,
/// CheckStartMoney and CheckRestartRound, GPL, consulted: docs/20 section 3).
export module cs.server:gameCvars;

import std;
import nx.core;
import nx.console;
import cs.shared;

export namespace cs {

/// The cvars of the rounds, of the money a game starts with, of the players' steps and of their attacks
/// on their teammates: `mp_freezetime` (6 seconds), `mp_roundtime` (5 minutes), `mp_startmoney` (800),
/// `sv_restartround` and `sv_restart` (0: the seconds to restart the game in), `mp_footsteps` (1: the
/// players' steps sound, any value but 0), and `mp_friendlyfire` (0: a player's attacks hurt no
/// teammate, any value but 0 that they do).
struct GameCvars {
    nx::console::Cvar<float> freezeTime;
    nx::console::Cvar<float> roundTime;
    nx::console::Cvar<float> startMoney;
    nx::console::Cvar<float> restartRound; ///< `sv_restartround`, read before sv_restart.
    nx::console::Cvar<float> restart;      ///< `sv_restart`.
    nx::console::Cvar<float> footsteps;    ///< `mp_footsteps`, read at each step.
    nx::console::Cvar<float> friendlyFire; ///< `mp_friendlyfire`, read at each attack.

    /// Registers them in `console`. Errors: those of Console::registerCvar.
    [[nodiscard]] static auto registerIn(nx::console::Console& console)
        -> nx::Result<GameCvars, nx::console::CvarError>;

    /// The lengths of a round that starts now, mp_freezetime and mp_roundtime set back where they
    /// are held (readRoundCvars).
    [[nodiscard]] auto roundLengths() const -> RoundLengths;
    /// The money a player starts the game with, mp_startmoney set back where it is held
    /// (readStartMoney).
    [[nodiscard]] auto startingMoney() const -> nx::Money;
    /// The restart of the game they ask for, as the game reads them once a second (restartDelayOf),
    /// both set back to 0 when one is asked for.
    [[nodiscard]] auto takeRestart() const -> std::optional<nx::Seconds>;
    /// Whether the players' steps sound: mp_footsteps is not 0.
    [[nodiscard]] auto doStepsSound() const -> bool;
    /// Whether a player's attacks hurt its teammates: mp_friendlyfire is not 0.
    [[nodiscard]] auto isFriendlyFireOn() const -> bool;
};

} // namespace cs
