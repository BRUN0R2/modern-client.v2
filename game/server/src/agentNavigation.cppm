// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the server's agents walk by (docs/adr/0023-navigation.md, ADR-0024): the profile of a player who
/// walks standing, which the bots and the bodies of mods share, since none of them ducks yet. Whoever
/// plays the map (the client's session, today) builds the map's mesh of it in jobs as the map loads, so
/// that no agent waits for it.
export module cs.server:agentNavigation;

import std;
import nx.core;
import nx.game;
import nx.navigation;

export namespace cs {

/// The name data/cstrike/navigation.toml gives the player's profile.
inline constexpr std::string_view kPlayerProfileName = "player";

/// The profile of `navigation` a standing player walks by: the player's (kPlayerProfileName), which must
/// match the movement (kStandingBox, MovementSettings::stepSize, kMinGroundNormalZ), with its crouched
/// height its standing one, so that its mesh leaves out what only a ducked player passes. Errors:
/// InvalidProfile (no player's profile, or one that does not match the movement).
[[nodiscard]] auto standingPlayerProfile(const nx::game::NavigationData& navigation)
    -> nx::Result<nx::navigation::AgentProfile, nx::navigation::MeshBuildError>;

} // namespace cs
