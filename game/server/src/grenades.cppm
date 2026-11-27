// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The grenades thrown (docs/10-counter-strike-gameplay.md, section 7.3), as ReGameDLL_CS's CGrenade plays
/// them (GPL, consulted: docs/20 section 3): each flies at its share of the world's gravity, bounces off what
/// is solid as MOVETYPE_BOUNCE does, damped as it rolls (BounceTouch), and goes off once its fuse is over, a
/// smoke grenade once it lies still too: the explosive hurting who is near (Explode3, RadiusDamage), the
/// flashbang blinding who sees it (Explode, RadiusFlash), the smoke grenade clouding where it lies
/// (SG_Detonate).
export module cs.server:grenades;

import std;
import nx.core;
import nx.math;
import cs.shared;
import :gameState;

export namespace cs {

/// `thrower` throws `thrown`: a grenade flying from then on.
void throwGrenade(GameState& game, const Player& thrower, const Throw& thrown);

/// One tick of the grenades flying: each moves, bounces, and goes off once it is due.
void thinkGrenades(GameState& game);

/// How long and how white `victim`, of eye `eye` looking along `forward`, is blinded by a flash `damage`
/// strong at `source`, `distance` from it, of a flash of `radius` (RadiusFlash): none out of it.
[[nodiscard]] auto blindingOf(nx::Vec3 source, nx::Vec3 eye, nx::Vec3 forward, float damage, float radius)
    -> std::optional<Blinding>;

} // namespace cs
