// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What a bot may see of a player (docs/10-counter-strike-gameplay.md, section 11): the cone about its
/// view and the spots of a player it looks for, as ReGameDLL_CS's reconstruction of Condition Zero's bot
/// weighs them (CBaseMonster::FInViewCone, CCSBot::IsVisible; GPL, consulted: docs/20 section 3). Whether
/// a line reaches a spot is the server's to tell (Game::seesSpot).
export module cs.bots:botSight;

import std;
import nx.core;
import nx.math;
import cs.shared;
import cs.server;
import :botRules;

export namespace cs {

/// Whether the bot of the player `player` looks around at the moment `now` of a game of `rate`, each
/// `interval`, in whole ticks, at least one: each player at its own tick of it, so that the bots of a game
/// share out their looks over its ticks.
[[nodiscard]] auto looksAroundAt(PlayerIndex player, nx::GameTime now, nx::TickRate rate,
                                 nx::Seconds interval) noexcept -> bool;

/// How many spots of a player a bot looks for (sightSpotsOf).
inline constexpr std::size_t kSightSpotCount = 5;

/// The cone an eye sees, across the ground, as FInViewCone weighs it: from `origin` about the way `facing`,
/// the cosine of half of it being `least`.
struct ViewCone {
    nx::Vec3 origin{};
    nx::Vec3 facing{};
    float least = 0.0F;

    /// Whether `point` is in it: `least` below the dot of the ground's way to it and `facing`. A point
    /// straight above or below `origin` is.
    [[nodiscard]] auto holds(nx::Vec3 point) const noexcept -> bool;
};

/// The cone of `fieldOfView` about `yaw` seen from `origin`.
[[nodiscard]] auto viewConeOf(nx::Vec3 origin, nx::Degrees yaw, nx::Degrees fieldOfView) noexcept -> ViewCone;

/// The spots of `target` a bot at `origin` looks for, in the order IsVisible tries them: the chest, the
/// head, the feet as it stands or ducks, then its sides across the ground's line from `origin` to it,
/// its left first; both sides are its chest when it is straight above or below `origin`.
[[nodiscard]] auto sightSpotsOf(const PerceptionTarget& target, nx::Vec3 origin,
                                const BotSightSpots& spots) noexcept -> std::array<nx::Vec3, kSightSpotCount>;

} // namespace cs
