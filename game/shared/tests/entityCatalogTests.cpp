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

TEST(EntityCatalog, ADuckedBodyPlaysWhatItHasForThatElseWhatItPlaysStanding) {
    nx::test::TemporaryDirectory directory;
    nx::test::writeEntityModel(directory);
    auto vfs = nx::assets::Vfs::create(std::array{directory.path()});
    ASSERT_TRUE(vfs.has_value());
    cs::EntityCatalog catalog;
    auto definition = nx::test::entityDefinition();
    auto standing = catalog.add(*vfs, definition, cs::ModelTextures::Skipped);
    ASSERT_TRUE(standing.has_value());
    EXPECT_EQ(catalog.type(*standing).sequences.crouchIdle, catalog.type(*standing).sequences.idle);
    EXPECT_EQ(catalog.type(*standing).sequences.crouchWalk, catalog.type(*standing).sequences.walk);
    definition.id = "crouching";
    definition.crouchIdleSequence = "wave";
    definition.crouchWalkSequence = "wave";
    auto crouching = catalog.add(*vfs, definition, cs::ModelTextures::Skipped);
    ASSERT_TRUE(crouching.has_value());
    EXPECT_EQ(catalog.type(*crouching).sequences.crouchIdle, catalog.type(*crouching).sequences.attack);
    EXPECT_EQ(catalog.type(*crouching).sequences.crouchWalk, catalog.type(*crouching).sequences.attack);
    definition.id = "missingCrouch";
    definition.crouchWalkSequence = "missing";
    const auto missing = catalog.add(*vfs, definition, cs::ModelTextures::Skipped);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, cs::EntityError::UnknownSequence);
}

TEST(EntityCatalog, AModelIsDrawnItsOwnOffsetDuckedElseTheOneItHasStanding) {
    auto definition = nx::test::entityDefinition();
    definition.modelOffset = {.z = nx::HammerUnits{2.0F}};
    EXPECT_EQ(cs::modelOffsetOf(definition, true).z, 2.0F);
    definition.crouchedModelOffset = cs::EntityModelOffset{.z = nx::HammerUnits{18.0F}};
    EXPECT_EQ(cs::modelOffsetOf(definition, true).z, 18.0F);
    EXPECT_EQ(cs::modelOffsetOf(definition, false).z, 2.0F);

    nx::test::TemporaryDirectory directory;
    nx::test::writeEntityModel(directory);
    auto vfs = nx::assets::Vfs::create(std::array{directory.path()});
    ASSERT_TRUE(vfs.has_value());
    cs::EntityCatalog catalog;
    definition.crouchedModelOffset =
        cs::EntityModelOffset{.z = nx::HammerUnits{std::numeric_limits<float>::infinity()}};
    const auto refused = catalog.add(*vfs, definition, cs::ModelTextures::Skipped);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, cs::EntityError::InvalidDefinition);
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
    definition.health = nx::HitPoints{100};
    definition.knockback = cs::EntityKnockback{.mostPushSpeed = nx::UnitsPerSecond{300}, .slowShare = 1.5F};
    const auto knockback = catalog.add(*vfs, definition, cs::ModelTextures::Read);
    ASSERT_FALSE(knockback.has_value()) << "a slow share past the whole speed";
    EXPECT_EQ(knockback.error().code, cs::EntityError::InvalidDefinition);
    definition.knockback.reset();
    definition.stride = nx::HammerUnits{};
    const auto stride = catalog.add(*vfs, definition, cs::ModelTextures::Read);
    ASSERT_FALSE(stride.has_value()) << "a stride of none";
    EXPECT_EQ(stride.error().code, cs::EntityError::InvalidDefinition);
    EXPECT_EQ(catalog.size(), 1U);
}
