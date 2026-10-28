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

export namespace cs {

/// Why a bot command could not act on the game.
enum class BotActionError : std::uint8_t {
    NoMap,       ///< No map is played.
    Unavailable, ///< The map's bots could not be made (cause: the BotError).
};

/// What the bot commands do to the game played.
struct BotActions {
    /// A bot joins `team`, or the game's default team for none. Errors: NoMap, Unavailable.
    std::function<nx::Result<void, BotActionError>(std::optional<Team> team)> add;
    /// Every bot leaves the game. Errors: NoMap.
    std::function<nx::Result<void, BotActionError>()> kickAll;
};

/// The bots' cvars: `bot_quota` (0), how many bots the game keeps (Bots::keepQuota), a whole number of
/// it, none below 0.
struct BotCvars {
    nx::console::Cvar<float> quota;

    /// Registers them in `console`. Errors: those of Console::registerCvar.
    [[nodiscard]] static auto registerIn(nx::console::Console& console)
        -> nx::Result<BotCvars, nx::console::CvarError>;

    /// How many bots bot_quota asks for: its whole part (int(cv_bot_quota.value)), none for less than 1.
    [[nodiscard]] auto quotaCount() const noexcept -> std::uint32_t;
    /// Sets bot_quota to `count`, as the bot commands set it.
    void setQuota(std::uint32_t count) const;
};

/// Registers in `console` `bot_add` (the game's default team), `bot_add_t` and `bot_add_ct`, each adding
/// one to bot_quota once its bot joins, and `bot_kick` (every bot, `bot_kick all` too), setting it to 0,
/// which act through `actions`. A bot is not kicked by its name until the bots have names (TODO.md).
/// `console` must outlive the commands. Errors: those of nx::console::Console::registerCommand.
[[nodiscard]] auto registerBotCommands(nx::console::Console& console, BotCvars cvars, BotActions actions)
    -> nx::Result<void, nx::console::ConsoleError>;

} // namespace cs
