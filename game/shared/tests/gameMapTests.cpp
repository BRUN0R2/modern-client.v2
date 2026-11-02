// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// A map as the game plays it: the spawn points of each team and the team a player first stands with,
// what its rounds are played for, where and whether its players buy, its hostages as the game reads them,
// and the map read whole, the BSP seed of the fuzz corpus given other entities.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.assets;
import nx.game;
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

TEST(BuyZoneTest, AMapsAreItsFuncBuyzonesWithAModelEachOfItsTeamOrOfBoth) {
    const std::vector<std::byte> bytes = nx::test::boxMapWith(R"({ "classname" "worldspawn" }
{ "classname" "func_buyzone" "model" "*1" "team" "1" "origin" "1 2 3" }
{ "classname" "func_buyzone" "model" "*2" "team" "2" }
{ "classname" "func_buyzone" "model" "*3" }
{ "classname" "func_buyzone" "model" "*4" "team" "7" }
{ "classname" "func_buyzone" })");
    auto map = nx::assets::BspView::parse(bytes);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    const std::vector<cs::BuyZone> expected{
        {.model = nx::assets::ModelIndex{1},
         .origin = {.x = 1.0F, .y = 2.0F, .z = 3.0F},
         .team = cs::Team::Terrorists},
        {.model = nx::assets::ModelIndex{2}, .team = cs::Team::CounterTerrorists},
        {.model = nx::assets::ModelIndex{3}},
        {.model = nx::assets::ModelIndex{4}},
    };
    EXPECT_EQ(cs::buyZonesOf(*map), expected);
}

TEST(BuyZoneTest, AZoneOfAModelTheMapLacksMakesTheMapUnreadable) {
    for (const std::string_view model : {"*99", "*0"}) {
        const auto map = cs::GameMap::read(nx::test::boxWithVolumeMapWith(std::format(
                                               R"({{ "classname" "worldspawn" }}
{{ "classname" "func_buyzone" "model" "{}" }})",
                                               model)),
                                           "box");
        ASSERT_FALSE(map.has_value()) << model;
        EXPECT_EQ(map.error().code, cs::GameMapError::Unreadable) << model;
    }
}

/// A map of the seed with a volume, `*1`, about the origin, in func_buyzones of `keys`, and spawn points of
/// each team far from it.
[[nodiscard]] auto mapWithBuyZone(std::string_view keys) -> std::unique_ptr<cs::GameMap> {
    auto map = cs::GameMap::read(nx::test::boxWithVolumeMapWith(std::format(R"({{ "classname" "worldspawn" }}
{{ "classname" "info_player_deathmatch" "origin" "3000 0 0" }}
{{ "classname" "info_player_start" "origin" "0 3000 0" }}
{{ "classname" "func_buyzone" "model" "*1" {} }})",
                                                                            keys)),
                                 "box");
    EXPECT_TRUE(map.has_value()) << (map ? "" : nx::describe(map.error()));
    return std::move(map).value_or(nullptr);
}

constexpr nx::Vec3 kInTheVolume{.x = 16.0F, .y = 16.0F, .z = 8.0F};
constexpr nx::Vec3 kFarFromAll{.x = -3000.0F, .y = -3000.0F, .z = 8.0F};
constexpr nx::HammerUnits kSpawnReach{200.0F};
const nx::game::Body kStanding{.box = cs::kStandingBox};

TEST(BuyZoneTest, APlayerBuysInAZoneOfItsTeamOrOfBothAndNowhereElse) {
    const auto both = mapWithBuyZone("");
    ASSERT_NE(both, nullptr);
    for (const cs::Team team : {cs::Team::Terrorists, cs::Team::CounterTerrorists}) {
        EXPECT_TRUE(both->isBuyingPlace(team, kStanding, kInTheVolume, kSpawnReach));
        EXPECT_FALSE(both->isBuyingPlace(team, kStanding, kFarFromAll, kSpawnReach));
    }
    const nx::Vec3 atASpawnPoint{.x = 3000.0F, .z = 0.0F};
    EXPECT_FALSE(both->isBuyingPlace(cs::Team::Terrorists, kStanding, atASpawnPoint, kSpawnReach))
        << "with zones, a spawn point buys nothing";

    const auto terrorists = mapWithBuyZone(R"("team" "1")");
    ASSERT_NE(terrorists, nullptr);
    EXPECT_TRUE(terrorists->isBuyingPlace(cs::Team::Terrorists, kStanding, kInTheVolume, kSpawnReach));
    EXPECT_FALSE(
        terrorists->isBuyingPlace(cs::Team::CounterTerrorists, kStanding, kInTheVolume, kSpawnReach));
}

TEST(BuyZoneTest, OnAMapWithoutAnyAPlayerBuysNearASpawnPointOfItsTeam) {
    const auto map = gameMapWith(R"({ "classname" "worldspawn" }
{ "classname" "info_player_deathmatch" "origin" "1000 0 0" }
{ "classname" "info_player_start" "origin" "-1000 0 0" })");
    ASSERT_NE(map, nullptr);
    const nx::Vec3 near{.x = 1199.0F};
    const nx::Vec3 far{.x = 1200.0F};
    EXPECT_TRUE(map->isBuyingPlace(cs::Team::Terrorists, kStanding, near, kSpawnReach));
    EXPECT_FALSE(map->isBuyingPlace(cs::Team::Terrorists, kStanding, far, kSpawnReach))
        << "less than its reach";
    EXPECT_FALSE(map->isBuyingPlace(cs::Team::CounterTerrorists, kStanding, near, kSpawnReach));
}

TEST(BuyZoneTest, AMapsParametersSayWhichTeamsBuy) {
    const auto buying = [](std::string_view value, cs::Team team) {
        const auto map = gameMapWith(std::format(R"({{ "classname" "worldspawn" }}
{{ "classname" "info_map_parameters" "buying" "{}" }})",
                                                 value));
        return map != nullptr && map->letsBuy(team);
    };
    for (const std::string_view everyone : {"0", "9"}) {
        EXPECT_TRUE(buying(everyone, cs::Team::Terrorists)) << everyone;
        EXPECT_TRUE(buying(everyone, cs::Team::CounterTerrorists)) << everyone;
    }
    EXPECT_FALSE(buying("1", cs::Team::Terrorists));
    EXPECT_TRUE(buying("1", cs::Team::CounterTerrorists));
    EXPECT_TRUE(buying("2", cs::Team::Terrorists));
    EXPECT_FALSE(buying("2", cs::Team::CounterTerrorists));
    EXPECT_FALSE(buying("3", cs::Team::Terrorists));
    EXPECT_FALSE(buying("3", cs::Team::CounterTerrorists));

    const auto none = gameMapWith(R"({ "classname" "worldspawn" })");
    ASSERT_NE(none, nullptr);
    EXPECT_TRUE(none->letsBuy(cs::Team::Terrorists)) << "without parameters, everyone";
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
