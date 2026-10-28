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
import cs.server;
import :botRules;

export namespace cs {

/// How many spots of a player a bot looks for (sightSpotsOf).
inline constexpr std::size_t kSightSpotCount = 5;

/// Whether `point` is in the cone of `fieldOfView` about `yaw` seen from `origin`, across the ground, as
/// FInViewCone weighs it: the cosine of half the cone below the dot of the ground's way to it and the
/// yaw's way. A point straight above or below `origin` is in it.
[[nodiscard]] auto isInViewCone(nx::Vec3 origin, nx::Degrees yaw, nx::Degrees fieldOfView,
                                nx::Vec3 point) noexcept -> bool;

/// The spots of `target` a bot at `origin` looks for, in the order IsVisible tries them: the chest, the
/// head, the feet as it stands or ducks, then its sides across the ground's line from `origin` to it,
/// its left first; both sides are its chest when it is straight above or below `origin`.
[[nodiscard]] auto sightSpotsOf(const PerceptionTarget& target, nx::Vec3 origin,
                                const BotSightSpots& spots) noexcept -> std::array<nx::Vec3, kSightSpotCount>;

} // namespace cs
