// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The rules of the rounds as the game plays them over its players (CHalfLifeMultiplay::Think,
/// TerminateRound, RestartRound, CheckWinConditions; docs/10-counter-strike-gameplay.md, section 3).
export module cs.server:gameRules;

import std;
import nx.core;
import cs.shared;
import :gameState;

namespace cs {

/// What the end of a round by `reason` does (TerminateRound), logged: its winners count a win
/// (TeamScore), what they get goes to their team's account, and a player alive on the losing team is not
/// paid as the next round starts when the end does not pay its living losers.
void roundEnded(GameState& game, RoundEndReason reason);

/// Pays each player what its team gets as a round starts (RestartRound), `hostagesHeld` still held
/// (repositionHostages), unless the last end left it unpaid.
void payForRound(GameState& game, std::uint32_t hostagesHeld);

/// Weighs the round's end by the players alive and dead of each team (CheckWinConditions,
/// Rounds::checkWinConditions), what an end does to the money then played (roundEnded).
void checkWinConditions(GameState& game);

/// The rules of the round at the tick (CHalfLifeMultiplay::Think): once a second the restart the
/// cvars ask for (GameCvars::takeRestart); then Rounds::think, a round that starts as long as the
/// cvars say. As the freeze ends each player's HUD is told the round's time and the players run
/// again; as a round starts the players spawn again (spawn); as the game restarts they spawn as they
/// joined: with mp_startmoney, without armor, and with their team's loadout alone.
void thinkRules(GameState& game);

} // namespace cs
