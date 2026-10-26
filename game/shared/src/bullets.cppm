// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How a round goes through what it meets (docs/10-counter-strike-gameplay.md, section 7.4), as the
/// original's FireBullets3 follows it (ReGameDLL_CS; GPL, consulted: docs/20 section 3): a line
/// after a line, each from a little beyond where the last met something, as far as what it went
/// through leaves it, its damage less with the distance it went and with what it went through.
export module cs.shared:bullets;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;
import :materials;
import :inventory;

export namespace cs {

/// What a round met on its way: the entity, none for the world; the trace that met it, its hit group
/// among it; the damage it deals there (iCurrentDamage); the way it goes; and whether what it met is
/// the world or a brush entity, which its mark shows on (DecalGunshot).
struct BulletHit {
    std::optional<nx::ecs::Entity> entity{};
    nx::collision::Trace trace{};
    nx::Damage damage{};
    nx::Vec3 direction{};
    bool isBrushModel = false;
};

/// What the texture a trace from `start` to `end` met, `hit`, is made of (UTIL_TextureHit): flesh for
/// a body; none where no texture is named.
using HitMaterial =
    std::function<std::optional<TextureMaterial>(const WeaponHit& hit, nx::Vec3 start, nx::Vec3 end)>;

/// What the round of `shot` meets, in order, as FireBullets3 follows it through what `tracer` finds,
/// a line from its origin along its direction as far as its reach, again while its round may meet
/// more: what it met's texture, by `materialOf`, takes from how far it goes into it (metal 0.15 of it,
/// concrete 0.25, a grate and a vent 0.5, tile 0.65, a computer 0.4) and sets what it deals after
/// (metal 0.2, a grate 0.4, a vent and a computer 0.45, tile 0.3, wood 0.6; else 0.5); its damage, cut
/// to a whole number, is its range modifier's power of the 500 units it went, then cut again; past its
/// penetration distance it meets nothing more; what is not a brush model, or the last it may meet, lets
/// it go 42 units in, deals 0.75 after and leaves it 0.75 of the way left, a brush model half; the next
/// line starts that far beyond where it met it along its direction, as far as what is left.
[[nodiscard]] auto followRound(const Shot& shot, const WeaponTracer& tracer, const HitMaterial& materialOf)
    -> std::vector<BulletHit>;

} // namespace cs
