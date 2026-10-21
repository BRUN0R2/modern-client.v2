// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The boxes the engine links entities by (not exported): what its pushers and its triggers compare
/// (SV_LinkEdict).
export module nx.game:links;

import std;
import nx.core;
import nx.math;
import :components;

namespace nx::game {

/// The engine grows every box it links by a unit on every side, and a brush model's bounds by
/// another when it loads them.
inline constexpr float kLinkGrowth = 1.0F;
inline constexpr float kLoadedBoundsGrowth = 1.0F;

/// The box the engine links a brush entity by, about the world: its loaded bounds where it is;
/// turned, a cube as wide as their farthest side; then a unit wider on every side.
[[nodiscard]] auto linkedBoxOf(const Aabb& bounds, Vec3 origin, EulerAngles angles) -> Aabb;

/// The box the engine links `body` at `origin` by: its box there, a unit wider on every side.
[[nodiscard]] auto linkedBoxOf(const Body& body, Vec3 origin) -> Aabb;

} // namespace nx::game
