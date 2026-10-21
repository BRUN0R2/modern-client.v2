// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What a player uses with its use key (docs/07-simulation-and-physics.md, section 5.2), as the
/// original's game finds it (CBasePlayer::PlayerUse, FindEntityInSphere; ReGameDLL and ReHLDS, GPL,
/// consulted: docs/20-legal-and-licensing.md, section 3).
export module nx.game:use;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;

export namespace nx::game {

/// Where a player looks as it uses: from its eye, the way it faces.
struct UseView {
    Vec3 eye{};
    Vec3 forward{}; ///< Of length 1.
};

/// `user`, a player at the origin of its Transform looking as `view` says, uses what is in front of it
/// (CBasePlayer::PlayerUse): of the brush entities a player's use reaches, a button that takes no
/// damage or a door used only, whose linked box comes within 64 units of its origin, the one whose
/// middle is most straight ahead from its eye, more than 0.7 of the way (about 45 degrees off); as the
/// original, through walls. Returns what it used; none when nothing was in front.
auto playerUse(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity user, const UseView& view)
    -> std::optional<ecs::Entity>;

} // namespace nx::game
