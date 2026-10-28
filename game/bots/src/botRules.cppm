// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How the bots see, remember, move and keep their number (docs/10-counter-strike-gameplay.md,
/// section 11): data/cstrike/bots.toml, with the values of ReGameDLL_CS's reconstruction of Condition
/// Zero's bot where it has them (CCSBot::IsVisible, CBaseMonster::FInViewCone,
/// CCSBotManager::MonitorBotCVars; GPL, consulted: docs/20 section 3) and the project's own where it
/// has none.
export module cs.bots:botRules;

import std;
import nx.core;
import nx.meta;
import nx.data;
import nx.agents;

export namespace cs {

/// Why the bots could not be made.
enum class BotError : std::uint8_t {
    InvalidData, ///< bots.toml breaks its schema or its rules (cause: the DataError, when there is one).
    NavigationUnavailable, ///< No navigation profile matches a standing player, or its mesh was not built
                           ///< (cause).
    Unregistered,          ///< A component of the bots' bodies could not be registered in the game (cause).
};

/// Where a bot looks for a player it may see, from the player's origin, in the order IsVisible tries
/// them: its chest (the origin itself), its head above, its feet below as it stands or ducks, and its
/// sides, either way across the bot's line to it.
struct BotSightSpots {
    nx::HammerUnits head{};
    nx::HammerUnits standingFeet{};
    nx::HammerUnits duckedFeet{};
    nx::HammerUnits side{};
};

/// data/cstrike/bots.toml: what a bot sees (the cone about its view's yaw, the players it looks at each
/// tick in turn, and the spots of each), how long and how many it remembers, how near it attacks, how
/// near it arrives and how far a goal moves before it finds another way, how far its hunt's next place
/// is drawn, and how often bot_quota is looked at.
struct BotRules {
    nx::Degrees fieldOfView{};
    std::uint32_t perceptionBudget = 0;
    BotSightSpots spots{};
    nx::Seconds memoryLifetime{};
    std::uint32_t memoryLimit = 0;
    nx::HammerUnits attackDistance{};
    nx::HammerUnits arrivalDistance{};
    nx::HammerUnits replanDistance{};
    nx::HammerUnits huntRadius{};
    nx::Seconds quotaInterval{};
};

/// Reads `text`, the file `sourceName`: every distance, time and budget more than none, the cone a
/// whole turn at most. Errors: InvalidData.
[[nodiscard]] auto readBotRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<BotRules, BotError>;

/// The rules of the brain a bot of `rules` thinks with.
[[nodiscard]] auto brainRulesOf(const BotRules& rules) noexcept -> nx::agents::Rules;

} // namespace cs

template <>
struct nx::meta::Description<cs::BotSightSpots> {
    static constexpr std::string_view kName = "cs::BotSightSpots";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BotSightSpots::head>("head", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotSightSpots::standingFeet>("standingFeet", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotSightSpots::duckedFeet>("duckedFeet", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotSightSpots::side>("side", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::BotRules> {
    static constexpr std::string_view kName = "cs::BotRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BotRules::fieldOfView>("fieldOfView"),
        nx::meta::field<&cs::BotRules::perceptionBudget>("perceptionBudget"),
        nx::meta::field<&cs::BotRules::spots>("spots"),
        nx::meta::field<&cs::BotRules::memoryLifetime>("memoryLifetime"),
        nx::meta::field<&cs::BotRules::memoryLimit>("memoryLimit"),
        nx::meta::field<&cs::BotRules::attackDistance>("attackDistance"),
        nx::meta::field<&cs::BotRules::arrivalDistance>("arrivalDistance"),
        nx::meta::field<&cs::BotRules::replanDistance>("replanDistance"),
        nx::meta::field<&cs::BotRules::huntRadius>("huntRadius"),
        nx::meta::field<&cs::BotRules::quotaInterval>("quotaInterval"),
    };
};
