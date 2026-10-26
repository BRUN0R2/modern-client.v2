// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.console;
import cs.shared;

namespace cs {

namespace {

// The original's values: a server's cvars, told to its players.
constexpr nx::console::CvarFlags kServerCvar{nx::console::CvarFlag::Notify};
constexpr float kDefaultFreezeSeconds = 6.0F;
constexpr float kDefaultRoundMinutes = 5.0F;

} // namespace

auto GameCvars::registerIn(nx::console::Console& console) -> nx::Result<GameCvars, nx::console::CvarError> {
    using nx::console::CvarDesc;
    const auto server = [&console](std::string_view name, float defaultValue, std::string_view description) {
        return console.registerCvar(CvarDesc<float>{
            .name = name, .defaultValue = defaultValue, .description = description, .flags = kServerCvar});
    };
    auto freezeTime = server("mp_freezetime", kDefaultFreezeSeconds,
                             "The seconds each round starts frozen for, its players still: 0 to 60.");
    auto roundTime = server("mp_roundtime", kDefaultRoundMinutes,
                            "The minutes a round runs for after its freeze: 1 to 9.");
    auto startMoney = server("mp_startmoney", static_cast<float>(kStartMoney.value()),
                             "The money each player starts the game with: 800 to 16000.");
    auto restartRound = server("sv_restartround", 0.0F,
                               "Restarts the game in this many seconds, up to 60; read before sv_restart.");
    auto restart = server("sv_restart", 0.0F, "Restarts the game in this many seconds, up to 60.");
    auto footsteps = server("mp_footsteps", 1.0F, "Whether the players' steps sound: any value but 0.");
    for (auto* const registered :
         {&freezeTime, &roundTime, &startMoney, &restartRound, &restart, &footsteps}) {
        if (!*registered) {
            return std::unexpected{std::move(registered->error())};
        }
    }
    return GameCvars{.freezeTime = *freezeTime,
                     .roundTime = *roundTime,
                     .startMoney = *startMoney,
                     .restartRound = *restartRound,
                     .restart = *restart,
                     .footsteps = *footsteps};
}

// The values set back are finite, and the cvars have no range and are neither read-only nor cheats:
// the sets hold.

auto GameCvars::roundLengths() const -> RoundLengths {
    const RoundCvars read = readRoundCvars(freezeTime.value(), roundTime.value());
    if (read.freezeSeconds) {
        std::ignore = freezeTime.set(*read.freezeSeconds);
    }
    if (read.roundMinutes) {
        std::ignore = roundTime.set(*read.roundMinutes);
    }
    return read.lengths;
}

auto GameCvars::startingMoney() const -> nx::Money {
    const StartMoney read = readStartMoney(startMoney.value());
    if (read.setTo) {
        std::ignore = startMoney.set(*read.setTo);
    }
    return read.money;
}

auto GameCvars::takeRestart() const -> std::optional<nx::Seconds> {
    const std::optional<nx::Seconds> delay = restartDelayOf(restartRound.value(), restart.value());
    if (delay) {
        for (const nx::console::Cvar<float>& asked : {restartRound, restart}) {
            std::ignore = asked.set(0.0F);
        }
    }
    return delay;
}

auto GameCvars::doStepsSound() const -> bool {
    return footsteps.value() != 0.0F;
}

} // namespace cs
