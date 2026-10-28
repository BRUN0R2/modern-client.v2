// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The console's bot commands and cvars (docs/10-counter-strike-gameplay.md, section 11), by Condition
/// Zero's names, as ReGameDLL_CS's reconstruction of its bot takes them (CCSBotManager::ServerCommand,
/// BotAddCommand; cs_bot_init.cpp's cvars; GPL, consulted: docs/20 section 3).
export module cs.bots:botCommands;

import std;
import nx.core;
import nx.console;
import cs.shared;
import :botRules;

export namespace cs {

/// Why a bot command could not act on the game.
enum class BotActionError : std::uint8_t {
    NoMap,       ///< No map is played.
    Unavailable, ///< The map's bots could not be made (cause: the BotError).
    Refused,     ///< The bot asked for cannot join (cause: the BotJoinError).
    NoSuchBot,   ///< No bot has the name given.
};

/// A bot a command asks for: the profile named `profile`, else one drawn of `difficulty`; in `team`,
/// else the game's default; named after `prefix` (bot_prefix), empty for none.
struct BotRequest {
    std::optional<std::string> profile{};
    std::optional<Team> team{};
    BotDifficulty difficulty = BotDifficulty::Easy;
    std::string prefix{};
};

/// What the bot commands do to the game played.
struct BotActions {
    /// The bot `request` asks for joins. Errors: NoMap, Unavailable, Refused.
    std::function<nx::Result<void, BotActionError>(const BotRequest& request)> add;
    /// The bot whose player is named `name`, exactly, leaves the game. Errors: NoMap, NoSuchBot.
    std::function<nx::Result<void, BotActionError>(std::string_view name)> kick;
    /// Every bot leaves the game. Errors: NoMap.
    std::function<nx::Result<void, BotActionError>()> kickAll;
};

/// The bots' cvars: `bot_quota` (0), how many bots the game keeps (Bots::keepQuota), a whole number of
/// it, none below 0; `bot_difficulty` (0), the difficulty the bots that join play at; `bot_prefix`
/// (empty), what the bots that join are named after.
struct BotCvars {
    nx::console::Cvar<float> quota;
    nx::console::Cvar<float> difficulty;
    nx::console::Cvar<std::string> prefix;

    /// Registers them in `console`. Errors: those of Console::registerCvar.
    [[nodiscard]] static auto registerIn(nx::console::Console& console)
        -> nx::Result<BotCvars, nx::console::CvarError>;

    /// How many bots bot_quota asks for: its whole part (int(cv_bot_quota.value)), none for less than 1.
    [[nodiscard]] auto quotaCount() const noexcept -> std::uint32_t;
    /// The difficulty bot_difficulty names (GetDifficultyLevel): each from a tenth below its number, easy
    /// below 0.9, normal below 1.9, hard below 2.9, else expert.
    [[nodiscard]] auto difficultyLevel() const noexcept -> BotDifficulty;
    /// Sets bot_quota to `count`, as the bot commands set it.
    void setQuota(std::uint32_t count) const;
};

/// Registers in `console` `bot_add [profile]` (the game's default team), `bot_add_t [profile]` and
/// `bot_add_ct [profile]`, each adding one to bot_quota once its bot, of the profile named or of one of
/// bot_difficulty's difficulty, named after bot_prefix, joins; and `bot_kick [all | name]`: every bot,
/// setting bot_quota to 0, or the bot of that name, one less in it (BotAddCommand, ServerCommand); which
/// act through `actions`. `console` must outlive the commands. Errors: those of
/// nx::console::Console::registerCommand.
[[nodiscard]] auto registerBotCommands(nx::console::Console& console, BotCvars cvars, BotActions actions)
    -> nx::Result<void, nx::console::ConsoleError>;

} // namespace cs
