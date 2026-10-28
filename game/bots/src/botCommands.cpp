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
/// How far below the number of the next difficulty bot_difficulty still names the one before
/// (GetDifficultyLevel's 0.9, 1.9 and 2.9).
constexpr float kDifficultyTolerance = 0.1F;

/// `command`'s bot joining `team` through `actions`, of the profile `line` names or one drawn, bot_quota
/// counting it.
[[nodiscard]] auto addBot(const BotActions& actions, const BotCvars& cvars, std::string_view command,
                          const CommandLine& line, std::optional<Team> team)
    -> nx::Result<void, ConsoleError> {
    const std::span<const std::string> words = line.arguments();
    if (words.size() > 1) {
        return nx::fail(ConsoleError::InvalidArguments, std::format("usage: {} [profile]", command));
    }
    const BotRequest request{.profile = words.empty() ? std::nullopt : std::optional{words.front()},
                             .team = team,
                             .difficulty = cvars.difficultyLevel(),
                             .prefix = cvars.prefix.value()};
    if (auto added = actions.add(request); !added) {
        return nx::fail(ConsoleError::CommandFailed, std::string{command}, std::move(added.error()));
    }
    cvars.setQuota(cvars.quotaCount() + 1);
    return {};
}

/// `bot_kick`: every bot, bot_quota set to 0, for no argument or kEveryBot; else the bot of the name
/// `line` gives, one less in bot_quota.
[[nodiscard]] auto kickBots(const BotActions& actions, const BotCvars& cvars, const CommandLine& line)
    -> nx::Result<void, ConsoleError> {
    const std::span<const std::string> words = line.arguments();
    if (words.size() > 1) {
        return nx::fail(ConsoleError::InvalidArguments, "usage: bot_kick [all | name]");
    }
    if (words.empty() || words.front() == kEveryBot) {
        if (auto kicked = actions.kickAll(); !kicked) {
            return nx::fail(ConsoleError::CommandFailed, "bot_kick", std::move(kicked.error()));
        }
        cvars.setQuota(0);
        return {};
    }
    if (auto kicked = actions.kick(words.front()); !kicked) {
        return nx::fail(ConsoleError::CommandFailed, "bot_kick", std::move(kicked.error()));
    }
    const std::uint32_t quota = cvars.quotaCount();
    cvars.setQuota(quota > 0 ? quota - 1 : 0);
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
    auto difficulty = console.registerCvar(nx::console::CvarDesc<float>{
        .name = "bot_difficulty",
        .defaultValue = 0.0F,
        .description = "The difficulty the bots that join play at: 0 easy, 1 normal, 2 hard, 3 expert.",
        .flags = kServerCvar});
    if (!difficulty) {
        return std::unexpected{std::move(difficulty.error())};
    }
    auto prefix = console.registerCvar(nx::console::CvarDesc<std::string>{
        .name = "bot_prefix",
        .defaultValue = "",
        .description = "What the bots that join are named after, a blank between: [CLAN] Name.",
        .flags = kServerCvar});
    if (!prefix) {
        return std::unexpected{std::move(prefix.error())};
    }
    return BotCvars{.quota = *quota, .difficulty = *difficulty, .prefix = *prefix};
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

auto BotCvars::difficultyLevel() const noexcept -> BotDifficulty {
    const float value = difficulty.value();
    for (const BotDifficulty level : nx::EnumArray<BotDifficulty, bool>::keys()) {
        const float next = static_cast<float>(std::to_underlying(level) + 1);
        if (value < next - kDifficultyTolerance) {
            return level;
        }
    }
    return BotDifficulty::Expert;
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
             add({.name = "bot_add",
                  .description = "Adds a bot, of a profile or one drawn, to the team the auto-select joins."},
                 [actions, cvars](Console&, const CommandLine& line) {
                     return addBot(actions, cvars, "bot_add", line, std::nullopt);
                 }),
             add({.name = "bot_add_t",
                  .description = "Adds a bot, of a profile or one drawn, to the terrorists."},
                 [actions, cvars](Console&, const CommandLine& line) {
                     return addBot(actions, cvars, "bot_add_t", line, Team::Terrorists);
                 }),
             add({.name = "bot_add_ct",
                  .description = "Adds a bot, of a profile or one drawn, to the counter-terrorists."},
                 [actions, cvars](Console&, const CommandLine& line) {
                     return addBot(actions, cvars, "bot_add_ct", line, Team::CounterTerrorists);
                 }),
             add({.name = "bot_kick",
                  .description =
                      "Kicks every bot, bot_quota going to 0, or the one named: bot_kick [all | name]."},
                 [actions, cvars](Console&, const CommandLine& line) {
                     return kickBots(actions, cvars, line);
                 }),
         }) {
        if (!registered) {
            return registered;
        }
    }
    return {};
}

} // namespace cs
