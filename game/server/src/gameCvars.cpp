// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.console;
import nx.assets;
import cs.shared;

namespace cs {

namespace {

// The original's values: a server's cvars, told to its players.
constexpr nx::console::CvarFlags kServerCvar{nx::console::CvarFlag::Notify};
constexpr nx::Seconds kDefaultFreezeTime{6.0F};
constexpr nx::Minutes kDefaultRoundTime{5.0F};
constexpr nx::Minutes kDefaultBuyTime{1.5F};
/// mp_limitteams' default, and the most it is held to.
constexpr float kDefaultTeamsLimit = 2.0F;
constexpr float kMostTeamsLimit = 20.0F;

/// Six hexadecimal digits in RRGGBB order, with no prefix.
[[nodiscard]] auto tracerColorOf(std::string_view text) noexcept -> std::optional<nx::assets::PaletteColor> {
    constexpr std::size_t kHexDigits = 6;
    constexpr int kHexBase = 16;
    constexpr std::uint32_t kChannelBits = std::numeric_limits<std::uint8_t>::digits;
    constexpr std::uint32_t kRedChannelShift = kChannelBits + kChannelBits;
    constexpr std::uint32_t kChannelMask = std::numeric_limits<std::uint8_t>::max();
    if (text.size() != kHexDigits) {
        return std::nullopt;
    }
    std::uint32_t rgb = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), rgb, kHexBase);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return nx::assets::PaletteColor{.red = static_cast<std::uint8_t>(rgb >> kRedChannelShift),
                                    .green = static_cast<std::uint8_t>((rgb >> kChannelBits) & kChannelMask),
                                    .blue = static_cast<std::uint8_t>(rgb & kChannelMask)};
}

} // namespace

auto GameCvars::registerIn(nx::console::Console& console) -> nx::Result<GameCvars, nx::console::CvarError> {
    using nx::console::CvarDesc;
    const auto server = [&console](std::string_view name, float defaultValue, std::string_view description) {
        return console.registerCvar(CvarDesc<float>{
            .name = name, .defaultValue = defaultValue, .description = description, .flags = kServerCvar});
    };
    auto freezeTime = server("mp_freezetime", kDefaultFreezeTime.count(),
                             "The seconds each round starts frozen for, its players still: 0 to 60.");
    auto roundTime = server("mp_roundtime", kDefaultRoundTime.count(),
                            "The minutes a round runs for after its freeze: 1 to 9.");
    auto startMoney = server("mp_startmoney", static_cast<float>(kStartMoney.value()),
                             "The money each player starts the game with: 800 to 16000.");
    auto restartRound = server("sv_restartround", 0.0F,
                               "Restarts the game in this many seconds, up to 60; read before sv_restart.");
    auto restart = server("sv_restart", 0.0F, "Restarts the game in this many seconds, up to 60.");
    auto footsteps = server("mp_footsteps", 1.0F, "Whether the players' steps sound: any value but 0.");
    auto friendlyFire =
        server("mp_friendlyfire", 0.0F, "Whether a player's attacks hurt its teammates: any value but 0.");
    auto buyTime = server("mp_buytime", kDefaultBuyTime.count(),
                          "The minutes each round's players buy for after its freeze: at least 0.25.");
    auto limitTeams = server("mp_limitteams", kDefaultTeamsLimit,
                             "How many players more than the other's a team may have: 0 to 20, 0 for no limit.");
    for (auto* const registered : {&freezeTime, &roundTime, &startMoney, &restartRound, &restart, &footsteps,
                                   &friendlyFire, &buyTime, &limitTeams}) {
        if (!*registered) {
            return std::unexpected{std::move(registered->error())};
        }
    }
    const auto defaultColor = kDefaultBulletTracerColor;
    auto tracerColor = console.registerCvar(CvarDesc<std::string>{
        .name = "sv_tracer_color",
        .defaultValue =
            std::format("{:02X}{:02X}{:02X}", defaultColor.red, defaultColor.green, defaultColor.blue),
        .description = "Bullet tracer color as six hexadecimal digits (RRGGBB).",
        .flags = kServerCvar,
        .rule = {.expectation = "six hexadecimal digits (RRGGBB)",
                 .accepts = [](std::string_view text) noexcept { return tracerColorOf(text).has_value(); }}});
    if (!tracerColor) {
        return std::unexpected{std::move(tracerColor.error())};
    }
    return GameCvars{.freezeTime = *freezeTime,
                     .roundTime = *roundTime,
                     .startMoney = *startMoney,
                     .restartRound = *restartRound,
                     .restart = *restart,
                     .footsteps = *footsteps,
                     .friendlyFire = *friendlyFire,
                     .buyTime = *buyTime,
                     .limitTeams = *limitTeams,
                     .tracerColor = *tracerColor};
}

// The values set back are finite, and the cvars have no range: each set holds. The cvars hold numbers:
// the seconds, the minutes and the money they give.

auto GameCvars::roundLengths() const -> RoundLengths {
    const RoundCvars read = readRoundCvars(nx::Seconds{freezeTime.value()}, nx::Minutes{roundTime.value()});
    if (read.freezeTime) {
        freezeTime.setAccepted(read.freezeTime->count());
    }
    if (read.roundTime) {
        roundTime.setAccepted(read.roundTime->count());
    }
    return read.lengths;
}

auto GameCvars::startingMoney() const -> nx::Money {
    const StartMoney read = readStartMoney(startMoney.value());
    if (read.setTo) {
        startMoney.setAccepted(static_cast<float>(read.setTo->value()));
    }
    return read.money;
}

auto GameCvars::buyingTime() const -> nx::Seconds {
    const BuyTime read = readBuyTime(nx::Minutes{buyTime.value()});
    if (read.setTo) {
        buyTime.setAccepted(read.setTo->count());
    }
    return read.length;
}

auto GameCvars::takeRestart() const -> std::optional<nx::Seconds> {
    const std::optional<nx::Seconds> delay =
        restartDelayOf(nx::Seconds{restartRound.value()}, nx::Seconds{restart.value()});
    if (delay) {
        for (const nx::console::Cvar<float>& asked : {restartRound, restart}) {
            asked.setAccepted(0.0F);
        }
    }
    return delay;
}

auto GameCvars::teamsLimit() const -> std::uint32_t {
    // ReadMultiplayCvars holds it between 0 and 20.
    const float limit = std::clamp(limitTeams.value(), 0.0F, kMostTeamsLimit);
    return static_cast<std::uint32_t>(limit);
}

auto GameCvars::doStepsSound() const -> bool {
    return footsteps.value() != 0.0F;
}

auto GameCvars::isFriendlyFireOn() const -> bool {
    return friendlyFire.value() != 0.0F;
}

auto GameCvars::bulletTracerColor() const -> nx::assets::PaletteColor {
    const auto color = tracerColorOf(tracerColor.value());
    nx::invariant(color.has_value(), "GameCvars: the tracer color rule keeps its value valid");
    return *color;
}

} // namespace cs
