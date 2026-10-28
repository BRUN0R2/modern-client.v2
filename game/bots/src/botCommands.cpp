// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.bots;

import std;
import nx.core;
import nx.console;
import cs.shared;

namespace cs {

namespace {

using nx::console::CommandLine;
using nx::console::Console;
using nx::console::ConsoleError;

/// `bot_kick`'s argument for every bot, which no argument means too.
constexpr std::string_view kEveryBot = "all";

// A server's cvar, told to its players, as the original's FCVAR_SERVER.
constexpr nx::console::CvarFlags kServerCvar{nx::console::CvarFlag::Notify};

/// `command`'s bot joining `team` through `actions`, bot_quota counting it.
[[nodiscard]] auto addBot(const BotActions& actions, const BotCvars& cvars, std::string_view command,
                          const CommandLine& line, std::optional<Team> team)
    -> nx::Result<void, ConsoleError> {
    if (!line.arguments().empty()) {
        return nx::fail(ConsoleError::InvalidArguments,
                        std::format("usage: {}: a bot by its name comes with the bots' names", command));
    }
    if (auto added = actions.add(team); !added) {
        return nx::fail(ConsoleError::CommandFailed, std::string{command}, std::move(added.error()));
    }
    cvars.setQuota(cvars.quotaCount() + 1);
    return {};
}

} // namespace

auto BotCvars::registerIn(Console& console) -> nx::Result<BotCvars, nx::console::CvarError> {
    auto quota = console.registerCvar(nx::console::CvarDesc<float>{
        .name = "bot_quota",
        .defaultValue = 0.0F,
        .description = "How many bots the game keeps, adding or kicking one at a time.",
        .flags = kServerCvar});
    if (!quota) {
        return std::unexpected{std::move(quota.error())};
    }
    return BotCvars{.quota = *quota};
}

auto BotCvars::quotaCount() const noexcept -> std::uint32_t {
    const float value = quota.value();
    if (!(value >= 1.0F)) {
        return 0;
    }
    return value >= static_cast<float>(std::numeric_limits<std::uint32_t>::max())
               ? std::numeric_limits<std::uint32_t>::max()
               : static_cast<std::uint32_t>(value);
}

void BotCvars::setQuota(std::uint32_t count) const {
    // bot_quota has no range and is neither read-only nor a cheat: the set holds.
    std::ignore = quota.set(static_cast<float>(count));
}

auto registerBotCommands(Console& console, BotCvars cvars, BotActions actions)
    -> nx::Result<void, ConsoleError> {
    const auto add = [&console](nx::console::CommandDesc desc, nx::console::CommandHandler handler) {
        return console.registerCommand(desc, std::move(handler));
    };
    for (auto&& registered : {
             add({.name = "bot_add", .description = "Adds a bot to the team the game's auto-select joins."},
                 [actions, cvars](Console&, const CommandLine& line) {
                     return addBot(actions, cvars, "bot_add", line, std::nullopt);
                 }),
             add({.name = "bot_add_t", .description = "Adds a bot to the terrorists."},
                 [actions, cvars](Console&, const CommandLine& line) {
                     return addBot(actions, cvars, "bot_add_t", line, Team::Terrorists);
                 }),
             add({.name = "bot_add_ct", .description = "Adds a bot to the counter-terrorists."},
                 [actions, cvars](Console&, const CommandLine& line) {
                     return addBot(actions, cvars, "bot_add_ct", line, Team::CounterTerrorists);
                 }),
             add({.name = "bot_kick", .description = "Kicks every bot: bot_kick [all]; bot_quota goes to 0."},
                 [actions, cvars](Console&, const CommandLine& line) -> nx::Result<void, ConsoleError> {
                     const std::span<const std::string> words = line.arguments();
                     if (words.size() > 1 || (words.size() == 1 && words[0] != kEveryBot)) {
                         return nx::fail(
                             ConsoleError::InvalidArguments,
                             "usage: bot_kick [all]: a bot by its name comes with the bots' names");
                     }
                     if (auto kicked = actions.kickAll(); !kicked) {
                         return nx::fail(ConsoleError::CommandFailed, "bot_kick", std::move(kicked.error()));
                     }
                     cvars.setQuota(0);
                     return {};
                 }),
         }) {
        if (!registered) {
            return registered;
        }
    }
    return {};
}

} // namespace cs
