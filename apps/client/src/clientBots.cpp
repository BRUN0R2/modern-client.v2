// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.core;
import nx.jobs;
import nx.platform;
import nx.console;
import cs.bots;

namespace app {

namespace {

constexpr nx::log::Category kLog{"client"};

} // namespace

auto readBotRules(const std::filesystem::path& dataFolder) -> nx::Result<cs::BotRules, cs::BotActionError> {
    const auto file = dataFolder / "cstrike" / "bots.toml";
    auto text = nx::platform::readTextFile(file);
    if (!text) {
        return nx::fail(cs::BotActionError::Unavailable, std::move(text.error()));
    }
    auto rules = cs::readBotRules(*text, nx::platform::toUtf8(file));
    if (!rules) {
        return nx::fail(cs::BotActionError::Unavailable, std::move(rules.error()));
    }
    return std::move(*rules);
}

ClientBots::ClientBots(nx::Result<cs::BotRules, cs::BotActionError> readRules, nx::jobs::JobSystem& workers,
                       const cs::BotCvars& botCvars) noexcept
    : rules{std::move(readRules)}, jobs{&workers}, cvars{&botCvars} {}

auto ClientBots::registerCommands(nx::console::Console& console, PlayedSession played)
    -> nx::Result<void, ClientError> {
    cs::BotActions actions{
        .add = [this, played](const cs::BotRequest& request) -> nx::Result<void, cs::BotActionError> {
            Session* const session = played();
            if (session == nullptr) {
                return nx::fail(cs::BotActionError::NoMap);
            }
            auto bots = botsOf(*session);
            if (!bots) {
                return std::unexpected{std::move(bots.error())};
            }
            if (!request.profile) {
                std::ignore = (*bots)->add(request.team, request.difficulty, request.prefix); // whom it added
                return {};
            }
            if (auto added = (*bots)->addProfile(*request.profile, request.team, request.prefix); !added) {
                return nx::fail(cs::BotActionError::Refused, std::move(added.error()));
            }
            return {};
        },
        .kick = [played](std::string_view name) -> nx::Result<void, cs::BotActionError> {
            Session* const session = played();
            if (session == nullptr) {
                return nx::fail(cs::BotActionError::NoMap);
            }
            if (!session->bots) {
                return nx::fail(cs::BotActionError::NoSuchBot, std::format("no bot is named {}", name));
            }
            if (auto kicked = session->bots->kickNamed(name); !kicked) {
                return nx::fail(cs::BotActionError::NoSuchBot, std::move(kicked.error()));
            }
            return {};
        },
        .kickAll = [played]() -> nx::Result<void, cs::BotActionError> {
            Session* const session = played();
            if (session == nullptr) {
                return nx::fail(cs::BotActionError::NoMap);
            }
            if (session->bots) {
                session->bots->kickAll();
            }
            return {};
        }};
    if (auto registered = cs::registerBotCommands(console, *cvars, std::move(actions)); !registered) {
        return nx::fail(ClientError::SettingsUnavailable, std::move(registered.error()));
    }
    return {};
}

auto ClientBots::botsOf(Session& session) -> nx::Result<cs::Bots*, cs::BotActionError> {
    if (session.bots) {
        return session.bots.get();
    }
    if (!rules) {
        return std::unexpected{rules.error()};
    }
    if (!session.agentMesh) {
        return nx::fail(cs::BotActionError::Unavailable, session.agentMesh.error());
    }
    auto bots = cs::Bots::create(*session.game, *session.agentMesh, *jobs, *rules);
    if (!bots) {
        return nx::fail(cs::BotActionError::Unavailable, std::move(bots.error()));
    }
    session.bots = std::move(*bots);
    return session.bots.get();
}

void ClientBots::keepQuota(Session& session) {
    const std::uint32_t quota = cvars->quotaCount();
    if (quota > 0 && !session.bots) {
        if (auto bots = botsOf(session); !bots) {
            nx::log::warning(kLog, "bot_quota set back to 0: {}", nx::describe(bots.error()));
            cvars->setQuota(0);
            return;
        }
    }
    if (session.bots) {
        session.bots->keepQuota(quota, cvars->difficultyLevel(), cvars->prefix.value());
    }
}

} // namespace app
