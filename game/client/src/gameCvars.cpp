// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.console;
import cs.shared;

namespace cs {

namespace {

// The original's values and ranges: a server's cvars, told to its players.
constexpr nx::console::CvarFlags kServerCvar{nx::console::CvarFlag::Notify};
constexpr float kDefaultFreezeSeconds = 6.0F;
constexpr float kMostFreezeSeconds = 60.0F;
constexpr float kDefaultRoundMinutes = 5.0F;
constexpr float kLeastRoundMinutes = 1.0F;
constexpr float kMostRoundMinutes = 9.0F;
constexpr std::int32_t kMostRestartSeconds = 60;

} // namespace

auto GameCvars::registerIn(nx::console::Console& console) -> nx::Result<GameCvars, nx::console::CvarError> {
    using nx::console::CvarDesc;
    auto freezeTime = console.registerCvar(
        CvarDesc<float>{.name = "mp_freezetime",
                        .defaultValue = kDefaultFreezeSeconds,
                        .description = "The seconds each round starts frozen for, its players still.",
                        .flags = kServerCvar,
                        .minimum = 0.0F,
                        .maximum = kMostFreezeSeconds});
    auto roundTime =
        console.registerCvar(CvarDesc<float>{.name = "mp_roundtime",
                                             .defaultValue = kDefaultRoundMinutes,
                                             .description = "The minutes a round runs for after its freeze.",
                                             .flags = kServerCvar,
                                             .minimum = kLeastRoundMinutes,
                                             .maximum = kMostRoundMinutes});
    auto startMoney = console.registerCvar(
        CvarDesc<std::int32_t>{.name = "mp_startmoney",
                               .defaultValue = kStartMoney.value(),
                               .description = "The money each player starts the game with.",
                               .flags = kServerCvar,
                               .minimum = kStartMoney.value(), // the original's least is its default
                               .maximum = kMostMoney.value()});
    auto restartRound = console.registerCvar(CvarDesc<std::int32_t>{
        .name = "sv_restartround",
        .defaultValue = 0,
        .description = "Restarts the game in this many seconds; read before sv_restart.",
        .flags = kServerCvar,
        .minimum = 0,
        .maximum = kMostRestartSeconds});
    auto restart =
        console.registerCvar(CvarDesc<std::int32_t>{.name = "sv_restart",
                                                    .defaultValue = 0,
                                                    .description = "Restarts the game in this many seconds.",
                                                    .flags = kServerCvar,
                                                    .minimum = 0,
                                                    .maximum = kMostRestartSeconds});
    if (!freezeTime || !roundTime) {
        return std::unexpected{std::move(!freezeTime ? freezeTime.error() : roundTime.error())};
    }
    for (auto* const registered : {&startMoney, &restartRound, &restart}) {
        if (!*registered) {
            return std::unexpected{std::move(registered->error())};
        }
    }
    return GameCvars{.freezeTime = *freezeTime,
                     .roundTime = *roundTime,
                     .startMoney = *startMoney,
                     .restartRound = *restartRound,
                     .restart = *restart};
}

auto GameCvars::roundLengths() const -> RoundLengths {
    return roundLengthsOf(freezeTime.value(), roundTime.value());
}

auto GameCvars::startingMoney() const -> nx::Money {
    return nx::Money{startMoney.value()};
}

auto GameCvars::takeRestart() const -> std::optional<nx::Seconds> {
    const std::int32_t seconds = restartRound.value() != 0 ? restartRound.value() : restart.value();
    if (seconds == 0) {
        return std::nullopt;
    }
    for (const nx::console::Cvar<std::int32_t>& asked : {restartRound, restart}) {
        // 0 is within their range, and they are neither read-only nor cheats: the set holds.
        std::ignore = asked.set(0);
    }
    return nx::Seconds{static_cast<float>(seconds)};
}

} // namespace cs
