// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the server's agents touch (ADR-0024's perception): the living players whose bodies meet theirs,
/// felt where they are, since a body blocked by another knows it is there, seen or not.
export module cs.server:agentTouch;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.agents;
import cs.shared;
import :game;

export namespace cs {

/// How far apart two boxes still touch: a body moving into another stops a thirty-second of a unit short
/// of it (the trace's DIST_EPSILON), and a unit more for the floats.
inline constexpr float kTouchMargin = 1.0F + (1.0F / 32.0F);

/// Who touches: its body, the team it plays in, none for an agent of no team, which feels every player,
/// and its box at its origin.
struct Toucher {
    nx::ecs::Entity entity{};
    std::optional<Team> team{};
    nx::Vec3 origin{};
    nx::Aabb box{};
};

/// The enemies `toucher` touches among `targets`, certain of where they are (nx::agents::Brain::sense),
/// in the order of `targets`: the players of another team than its own, but itself, whose boxes, standing
/// or ducked at their origins, come within kTouchMargin of its own.
[[nodiscard]] auto touchedBy(const Toucher& toucher, std::span<const PerceptionTarget> targets)
    -> std::vector<nx::agents::Sensed>;

} // namespace cs
