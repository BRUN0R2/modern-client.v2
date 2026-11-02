// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.game;
import cs.shared;

namespace cs {

namespace {

/// How often the rules look at the restart the cvars ask for (Think's periodic part).
constexpr nx::Seconds kPeriodicThinkInterval{1.0F};

} // namespace

void roundEnded(GameState& game, RoundEndReason reason) {
    nx::log::info(kGameLog, "the round is over: {}", nx::enumName(reason));
    game.accounts.roundEnded(reason, game.map.objectives());
    const std::optional<Team> winner = winnerOf(reason);
    if (winner) {
        ++game.wins[*winner];
    }
    const bool paysLivingLosers = game.content.economy.roundEnd(reason).paysLivingLosers;
    for (Player& player : playersIn(game.players)) {
        if (winner && *winner != player.team && player.status.isAlive() && !paysLivingLosers) {
            player.isPaidNextRound = false;
        }
    }
}

void payForRound(GameState& game, std::uint32_t hostagesHeld) {
    const nx::EnumArray<Team, nx::Money> paid =
        game.accounts.roundStarts(game.map.objectives(), hostagesHeld);
    for (Player& player : playersIn(game.players)) {
        if (player.isPaidNextRound) {
            player.status.money = addMoney(player.status.money, paid[player.team]);
        }
    }
}

void checkWinConditions(GameState& game) {
    const TeamCounts counts = teamCountsOf(game);
    const nx::game::TickTime& time = nx::game::tickTimeOf(game.world);
    if (const std::optional<RoundEndReason> reason =
            game.rounds.checkWinConditions(counts, time.now, time.rate)) {
        roundEnded(game, *reason);
    }
}

void thinkRules(GameState& game) {
    const nx::game::TickTime& time = nx::game::tickTimeOf(game.world);
    const RoundEvents events = game.rounds.think(time.now, time.rate, game.cvars.roundLengths());
    if (events.test(RoundEvent::FreezeEnded)) {
        nx::log::info(kGameLog, "the round's freeze is over");
        for (Player& player : playersIn(game.players)) {
            player.roundTimer = game.rounds.timerAt(time.now, time.rate);
            player.movement.maxSpeed = maxSpeed(game, player);
        }
    }
    if (const std::optional<RoundEndReason> reason = game.rounds.end();
        events.test(RoundEvent::Ended) && reason) {
        roundEnded(game, *reason);
    }
    std::uint32_t hostagesHeld = 0;
    if (events.test(RoundEvent::RoundStarted) || events.test(RoundEvent::GameRestarted)) {
        // RestartRound puts the hostages back as it counts what they bring, before it pays.
        hostagesHeld = repositionHostages(game.world, game.map.hulls(), game.content.economy.hostageBonus);
    }
    if (events.test(RoundEvent::RoundStarted)) {
        nx::log::info(kGameLog, "a round starts");
        payForRound(game, hostagesHeld);
    }
    if (events.test(RoundEvent::GameRestarted)) {
        nx::log::info(kGameLog, "the game restarts");
        game.accounts.restartGame();
        game.wins = {}; // m_bCompleteReset
        for (Player& player : playersIn(game.players)) {
            player.score = {}; // CBasePlayer::Reset
            player.status = PlayerStatus{.money = game.cvars.startingMoney()};
            player.inventory.removeAll();
            equip(game, player);
        }
    }
    if (events.test(RoundEvent::RoundStarted) || events.test(RoundEvent::GameRestarted)) {
        // RestartRound counts the spawns of the round anew before the players spawn again.
        for (Player& player : playersIn(game.players)) {
            player.spawnsThisRound = 0;
        }
        for (Player& player : playersIn(game.players)) {
            spawn(game, player);
        }
        // After the players spawn, as the original's RestartRound cleans the map up (CleanUpMap).
        nx::game::restartBrushEntities(game.world, game.map.hulls());
    }
    // Last, as the original's Think reads them once a second after the round's checks.
    if (time.now > game.nextPeriodicThink) {
        if (const std::optional<nx::Seconds> delay = game.cvars.takeRestart()) {
            nx::log::info(kGameLog, "the game restarts in {}", *delay);
            game.rounds.restartGameIn(*delay, time.now, time.rate);
        }
        game.nextPeriodicThink = time.now.after(kPeriodicThinkInterval, time.rate);
    }
}

} // namespace cs
