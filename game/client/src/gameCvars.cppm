// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The cvars of the game's rules (docs/12-ui-hud-and-console.md, section 5.2), by the original's
/// names, which a server sets and tells its players of, as the local game plays them until its
/// server comes (cs.server): their values, their ranges and when the game reads them are
/// ReGameDLL_CS's (game.cpp, ReadMultiplayCvars, CheckStartMoney and CheckRestartRound, GPL,
/// consulted: docs/20 section 3).
export module cs.client:gameCvars;

import std;
import nx.core;
import nx.console;
import cs.shared;

export namespace cs {

/// The cvars of the rounds and of the money a game starts with: `mp_freezetime` (6 seconds, 0 to 60),
/// `mp_roundtime` (5 minutes, 1 to 9), `mp_startmoney` (800, 800 to 16000), and `sv_restartround` and
/// `sv_restart` (0: the seconds to restart the game in, up to 60).
struct GameCvars {
    nx::console::Cvar<float> freezeTime;
    nx::console::Cvar<float> roundTime;
    nx::console::Cvar<std::int32_t> startMoney;
    nx::console::Cvar<std::int32_t> restartRound; ///< `sv_restartround`, read before sv_restart.
    nx::console::Cvar<std::int32_t> restart;      ///< `sv_restart`.

    /// Registers them in `console`. Errors: those of Console::registerCvar.
    [[nodiscard]] static auto registerIn(nx::console::Console& console)
        -> nx::Result<GameCvars, nx::console::CvarError>;

    /// The lengths of a round that starts now (roundLengthsOf).
    [[nodiscard]] auto roundLengths() const -> RoundLengths;
    /// The money a player starts the game with.
    [[nodiscard]] auto startingMoney() const -> nx::Money;
    /// The restart of the game they ask for, as the game reads them once a second: sv_restartround's
    /// seconds, else sv_restart's, both set back to 0 once read; none while both are 0.
    [[nodiscard]] auto takeRestart() const -> std::optional<nx::Seconds>;
};

} // namespace cs
