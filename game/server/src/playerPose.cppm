// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How a player's body stands and its model plays, as the game's traces and its clients see it
/// (cs.shared:playerAnimation; CBasePlayer::SetAnimation, PostThink).
export module cs.server:playerPose;

import std;
import nx.core;
import cs.shared;
import :gameState;

namespace cs {

/// `player`'s body where it is: at its origin, its box among the bodies the pushers move, and, alive,
/// solid to the server's traces as its box, standing or ducked (setBodySolidity); dead, not solid.
void placeBody(GameState& game, Player& player);

/// `player`'s body posed for the server's traces as its model plays (PlayerPose): its torso's sequence
/// and frame, its legs' gait sequence and yaw, the aim's blends and its view's body angles.
void poseBody(GameState& game, Player& player);

/// `player`'s model plays `action` (setPlayerAnimation, CBasePlayer::SetAnimation), as the player
/// stands and moves, by the weapon it holds, drawing from its command's seed; nothing for a character
/// whose model could not be read.
void animate(GameState& game, Player& player, PlayerAction action);

/// What `player`'s model plays as it ends a command of `frameTime` (PostThink): alive, walking while it
/// moves on the ground or in water, else standing (but, as the original compares the gait sequence
/// with ACT_FLY's number, while its legs play the sixth), its torso's frame advanced; dead or alive,
/// its gait (processGait, StudioProcessGait) turned by its view.
void animateAfterCommand(GameState& game, Player& player, nx::Seconds frameTime);

} // namespace cs
