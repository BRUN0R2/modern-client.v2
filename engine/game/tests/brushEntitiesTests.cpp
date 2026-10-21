// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The classes of data/cstrike/entities.toml, and the brush entities made by them.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.ecs;
import nx.assets;
import nx.game;

namespace {

using nx::assets::BspEntity;
using nx::game::BrushModel;
using nx::game::BrushSolidity;
using nx::game::EntityClasses;
using nx::game::GameError;
using nx::game::Solid;
using nx::game::Transform;

[[nodiscard]] auto repositoryClasses() -> EntityClasses {
    const std::filesystem::path file = std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "entities.toml";
    auto text = nx::platform::readTextFile(file);
    EXPECT_TRUE(text.has_value());
    auto classes = nx::game::readEntityClasses(text.value_or(""), "entities.toml");
    EXPECT_TRUE(classes.has_value()) << (classes ? "" : nx::describe(classes.error()));
    return classes.value_or(EntityClasses{});
}

[[nodiscard]] auto entity(std::initializer_list<std::pair<std::string, std::string>> keyValues) -> BspEntity {
    return BspEntity{.keyValues = keyValues};
}

TEST(BrushEntityTest, TheRepositoryDescribesTheBrushClasses) {
    const EntityClasses classes = repositoryClasses();
    const auto find = [&](std::string_view classname) {
        return std::ranges::find(classes.brushClasses, classname, &nx::game::BrushClass::classname);
    };

    ASSERT_NE(find("func_wall"), classes.brushClasses.end());
    EXPECT_EQ(find("func_wall")->solid, Solid::Bsp);
    ASSERT_NE(find("func_ladder"), classes.brushClasses.end());
    EXPECT_EQ(find("func_ladder")->solid, Solid::Not);
    EXPECT_EQ(find("func_ladder")->contents, -16);
    ASSERT_NE(find("func_water"), classes.brushClasses.end());
    EXPECT_EQ(find("func_water")->skinMakesNotSolid, true);
    EXPECT_EQ(find("trigger_multiple"), classes.brushClasses.end()) << "triggers make no brush entity yet";
}

TEST(BrushEntityTest, RefusesBrokenClasses) {
    const auto errorOf = [](std::string_view text) {
        const auto classes = nx::game::readEntityClasses(text, "test.toml");
        return classes ? std::nullopt : std::optional{classes.error().code};
    };

    EXPECT_EQ(errorOf("[[brushClasses]]\nclassname = \"func_wall\"\nsolid = \"bsp\"\n"
                      "[[brushClasses]]\nclassname = \"func_wall\"\nsolid = \"not\"\n"),
              GameError::ConfigInvalid)
        << "twice";
    EXPECT_EQ(errorOf("[[brushClasses]]\nclassname = \"func_ladder\"\nsolid = \"not\"\ncontents = -17\n"),
              GameError::ConfigInvalid)
        << "contents out of range";
    EXPECT_EQ(errorOf("[[brushClasses]]\nclassname = \"func_wall\"\nsolid = \"box\"\n"),
              GameError::ConfigInvalid)
        << "an unknown solid";
}

TEST(BrushEntityTest, MakesTheBrushEntitiesAsTheGameDoes) {
    nx::ecs::World world;
    ASSERT_TRUE(nx::game::registerBrushComponents(world).has_value());
    const std::vector entities{
        entity({{"classname", "worldspawn"}}),
        entity({{"classname", "func_wall"}, {"model", "*1"}, {"origin", "8 16 32"}}),
        entity({{"classname", "func_door"}, {"model", "*2"}, {"spawnflags", "8"}}),
        entity({{"classname", "func_water"}, {"model", "*3"}, {"skin", "-3"}}),
        entity({{"classname", "func_ladder"}, {"model", "*4"}, {"skin", "-3"}}),
        entity({{"classname", "trigger_multiple"}, {"model", "*5"}}),
        entity({{"classname", "info_player_start"}, {"origin", "0 0 0"}}),
    };

    const auto spawned = nx::game::spawnBrushEntities(world, entities, 6, repositoryClasses());
    ASSERT_TRUE(spawned.has_value()) << nx::describe(spawned.error());
    EXPECT_EQ(*spawned, 4U);

    std::vector<std::tuple<std::uint32_t, nx::Vec3, BrushSolidity>> made;
    nx::ecs::Query<const BrushModel&, const Transform&, const BrushSolidity&>{world}.forEach(
        [&](const BrushModel& model, const Transform& transform, const BrushSolidity& solidity) {
            made.emplace_back(model.index, transform.origin, solidity);
        });
    ASSERT_EQ(made.size(), 4U);
    EXPECT_EQ(std::get<0>(made[0]), 1U);
    EXPECT_EQ(std::get<1>(made[0]), (nx::Vec3{.x = 8.0F, .y = 16.0F, .z = 32.0F}));
    EXPECT_EQ(std::get<2>(made[0]).solid, Solid::Bsp);
    EXPECT_EQ(std::get<2>(made[1]).solid, Solid::Not) << "a passable door";
    EXPECT_EQ(std::get<2>(made[2]).solid, Solid::Not) << "water: a door with a skin";
    EXPECT_EQ(std::get<2>(made[2]).contents, -3);
    EXPECT_EQ(std::get<2>(made[3]).solid, Solid::Not);
    EXPECT_EQ(std::get<2>(made[3]).contents, -16) << "a ladder whatever its skin";
}

TEST(BrushEntityTest, ABrushModelTheMapLacksIsAnError) {
    nx::ecs::World world;
    ASSERT_TRUE(nx::game::registerBrushComponents(world).has_value());
    const std::vector entities{entity({{"classname", "func_wall"}, {"model", "*9"}})};

    const auto spawned = nx::game::spawnBrushEntities(world, entities, 6, repositoryClasses());
    ASSERT_FALSE(spawned.has_value());
    EXPECT_EQ(spawned.error().code, GameError::UnknownModel);
}

} // namespace
