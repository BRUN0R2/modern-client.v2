// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>
import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.assets;
import nx.ecs;
import nx.collision;
import nx.game;
import cs.shared;
import cs.server;
#include "entityFixtures.hpp"

namespace {
class EntityBodyTest : public ::testing::Test {
protected:
    void SetUp() override {
        nx::test::writeEntityModel(directory);
        auto files = nx::assets::Vfs::create(std::array{directory.path()});
        ASSERT_TRUE(files.has_value());
        auto read = catalog.add(*files, nx::test::entityDefinition(), cs::ModelTextures::Skipped);
        ASSERT_TRUE(read.has_value()) << (read ? "" : nx::describe(read.error()));
        type = *read;
        ASSERT_TRUE(nx::game::registerComponents(world).has_value());
        ASSERT_TRUE(cs::registerEntityBodies(world, catalog).has_value());
        const nx::collision::Contents empty = nx::collision::Contents::Empty;
        auto created = nx::collision::BrushHulls::create(
            {.models = {nx::collision::BrushModel{
                 .hulls = nx::collision::HullRoots{{empty, empty, empty, empty}}}}});
        ASSERT_TRUE(created.has_value());
        hulls.emplace(std::move(*created));
    }
    nx::test::TemporaryDirectory directory;
    cs::EntityCatalog catalog;
    cs::EntityTypeIndex type{};
    nx::ecs::World world;
    std::optional<nx::collision::BrushHulls> hulls;
};

TEST_F(EntityBodyTest, DamageUsesTheHitGroupAndDeathRemovesSolidityOnce) {
    auto made = cs::spawnEntityBody(world, *hulls, type, {});
    ASSERT_TRUE(made.has_value());
    const auto first =
        nx::game::attackEntity(world, *made, {.damage = nx::Damage{20}, .trace = {.hitGroup = 1}});
    EXPECT_EQ(first.damage, nx::Damage{40});
    EXPECT_FALSE(first.isKilled);
    auto states = cs::entityStatesOf(world);
    ASSERT_EQ(states.size(), 1U);
    EXPECT_EQ(states[0].health, nx::HitPoints{60});
    const auto killed = nx::game::attackEntity(world, *made, {.damage = nx::Damage{100}});
    EXPECT_EQ(killed.damage, nx::Damage{60});
    EXPECT_TRUE(killed.isKilled);
    EXPECT_FALSE(world.has<nx::game::BoxSolidity>(*made));
    EXPECT_FALSE(world.get<nx::game::Body>(*made)->isPushed);
    EXPECT_EQ(nx::game::attackEntity(world, *made, {.damage = nx::Damage{100}}).damage, nx::Damage{});
}

TEST_F(EntityBodyTest, AHitKnocksABodyOfAKnockbackBackHoldingItsWalkThenSlowingIt) {
    auto files = nx::assets::Vfs::create(std::array{directory.path()});
    ASSERT_TRUE(files.has_value());
    cs::EntityDefinition definition = nx::test::entityDefinition();
    definition.id = "knockedBack";
    definition.knockback = cs::EntityKnockback{.pushPerDamage = nx::UnitsPerSecond{4},
                                               .mostPushSpeed = nx::UnitsPerSecond{300},
                                               .staggerTime = nx::Seconds{0.1F},
                                               .slowShare = 0.5F,
                                               .slowRecovery = nx::Seconds{0.5F}};
    auto knocked = catalog.add(*files, definition, cs::ModelTextures::Skipped);
    ASSERT_TRUE(knocked.has_value()) << (knocked ? "" : nx::describe(knocked.error()));
    auto made = cs::spawnEntityBody(world, *hulls, *knocked, {});
    ASSERT_TRUE(made.has_value());
    auto still = cs::spawnEntityBody(world, *hulls, type, {.origin = {.y = 200}});
    ASSERT_TRUE(still.has_value());
    // Shot along x, 20 points dealt in the generic group: pushed at 80 units a second the way the shot went.
    const nx::Vec3 away{.x = 1.0F};
    ASSERT_FALSE(
        nx::game::attackEntity(world, *made, {.damage = nx::Damage{20}, .direction = away}).isKilled);
    ASSERT_FALSE(
        nx::game::attackEntity(world, *still, {.damage = nx::Damage{20}, .direction = away}).isKilled);
    EXPECT_FLOAT_EQ(world.get<cs::EntityBody>(*made)->movement.velocity.x, 80.0F);
    EXPECT_EQ(world.get<cs::EntityBody>(*still)->movement.velocity, nx::Vec3{}) << "a type of no knockback";
    // Walking back at the shooter, it is carried away, held back while it is staggered.
    const auto walkBack = [&](nx::ecs::Entity entity) {
        return cs::EntityCommand{.entity = entity,
                                 .command = {.move = {.forward = nx::UnitsPerSecond{-160}}}};
    };
    std::ignore = cs::runEntityBodies(world, *hulls, std::array{walkBack(*made), walkBack(*still)});
    EXPECT_NEAR(world.get<cs::EntityBody>(*made)->movement.velocity.x, 80.0F, 1e-3F)
        << "its walk held back, the push carries it";
    EXPECT_LT(world.get<cs::EntityBody>(*still)->movement.velocity.x, 0.0F) << "walking back";
    const auto shareAfter = [&](int ticks) {
        for (int tick = 0; tick < ticks; ++tick) {
            nx::game::advanceTick(world);
            std::ignore = cs::runEntityBodies(world, *hulls, std::array{walkBack(*made)});
        }
        const nx::game::TickTime time = nx::game::tickTimeOf(world);
        return cs::walkShareOf(*definition.knockback, world.get<cs::EntityBody>(*made)->knockedBack, time.now,
                               time.rate);
    };
    EXPECT_FLOAT_EQ(shareAfter(10), 0.5F) << "past its stagger, at half its speed";
    EXPECT_FLOAT_EQ(shareAfter(50), 1.0F) << "back to the whole of it";
}

TEST_F(EntityBodyTest, RetiredGenerationsNeverControlAReplacementBody) {
    auto first = cs::spawnEntityBody(world, *hulls, type, {});
    ASSERT_TRUE(first.has_value());
    const auto retired = *first;
    world.destroy(retired);
    const nx::Degrees restingYaw{90};
    auto second = cs::spawnEntityBody(world, *hulls, type, {.angles = {.yaw = restingYaw}});
    ASSERT_TRUE(second.has_value());
    const std::array commands{
        cs::EntityCommand{.entity = retired, .command = {.move = {.forward = nx::UnitsPerSecond{160}}}}};
    EXPECT_TRUE(cs::runEntityBodies(world, *hulls, commands).attacks.empty());
    EXPECT_EQ(world.get<cs::EntityBody>(*second)->movement.origin.x, 0);
    EXPECT_EQ(world.get<nx::game::Transform>(*second)->angles.yaw, restingYaw);
    EXPECT_NE(retired, *second);
}

TEST_F(EntityBodyTest, ALoopingAttackReturnsToRestWhenItsIntervalEnds) {
    auto files = nx::assets::Vfs::create(std::array{directory.path()});
    ASSERT_TRUE(files.has_value());
    auto definition = nx::test::entityDefinition();
    definition.id = "loopingAttack";
    definition.attackSequence = "idle";
    // The animation outlasts the cooldown: finishing its cycle cannot release the body yet.
    definition.meleeInterval = nx::tickLengthOf(nx::game::tickTimeOf(world).rate);
    auto looping = catalog.add(*files, definition, cs::ModelTextures::Skipped);
    ASSERT_TRUE(looping.has_value());
    auto made = cs::spawnEntityBody(world, *hulls, *looping, {});
    ASSERT_TRUE(made.has_value());
    cs::UserCommand attack;
    attack.buttons.set(cs::Button::Attack);
    const std::array commands{cs::EntityCommand{.entity = *made, .command = attack}};
    EXPECT_TRUE(cs::runEntityBodies(world, *hulls, commands).attacks.empty());
    EXPECT_EQ(world.get<cs::EntityBody>(*made)->motion, cs::EntityMotion::Attacking);
    const nx::GameTime intervalEnd = world.get<cs::EntityBody>(*made)->attacksAgainAt;
    while (nx::game::tickTimeOf(world).now < intervalEnd) {
        nx::game::advanceTick(world);
    }
    EXPECT_TRUE(cs::runEntityBodies(world, *hulls, {}).attacks.empty());
    EXPECT_NE(world.get<cs::EntityBody>(*made)->motion, cs::EntityMotion::Attacking);
}

TEST_F(EntityBodyTest, RejectsNonfinitePlacementsBeforeSpawning) {
    const auto made =
        cs::spawnEntityBody(world, *hulls, type, {.origin = {.x = std::numeric_limits<float>::quiet_NaN()}});
    ASSERT_FALSE(made.has_value());
    EXPECT_EQ(made.error().code, cs::EntityError::InvalidPlacement);
    EXPECT_TRUE(cs::entityStatesOf(world).empty());
    EXPECT_FALSE(cs::spawnEntityBody(world, *hulls, type, {.angles = {.roll = nx::Degrees{90}}}).has_value());
    EXPECT_TRUE(cs::entityStatesOf(world).empty());
}
} // namespace
