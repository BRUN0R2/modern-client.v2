// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The decals a map's infodecal entities stamp as its game starts (docs/05-rendering.md, section 8), as
/// Counter-Strike's game makes them (ReGameDLL_CS's CDecal, GPL, consulted: docs/20 section 3): each
/// on what is solid at its origin, the world or a brush entity, which the clients stamp it on for good.
///
/// Today: the decals stamped as the game starts; those of an infodecal with a name, stamped when it is
/// used, come with the game's decal events (TODO.md).
export module nx.game:decals;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.assets;
import nx.collision;

export namespace nx::game {

/// A decal a map stamps as its game starts: its image's name in decals.wad, its origin, and the brush
/// entity it lies on, none for the world.
struct StaticDecal {
    std::string texture{};
    Vec3 origin{};
    std::optional<ecs::Entity> entity{};
};

/// The decals the infodecal entities of `entities` stamp as the game starts (CDecal::StaticDecal), in
/// their order, among the brush entities of `world` where they are and the world of `hulls`: each its
/// "texture" at its "origin", on the brush entity a line through what is solid (traceSolidsHit) meets
/// from 5 units below its origin on each axis to 5 above, the world when it meets none. Left out:
/// those with a "targetname", stamped when they are used; and those not stamped in a deathmatch game
/// (spawnflag 2048), Counter-Strike's being one.
[[nodiscard]] auto staticDecalsOf(std::span<const assets::BspEntity> entities, ecs::World& world,
                                  const collision::BrushHulls& hulls) -> std::vector<StaticDecal>;

} // namespace nx::game
