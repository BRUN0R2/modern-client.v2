// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.math;
import nx.game;
import cs.shared;

namespace cs {

GameState::GameState(const GameContent& gameContent, const GameMap& gameMap, GameCvars gameCvars,
                     GameModels gameModels)
    : content{gameContent}, map{gameMap}, cvars{std::move(gameCvars)}, models{std::move(gameModels)},
      // The first round starts with the map, the first moment of the game's clock.
      rounds{Rounds::start(gameMap.objectives(), cvars.roundLengths(), nx::GameTime{})},
      accounts{gameContent.economy}, lightStyles{mapLightStyles(gameMap.map(), gameContent.lightStyles)} {}

void TickEvents::clear() noexcept {
    impacts.clear();
    entitySteps.clear();
    bloodMarks.clear();
    bloodSprays.clear();
    bloodStreams.clear();
    streakSplashes.clear();
    killNotices.clear();
}

auto playerAt(GameState& game, PlayerIndex index) -> Player& {
    nx::expects(index.value < game.players.size() && game.players[index.value], "Game: a player of the game");
    return *game.players[index.value];
}

auto playerAt(const GameState& game, PlayerIndex index) -> const Player& {
    nx::expects(index.value < game.players.size() && game.players[index.value], "Game: a player of the game");
    return *game.players[index.value];
}

auto teamCountsOf(const GameState& game) -> TeamCounts {
    TeamCounts counts{};
    for (const Player& player : playersIn(game.players)) {
        TeamCount& count = counts[player.team];
        if (player.status.isAlive()) {
            ++count.alive;
        } else {
            ++count.dead;
        }
    }
    return counts;
}

auto eyeOf(const MovementState& player) -> nx::Vec3 {
    return player.origin + nx::Vec3{.z = player.viewHeight.value()};
}

auto feetOf(const MovementState& player) -> nx::Vec3 {
    const nx::Aabb& hull = player.isDucked ? kDuckedBox : kStandingBox;
    return player.origin + nx::Vec3{.z = hull.min.z};
}

void makeSound(const GameState& game, Player& player, const GameSound& sound) {
    player.sounds.push_back(
        SoundMade{.sound = &sound, .at = nx::game::tickTimeOf(game.world).now, .volume = sound.volume});
}

} // namespace cs
