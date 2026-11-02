// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.math;
import nx.game;
import cs.shared;

namespace cs {

namespace {

/// The stream of random numbers a spawn's weapons draw from.
constexpr std::string_view kSpawnStream = "spawn";
/// How far above its spawn point a player spawns (GetPlayerSpawnSpot).
constexpr nx::HammerUnits kSpawnLift{1.0F};

} // namespace

auto maxSpeed(const GameState& game, const Player& player) -> nx::UnitsPerSecond {
    return game.rounds.isFrozen() ? kFreezeSpeed : player.inventory.maxSpeed();
}

void standAt(GameState& game, Player& player, nx::Vec3 origin) {
    player.movement = MovementState{.origin = origin};
    player.movement.maxSpeed = maxSpeed(game, player);
    placeBody(game, player);
    player.wasPlaced = true;
}

auto nextSpawnPoint(GameState& game, Team team) -> SpawnPoint {
    const std::span<const SpawnPoint> points = game.map.spawnPoints(team);
    if (points.empty()) {
        const std::span<const SpawnPoint> terrorists = game.map.spawnPoints(Team::Terrorists);
        return terrorists.empty() ? SpawnPoint{} : terrorists.front();
    }
    std::optional<std::size_t>& last = game.lastSpawnPoints[team];
    last = last ? (*last + 1) % points.size() : 0;
    return points[*last];
}

void equip(GameState& game, Player& player) {
    const Loadout* const loadout = game.content.loadouts.find(player.team);
    if (loadout == nullptr) {
        return;
    }
    const nx::game::TickTime& time = nx::game::tickTimeOf(game.world);
    nx::Random random{std::bit_cast<std::uint64_t>(time.now.tick()), kSpawnStream};
    for (const std::string& weapon : loadout->weapons) {
        // readLoadouts checked that the catalog has each.
        std::ignore =
            player.inventory.give(*game.content.weapons.indexOf(weapon), player.autoSwitch, time, random);
    }
    for (const LoadoutAmmo& ammo : loadout->ammo) {
        std::ignore = player.inventory.giveAmmo(ammo.id, ammo.rounds);
    }
}

void spawn(GameState& game, Player& player) {
    if (!player.status.isAlive()) {
        // Killed, it spawns without armor (Spawn), given what a player of its team spawns with
        // (GiveDefaultItems), as its weapons went with it.
        player.status.armor = nx::ArmorPoints{};
        player.status.armorKind = ArmorKind::None;
        equip(game, player);
    }
    const SpawnPoint point = nextSpawnPoint(game, player.team);
    player.status.health = kSpawnHealth;
    standAt(game, player, point.origin + nx::Vec3{.z = kSpawnLift.value()});
    player.speedShare = 1.0F;
    player.isPaidNextRound = true;
    ++player.spawnsThisRound;
    player.inventory.spawned();
    player.turnedView = nx::EulerAngles{.yaw = point.yaw};
    player.view = *player.turnedView;
    spawnAnimation(player.animation);
    poseBody(game, player);
    const nx::game::TickTime& time = nx::game::tickTimeOf(game.world);
    player.breath.fill(time);
    player.timedHarms.spawned();
    player.roundTimer = game.rounds.timerAt(time.now, time.rate);
    player.spawnedAt = time.now;
}

void observe(GameState& game, Player& player) {
    const SpawnPoint point = nextSpawnPoint(game, player.team);
    standAt(game, player, point.origin + nx::Vec3{.z = kSpawnLift.value()});
    player.movement.isDead = true;
    placeBody(game, player);
    player.turnedView = nx::EulerAngles{.yaw = point.yaw};
    player.view = *player.turnedView;
}

auto canSpawn(const GameState& game, const Player& player) -> bool {
    if (player.spawnsThisRound > 0) {
        return false;
    }
    const auto hasPlayers = [&game](Team team) {
        return std::ranges::contains(playersIn(game.players), team, &Player::team);
    };
    const nx::game::TickTime& time = nx::game::tickTimeOf(game.world);
    return !hasPlayers(Team::Terrorists) || !hasPlayers(Team::CounterTerrorists) ||
           game.rounds.sinceStartAt(time.now, time.rate) <= kJoinSpawnWindow;
}

void giveWeapon(GameState& game, Player& player, WeaponIndex weapon) {
    const nx::game::TickTime& time = nx::game::tickTimeOf(game.world);
    nx::Random random{std::bit_cast<std::uint64_t>(time.now.tick()), kSpawnStream};
    std::ignore = player.inventory.give(weapon, player.autoSwitch, time, random);
    player.movement.maxSpeed = maxSpeed(game, player);
}

} // namespace cs
