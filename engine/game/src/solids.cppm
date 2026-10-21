// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The solid brush entities of a world and the server's traces among them (not exported): what the
/// pushers carry bodies through, and what an entity the game drops lands on.
export module nx.game:solids;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;
import :components;

namespace nx::game {

/// A solid brush entity, where it is now.
struct SolidBrush {
    ecs::Entity entity{};
    collision::ModelIndex model{};
    Vec3 origin{};
    EulerAngles angles{};
};

/// The solid brush entities of `world`, where they are now.
[[nodiscard]] auto solidBrushesOf(ecs::World& world) -> std::vector<SolidBrush>;

/// The server's way for `hull` from `start` to `end` through the world of `hulls` and the solid brush
/// entities of `solids` but `ignored` (SV_Move of a box among brush models): the world's trace, then
/// each entity's that stops it sooner or starts in solid; once all-solid, no other is looked at.
[[nodiscard]] auto serverTrace(const collision::BrushHulls& hulls, std::span<const SolidBrush> solids,
                               std::optional<ecs::Entity> ignored, collision::Hull hull, Vec3 start, Vec3 end)
    -> collision::Trace;

} // namespace nx::game
