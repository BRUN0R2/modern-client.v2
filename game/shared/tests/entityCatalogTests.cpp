// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>
import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.assets;
import cs.shared;
#include "entityFixtures.hpp"

TEST(EntityCatalog, ValidatesAnimationNamesAndDeduplicatesModelsAcrossTypes) {
    nx::test::TemporaryDirectory directory;
    nx::test::writeEntityModel(directory);
    auto vfs = nx::assets::Vfs::create(std::array{directory.path()});
    ASSERT_TRUE(vfs.has_value());
    cs::EntityCatalog catalog;
    auto definition = nx::test::entityDefinition();
    auto first = catalog.add(*vfs, definition, cs::ModelTextures::Read);
    ASSERT_TRUE(first.has_value()) << (first ? "" : nx::describe(first.error()));
    definition.id = "anotherEntity";
    auto second = catalog.add(*vfs, definition, cs::ModelTextures::Read);
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(first->value, second->value);
    EXPECT_EQ(catalog.modelCount(), 1U);
    EXPECT_EQ(catalog.type(*first).model, catalog.type(*second).model);
    definition.id = "invalidEntity";
    definition.attackSequence = "missing";
    auto invalid = catalog.add(*vfs, definition, cs::ModelTextures::Read);
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, cs::EntityError::UnknownSequence);
    EXPECT_EQ(catalog.size(), 2U);
    definition.id = "loopingDeath";
    definition.attackSequence = "wave";
    definition.deathSequence = "idle";
    const auto looping = catalog.add(*vfs, definition, cs::ModelTextures::Read);
    ASSERT_FALSE(looping.has_value());
    EXPECT_EQ(looping.error().code, cs::EntityError::InvalidDefinition);
    EXPECT_EQ(catalog.size(), 2U);
    definition.id = "loopingAttack";
    definition.attackSequence = "idle";
    definition.deathSequence = "wave";
    EXPECT_TRUE(catalog.add(*vfs, definition, cs::ModelTextures::Read).has_value());
}

TEST(EntityCatalog, RejectsInvalidStatsAndDuplicateIdsWithoutChangingLiveDefinitions) {
    nx::test::TemporaryDirectory directory;
    nx::test::writeEntityModel(directory);
    auto vfs = nx::assets::Vfs::create(std::array{directory.path()});
    ASSERT_TRUE(vfs.has_value());
    cs::EntityCatalog catalog;
    auto definition = nx::test::entityDefinition();
    ASSERT_TRUE(catalog.add(*vfs, definition, cs::ModelTextures::Read).has_value());
    EXPECT_FALSE(catalog.add(*vfs, definition, cs::ModelTextures::Read).has_value());
    definition.id = "invalid";
    definition.health = nx::HitPoints{std::numeric_limits<float>::infinity()};
    EXPECT_FALSE(catalog.add(*vfs, definition, cs::ModelTextures::Read).has_value());
    EXPECT_EQ(catalog.size(), 1U);
}
