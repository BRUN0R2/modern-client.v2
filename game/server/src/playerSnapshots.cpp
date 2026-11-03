// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.ecs;
import nx.game;
import cs.shared;

namespace cs {

namespace {

/// Where the game has the brush entities of `world`, and which of their looks they show.
[[nodiscard]] auto brushEntityStatesOf(nx::ecs::World& world) -> std::vector<BrushEntityState> {
    std::vector<BrushEntityState> states;
    nx::ecs::Query<const nx::game::BrushModel&, const nx::game::Transform&,
                   nx::ecs::Optional<const nx::game::Waves>,
                   nx::ecs::Optional<const nx::game::AlternateTextures>>{world}
        .forEach([&states](const nx::game::BrushModel& model, const nx::game::Transform& transform,
                           const nx::game::Waves* waves, const nx::game::AlternateTextures* textures) {
            states.push_back({.model = model.index,
                              .origin = transform.origin,
                              .angles = transform.angles,
                              .waveHeight = waves != nullptr ? waves->height : nx::HammerUnits{},
                              .showsAlternateTextures = textures != nullptr && textures->isShown});
        });
    return states;
}

} // namespace

auto scoresFor(const GameState& game, const Player& viewer) -> std::vector<ScoreRow> {
    std::vector<ScoreRow> rows;
    rows.reserve(game.players.size());
    for (const auto& [slot, player] : std::views::enumerate(game.players)) {
        if (!player) {
            continue;
        }
        const bool isTeammate = player->team == viewer.team;
        rows.push_back(ScoreRow{
            .player = PlayerIndex{nx::narrow<std::uint32_t>(slot)},
            .name = player->name,
            .team = player->team,
            .score = player->score,
            .isDead = !player->status.isAlive(),
            .health = isTeammate ? std::optional{player->status.health} : std::nullopt,
            .money = isTeammate ? std::optional{player->status.money} : std::nullopt,
        });
    }
    return rows;
}

auto snapshotFor(GameState& game, PlayerIndex index) -> PlayerSnapshot {
    const Player& player = playerAt(game, index);
    std::vector<PlayerEntityState> others;
    for (std::uint32_t other = 0; other < game.players.size(); ++other) {
        if (other == index.value || !game.players[other]) {
            continue;
        }
        const Player& shown = *game.players[other];
        const OwnedWeapon* const held = shown.inventory.held();
        const Animation& torso = shown.animation.torso;
        others.push_back(
            PlayerEntityState{.player = PlayerIndex{other},
                              .character = shown.character,
                              .weapon = held != nullptr ? std::optional{held->weapon} : std::nullopt,
                              .origin = shown.movement.origin,
                              .angles = bodyAnglesOf(shown.view),
                              .sequence = torso.sequence,
                              .frame = torso.frame,
                              .advancedAt = torso.advancedAt,
                              .framerate = torso.framerate,
                              .gaitSequence = shown.animation.gaitSequence,
                              .isDead = shown.movement.isDead});
    }
    const nx::game::TickTime& time = nx::game::tickTimeOf(game.world);
    return PlayerSnapshot{.time = time.now,
                          .rate = time.rate,
                          .self = index,
                          .team = player.team,
                          .player = player.movement,
                          .status = player.status,
                          .buying = buyingStateOf(buyerOf(game, player), game.rounds.isFrozen()),
                          .lastPurchase = player.lastPurchase,
                          .inventory = player.inventory,
                          .roundTimer = player.roundTimer,
                          .turnedView = player.turnedView,
                          .wasPlaced = player.wasPlaced,
                          .sounds = player.sounds,
                          .impacts = game.events.impacts,
                          .bloodMarks = game.events.bloodMarks,
                          .bloodSprays = game.events.bloodSprays,
                          .bloodStreams = game.events.bloodStreams,
                          .streakSplashes = game.events.streakSplashes,
                          .damageDealt = player.damageDealt,
                          .entitySteps = game.events.entitySteps,
                          .killNotices = game.events.killNotices,
                          .scores = scoresFor(game, player),
                          .teamWins = game.wins,
                          .brushEntities = brushEntityStatesOf(game.world),
                          .hostages = hostageStatesOf(game.world),
                          .players = std::move(others),
                          .entities = entityStatesOf(game.world),
                          .lightStyles = game.lightStyles};
}

} // namespace cs
