// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the server's agents hear (ADR-0024's perception): the noises the players made in a tick
/// (Game::noises), heard by an agent of the other team within how far each kind carries, as Condition
/// Zero's bot hears the game's events (CCSBot::OnEvent, IsGameEventAudible; ReGameDLL_CS, GPL,
/// consulted: docs/20 section 3). What it hears is where an enemy roughly is, never where exactly: it
/// turns to look, and its sight tells it the rest.
export module cs.server:agentHearing;

import std;
import nx.core;
import nx.meta;
import nx.math;
import nx.ecs;
import nx.navigation;
import nx.agents;
import cs.shared;
import :game;

export namespace cs {

/// How far an agent hears each kind of noise at its sound's full volume (IsGameEventAudible's ranges),
/// and how far off it places what it hears: `errorShare` of how far it is, either way along each axis
/// across (OnEvent's error, 400 at 2000).
struct HearingRules {
    nx::HammerUnits footstep{};
    nx::HammerUnits landing{};
    nx::HammerUnits shot{};
    nx::HammerUnits quietShot{};
    nx::HammerUnits emptyShot{};
    float errorShare = 0.0F;
};

/// Whether an agent may hear by `rules`: each range more than none, its error share none or more.
[[nodiscard]] auto isValid(const HearingRules& rules) noexcept -> bool;

/// How far a noise of `kind` carries by `rules`, at its sound's full volume.
[[nodiscard]] auto rangeOf(const HearingRules& rules, NoiseKind kind) noexcept -> nx::HammerUnits;

/// Who hears: the team it plays in, none for an agent of no team, which hears every player, and its ear.
struct Listener {
    std::optional<Team> team{};
    nx::Vec3 ear{};
};

/// What `listener` hears of `noises`, by `rules`, as observations of the enemies that made them
/// (nx::agents::Brain::sense), the nearest noise of each, in the order of `noises`: those of another team
/// than its own, within the range of their kind times their loudness of its ear; each placed off where
/// it was made by its error across, drawn from `random`, then walked back onto the mesh of `query` from
/// where it was made (as OnEvent keeps it on the noise's area), off the mesh where it was made off it,
/// its eye as high above that.
[[nodiscard]] auto heardBy(const Listener& listener, std::span<const GameNoise> noises,
                           const HearingRules& rules, nx::navigation::NavigationQuery& query,
                           nx::Random& random) -> std::vector<nx::agents::Observation>;

} // namespace cs

template <>
struct nx::meta::Description<cs::HearingRules> {
    static constexpr std::string_view kName = "cs::HearingRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::HearingRules::footstep>("footstep"),
        nx::meta::field<&cs::HearingRules::landing>("landing"),
        nx::meta::field<&cs::HearingRules::shot>("shot"),
        nx::meta::field<&cs::HearingRules::quietShot>("quietShot"),
        nx::meta::field<&cs::HearingRules::emptyShot>("emptyShot"),
        nx::meta::field<&cs::HearingRules::errorShare>("errorShare"),
    };
};
