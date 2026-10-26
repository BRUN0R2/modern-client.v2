// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// A map as the game plays it: the spawn points of each team and the team a player first stands with,
// what its rounds are played for, its hostages as the game reads them, and the map read whole, the BSP
// seed of the fuzz corpus given other entities.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.assets;
import cs.shared;

#include "boxMap.hpp"

namespace {

/// A map of the BSP seed whose entities are `entities`, the text of its entities lump.
[[nodiscard]] auto gameMapWith(std::string_view entities) -> std::unique_ptr<cs::GameMap> {
    auto map = cs::GameMap::read(nx::test::boxMapWith(entities), "box");
    EXPECT_TRUE(map.has_value()) << (map ? "" : nx::describe(map.error()));
    return std::move(map).value_or(nullptr);
}

TEST(SpawnPointTest, EachTeamsAreItsClassesWithAnOriginInTheirOrder) {
    const auto map = gameMapWith(R"({ "classname" "worldspawn" }
{ "classname" "info_player_deathmatch" "origin" "1 2 3" "angles" "0 90 0" }
{ "classname" "info_player_start" "origin" "4 5 6" }
{ "classname" "info_player_deathmatch" "origin" "0 0 0" }
{ "classname" "info_player_deathmatch" }
{ "classname" "info_player_deathmatch" "origin" "7 8 9" "angles" "10 180 0" })");
    ASSERT_NE(map, nullptr);
    const std::vector<cs::SpawnPoint> terrorists{
        {.origin = {.x = 1.0F, .y = 2.0F, .z = 3.0F}, .yaw = nx::Degrees{90.0F}},
        {.origin = {.x = 7.0F, .y = 8.0F, .z = 9.0F}, .yaw = nx::Degrees{180.0F}}};
    EXPECT_EQ(cs::spawnPointsOf(map->map(), cs::Team::Terrorists), terrorists);
    EXPECT_TRUE(std::ranges::equal(map->spawnPoints(cs::Team::Terrorists), terrorists));
    const std::vector<cs::SpawnPoint> counterTerrorists{{.origin = {.x = 4.0F, .y = 5.0F, .z = 6.0F}}};
    EXPECT_EQ(cs::spawnPointsOf(map->map(), cs::Team::CounterTerrorists), counterTerrorists);
    // A player first stands at the first counter-terrorist spawn point.
    EXPECT_EQ(cs::spawnTeam(map->map()), cs::Team::CounterTerrorists);
    const std::optional<cs::TeamSpawn> first = cs::firstSpawnOf(map->map());
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->point, counterTerrorists.front());
}

TEST(SpawnPointTest, WithoutCounterTerroristsAPlayerFirstStandsWithTheTerrorists) {
    const auto map = gameMapWith(R"({ "classname" "worldspawn" }
{ "classname" "info_player_deathmatch" "origin" "1 2 3" "angles" "0 90 0" })");
    ASSERT_NE(map, nullptr);
    EXPECT_EQ(cs::spawnTeam(map->map()), cs::Team::Terrorists);
    const auto none = gameMapWith(R"({ "classname" "worldspawn" })");
    ASSERT_NE(none, nullptr);
    EXPECT_FALSE(cs::firstSpawnOf(none->map()).has_value());
    EXPECT_EQ(cs::spawnTeam(none->map()), cs::Team::CounterTerrorists);
}

TEST(MapObjectiveTest, AMapIsPlayedForWhatItsEntitiesAre) {
    const auto bomb = gameMapWith(R"({ "classname" "worldspawn" }
{ "classname" "func_buyzone" }
{ "classname" "info_bomb_target" "origin" "1 2 3" }
{ "classname" "hostage_entity" })");
    ASSERT_NE(bomb, nullptr);
    EXPECT_EQ(bomb->objectives(),
              (cs::MapObjectives{cs::MapObjective::BombTarget, cs::MapObjective::Hostages}));
    EXPECT_EQ(bomb->hostages(), 1U);
    const auto none = gameMapWith(R"({ "classname" "worldspawn" }
{ "classname" "info_player_start" "origin" "1 2 3" })");
    ASSERT_NE(none, nullptr);
    EXPECT_EQ(none->objectives(), cs::MapObjectives{});
    EXPECT_EQ(none->hostages(), 0U);
}

TEST(MapHostageTest, TheGameReadsEachHostageAsItsEntityNamesIt) {
    const auto map = gameMapWith(R"({ "classname" "worldspawn" }
{ "classname" "hostage_entity" "model" "/models/hostage.mdl" "origin" "1 2 3" "angle" "270" "skin" "2" }
{ "classname" "info_player_start" "origin" "4 5 6" }
{ "classname" "hostage_entity" "skin" "-1" })");
    ASSERT_NE(map, nullptr);
    const std::vector<cs::MapHostage> hostages = cs::mapHostagesOf(map->map());
    ASSERT_EQ(hostages.size(), 2U);
    EXPECT_EQ(hostages[0].model, "/models/hostage.mdl");
    EXPECT_EQ(hostages[0].origin, (nx::Vec3{.x = 1.0F, .y = 2.0F, .z = 3.0F}));
    EXPECT_FLOAT_EQ(hostages[0].angles.yaw.value(), 270.0F) << "\"angle\" is the yaw";
    EXPECT_EQ(hostages[0].skin, 2U);
    EXPECT_EQ(hostages[1].model, cs::kDefaultHostageModel) << "a hostage without a model";
    EXPECT_EQ(hostages[1].skin, 0U) << "a negative skin is 0";
}

TEST(GameMapTest, AMapThatIsNotABspIsUnreadable) {
    const std::array bytes{std::byte{1}, std::byte{2}, std::byte{3}};
    const auto map = cs::GameMap::read({bytes.begin(), bytes.end()}, "broken");
    ASSERT_FALSE(map.has_value());
    EXPECT_EQ(map.error().code, cs::GameMapError::Unreadable);
}

TEST(GameMapTest, ATextureOfNoMaterialIsConcrete) {
    const auto map = gameMapWith(R"({ "classname" "worldspawn" })");
    ASSERT_NE(map, nullptr);
    EXPECT_EQ(map->name(), "box");
    // Down onto the floor, whose texture materials.txt does not tag: concrete, as without a face.
    EXPECT_EQ(map->materialAlong(cs::TextureMaterials{}, nx::assets::kWorldModel,
                                 {.x = 32.0F, .y = 32.0F, .z = 32.0F}, {.x = 32.0F, .y = 32.0F, .z = -32.0F}),
              cs::TextureMaterial::Concrete);
}

} // namespace
