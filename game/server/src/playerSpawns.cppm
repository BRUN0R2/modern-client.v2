// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Where and with what a player spawns, as the original's CBasePlayer::Spawn, GiveDefaultItems and
/// EntSelectSpawnPoint make it, and the weapons it is given.
export module cs.server:playerSpawns;

import std;
import nx.core;
import cs.shared;
import :gameState;

namespace cs {

/// How fast `player` may run: kFreezeSpeed while the round is frozen, else as the weapon it holds
/// allows (ResetMaxSpeed).
[[nodiscard]] auto maxSpeed(const GameState& game, const Player& player) -> nx::UnitsPerSecond;

/// Stands `player` still at `origin`, its body there, and its client told it was placed.
void standAt(GameState& game, Player& player, nx::Vec3 origin);

/// The spawn point a player of `team` spawns at next, as EntSelectSpawnPoint picks it: the first of
/// the team's after the one its last player spawned at, the first after the last; the terrorists'
/// first when the team has none; the world's origin without either.
[[nodiscard]] auto nextSpawnPoint(GameState& game, Team team) -> SpawnPoint;

/// Gives `player` what a player of its team spawns with, drawn as its _cl_autowepswitch says.
void equip(GameState& game, Player& player);

/// Spawns `player` at the next spawn point of its team (nextSpawnPoint), as Spawn does: one unit
/// above it, standing still and turned the way it faces, its health whole, its speed no longer
/// slowed; alive, it keeps its armor, its weapons and its money; dead, it keeps its money alone,
/// given what a player of its team spawns with. It is paid as the next round starts, and has spawned
/// once more in the round. Its HUD is told the round's time.
void spawn(GameState& game, Player& player);

/// `player` watches until the next round, dead, from its team's next spawn point, turned the way it
/// faces (StartObserver at GetPlayerSpawnSpot).
void observe(GameState& game, Player& player);

/// Whether `player`, joining, spawns now (FPlayerCanRespawn): not when it spawned in the round
/// already, nor, the game having players of both teams, once the round started kJoinSpawnWindow ago.
[[nodiscard]] auto canSpawn(const GameState& game, const Player& player) -> bool;

/// Gives `player` `weapon` (its replacement, when it is replaced), which must be complete, as its loadout
/// gives one, drawn as its _cl_autowepswitch says; the speed it runs at follows the weapon it holds.
void giveWeapon(GameState& game, Player& player, WeaponIndex weapon);

} // namespace cs
