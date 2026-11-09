// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Counter-Strike's bots in the maps the client plays (docs/10-counter-strike-gameplay.md, section 11).
export module app.client:bots;

import std;
import nx.core;
import nx.jobs;
import nx.console;
import cs.bots;
import :client;
import :session;

namespace app {

/// The bots' rules of `cstrike/bots.toml` in the project's `dataFolder`. Errors: Unavailable, with the cause.
[[nodiscard]] auto readBotRules(const std::filesystem::path& dataFolder)
    -> nx::Result<cs::BotRules, cs::BotActionError>;

/// The bots of the maps the client plays: made for a map the first time they are asked for, by the bots'
/// rules on the mesh the map built as it loaded, deciding in jobs, kept as `bot_quota` asks, and the
/// console's `bot_*` commands. It stays where it is made: its commands point into it.
class ClientBots {
public:
    /// The bots made by `readRules`, or none and why when they could not be read, deciding in jobs of
    /// `workers`, kept as `botCvars` say; `workers` and `botCvars` must outlive it.
    ClientBots(nx::Result<cs::BotRules, cs::BotActionError> readRules, nx::jobs::JobSystem& workers,
               const cs::BotCvars& botCvars) noexcept;

    ClientBots(const ClientBots&) = delete;
    auto operator=(const ClientBots&) -> ClientBots& = delete;
    ClientBots(ClientBots&&) = delete;
    auto operator=(ClientBots&&) -> ClientBots& = delete;
    ~ClientBots() = default;

    /// Registers in `console` the bot commands (cs::registerBotCommands), acting on the session `played`
    /// gives. Errors: SettingsUnavailable, with the cause.
    [[nodiscard]] auto registerCommands(nx::console::Console& console, PlayedSession played)
        -> nx::Result<void, ClientError>;

    /// Keeps the bots of `session` as bot_quota asks, those it adds of bot_difficulty's difficulty
    /// (cs::Bots::keepQuota), making them first when it asks for any; when they cannot be made, it is logged
    /// and bot_quota set back to 0.
    void keepQuota(Session& session);

private:
    /// The bots of `session`, made the first time they are asked for. Errors: Unavailable, with the cause.
    [[nodiscard]] auto botsOf(Session& session) -> nx::Result<cs::Bots*, cs::BotActionError>;

    nx::Result<cs::BotRules, cs::BotActionError> rules;
    nx::jobs::JobSystem* jobs;
    const cs::BotCvars* cvars;
};

} // namespace app
