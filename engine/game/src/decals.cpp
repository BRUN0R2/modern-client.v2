// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.game;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.assets;
import nx.collision;

namespace nx::game {

namespace {

constexpr std::string_view kDecalClass = "infodecal";
/// The spawnflag of an infodecal a deathmatch game leaves out (SF_DECAL_NOTINDEATHMATCH).
constexpr int kNotInDeathmatch = 1 << 11;
/// How far the line that finds what a decal lies on reaches from its origin, along each axis.
constexpr float kReach = 5.0F;

} // namespace

auto staticDecalsOf(std::span<const assets::BspEntity> entities, ecs::World& world,
                    const collision::BrushHulls& hulls) -> std::vector<StaticDecal> {
    const Vec3 reach{.x = kReach, .y = kReach, .z = kReach};
    std::vector<StaticDecal> decals;
    for (const assets::BspEntity& entity : entities) {
        const bool isNamed = !entity.find("targetname").value_or("").empty();
        const bool isLeftOut = (entity.findInteger("spawnflags").value_or(0) & kNotInDeathmatch) != 0;
        if (entity.find("classname") != kDecalClass || isNamed || isLeftOut) {
            continue;
        }
        const Vec3 origin = entity.findVector("origin").value_or(Vec3{});
        // The original's line looks for what the decal lies on alone (ignore_monsters).
        const SolidHit hit = traceSolidsHit(
            world, hulls,
            ServerTrace{.start = origin - reach, .end = origin + reach, .type = TraceType::NoMonsters});
        decals.push_back(StaticDecal{.texture = std::string{entity.find("texture").value_or("")},
                                     .origin = origin,
                                     .entity = hit.entity});
    }
    return decals;
}

} // namespace nx::game
