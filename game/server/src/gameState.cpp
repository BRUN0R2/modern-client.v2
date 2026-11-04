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

auto nextEntityStates(EntityStateLists& lists) -> std::vector<EntityState>& {
    for (std::size_t step = 1; step <= EntityStateLists::kKept; ++step) {
        const std::size_t at = (lists.newest + step) % EntityStateLists::kKept;
        std::shared_ptr<std::vector<EntityState>>& list = lists.lists[at];
        if (list == nullptr || list.use_count() == 1) {
            if (list == nullptr) {
                list = std::make_shared<std::vector<EntityState>>();
            }
            lists.newest = at;
            return *list;
        }
    }
    // All held, by more than a client that holds two: it goes on to the next, which they keep.
    lists.newest = (lists.newest + 1) % EntityStateLists::kKept;
    lists.lists[lists.newest] = std::make_shared<std::vector<EntityState>>();
    return *lists.lists[lists.newest];
}

void refreshEntityStates(GameState& game, nx::jobs::JobSystem* jobs) {
    writeEntityStates(game.world, nextEntityStates(game.entityStates), jobs);
    game.entityStates.isStale = false;
}

auto entityStatesNow(GameState& game) -> EntityStates {
    if (game.entityStates.isStale) {
        refreshEntityStates(game, nullptr);
    }
    return EntityStates{
        std::shared_ptr<const std::vector<EntityState>>{game.entityStates.lists[game.entityStates.newest]}};
}

auto solidsNow(GameState& game) -> nx::game::Solids& {
    MoveLists& moves = *game.moves;
    if (moves.areBrushesStale) {
        linkedBrushEntitiesOf(moves.brushQuery, game.map.hulls(), moves.brushes);
        moves.solids.brushes = nx::game::solidBrushesOf(game.world, game.map.hulls());
        moves.areBrushesStale = false;
    }
    if (moves.areBoxesStale) {
        moves.solids.boxes.gather(game.world, moves.jobs);
        moves.areBoxesStale = false;
    }
    return moves.solids;
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
