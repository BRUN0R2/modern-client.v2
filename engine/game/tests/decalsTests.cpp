// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The decals a map's infodecals stamp as its game starts, among a wall (solid from x = 100) and a
// func_wall block (from x = 0 to 64).

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.assets;
import nx.collision;
import nx.game;

#include "solidsWorld.hpp"

namespace {

/// An infodecal of `texture` at `origin`, with `keys` besides.
[[nodiscard]] auto infodecal(std::string_view texture, nx::Vec3 origin,
                             std::initializer_list<std::pair<std::string, std::string>> keys = {})
    -> nx::assets::BspEntity {
    nx::assets::BspEntity entity{
        .keyValues = {{"classname", "infodecal"},
                      {"texture", std::string{texture}},
                      {"origin", std::format("{} {} {}", origin.x, origin.y, origin.z)}}};
    entity.keyValues.insert(entity.keyValues.end(), keys.begin(), keys.end());
    return entity;
}

TEST(StaticDecalsTest, EachLiesOnWhatIsSolidAtItsOrigin) {
    nx::test::SolidsWorld solids{{}};
    const std::vector entities{
        infodecal("{sitea", {.x = -2.0F}), nx::assets::BspEntity{.keyValues = {{"classname", "info_target"}}},
        infodecal("{target", {.x = 98.0F, .y = 10.0F}), infodecal("{arrow_r", {.x = 80.0F})};
    const std::vector<nx::game::StaticDecal> decals =
        nx::game::staticDecalsOf(entities, solids.m_world, solids.m_hulls);
    ASSERT_EQ(decals.size(), 3U) << "the infodecals alone, in their order";
    EXPECT_EQ(decals[0].texture, "{sitea");
    EXPECT_EQ(decals[0].origin.x, -2.0F);
    EXPECT_EQ(decals[0].entity, std::optional{solids.m_block}) << "the block, 2 units ahead";
    EXPECT_EQ(decals[1].texture, "{target");
    EXPECT_EQ(decals[1].origin.y, 10.0F);
    EXPECT_FALSE(decals[1].entity.has_value()) << "the wall, the world's";
    EXPECT_FALSE(decals[2].entity.has_value()) << "nothing within 5 units: the world's too";
}

TEST(StaticDecalsTest, ANamedOneOrOneNotForDeathmatchIsLeftOut) {
    nx::test::SolidsWorld solids{{}};
    const std::vector entities{infodecal("{scorch1", {.x = -2.0F}, {{"targetname", "boom"}}),
                               infodecal("{scorch2", {.x = -2.0F}, {{"spawnflags", "2048"}}),
                               infodecal("{scorch3", {.x = -2.0F}, {{"spawnflags", "1"}})};
    const std::vector<nx::game::StaticDecal> decals =
        nx::game::staticDecalsOf(entities, solids.m_world, solids.m_hulls);
    ASSERT_EQ(decals.size(), 1U);
    EXPECT_EQ(decals[0].texture, "{scorch3");
}

} // namespace
