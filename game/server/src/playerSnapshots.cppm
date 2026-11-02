// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What each client is told after a tick (PlayerSnapshot; docs/01-architecture.md, section 2.3).
export module cs.server:playerSnapshots;

import std;
import nx.core;
import cs.shared;
import :gameState;

namespace cs {

/// The rows of the scoreboard `viewer`'s client is told of (ScoreRow): of every player in the game, by
/// its slot, the health and the money of `viewer`'s teammates and of itself alone.
[[nodiscard]] auto scoresFor(const GameState& game, const Player& viewer) -> std::vector<ScoreRow>;

/// What `player`'s client is told after the last tick (PlayerSnapshot).
[[nodiscard]] auto snapshotFor(GameState& game, PlayerIndex player) -> PlayerSnapshot;

} // namespace cs
