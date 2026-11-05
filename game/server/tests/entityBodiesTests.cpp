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

/// A floor at z = 0, on which each of a body's boxes stands.
[[nodiscard]] auto floorHulls() -> nx::collision::BrushHulls {
    using nx::collision::Contents;
    using nx::collision::HullChild;
    using nx::collision::HullNode;
    using nx::collision::HullNodeIndex;
    const nx::Plane standingOrigin{.normal = {.z = 1.0F}, .distance = -cs::kStandingBox.min.z};
    const nx::Plane duckedOrigin{.normal = {.z = 1.0F}, .distance = -cs::kDuckedBox.min.z};
    // A last node none is rooted at: movement takes a hull rooted at the last for an empty one.
    auto created = nx::collision::BrushHulls::create(
        {.planes = {standingOrigin, duckedOrigin},
         .clipNodes = {HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Solid}},
                       HullNode{.plane = {1}, .children = {Contents::Empty, Contents::Solid}},
                       HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Empty}}},
         .models = {nx::collision::BrushModel{
             .hulls = nx::collision::HullRoots{{HullChild{Contents::Empty}, HullChild{HullNodeIndex{0}},
                                                HullChild{Contents::Empty}, HullChild{HullNodeIndex{1}}}}}}});
    EXPECT_TRUE(created.has_value());
    return std::move(created).value();
}

TEST_F(EntityBodyTest, ABodyDucksAsItsCommandHoldsItsKeyAndStandsUpWhenItLetsGo) {
    constexpr int kTicksDown = 50; // a duck takes 0.4 seconds
    auto files = nx::assets::Vfs::create(std::array{directory.path()});
    ASSERT_TRUE(files.has_value());
    cs::EntityDefinition definition = nx::test::entityDefinition();
    definition.id = "crouching";
    definition.crouchIdleSequence = "wave";
    auto crouching = catalog.add(*files, definition, cs::ModelTextures::Skipped);
    ASSERT_TRUE(crouching.has_value());
    const nx::collision::BrushHulls floor = floorHulls();
    auto made = cs::spawnEntityBody(world, floor, *crouching, {.origin = {.z = 64.0F}});
    ASSERT_TRUE(made.has_value());
    const nx::game::Transform& placed = *world.get<nx::game::Transform>(*made);
    const float standing = placed.origin.z;
    const nx::game::BoxSolidity& solidity = *world.get<nx::game::BoxSolidity>(*made);
    const cs::EntityBody& body = *world.get<cs::EntityBody>(*made);
    const cs::EntitySequences& sequences = catalog.type(*crouching).sequences;
    const auto run = [&](nx::Flags<cs::Button> buttons) {
        const std::array commands{cs::EntityCommand{.entity = *made, .command = {.buttons = buttons}}};
        std::ignore = cs::runEntityBodies(world, floor, commands);
        nx::game::advanceTick(world);
    };
    // A line across, above a ducked body's box and within a standing one's.
    const auto meetsItsTop = [&] {
        const nx::Vec3 across{.x = 64.0F};
        const nx::Vec3 height{.z = standing + cs::kDuckedBox.max.z};
        const nx::Aabb traced{.min = {.z = -1.0F}, .max = {.z = 1.0F}};
        return world.get<nx::game::ClipMethod>(*made)
                   ->clip(world, *made, traced, height - across, height + across)
                   .fraction < 1.0F;
    };
    EXPECT_TRUE(meetsItsTop());
    for (int tick = 0; tick < kTicksDown; ++tick) {
        run({cs::Button::Duck});
    }
    EXPECT_TRUE(body.movement.isDucked);
    EXPECT_NEAR(placed.origin.z, standing + cs::kStandingBox.min.z - cs::kDuckedBox.min.z, 0.1F)
        << "its origin lower, over the same feet";
    EXPECT_EQ(solidity.box.max.z, cs::kDuckedBox.max.z);
    EXPECT_EQ(world.get<nx::game::Body>(*made)->box.max.z, cs::kDuckedBox.max.z);
    EXPECT_TRUE(cs::entityStatesOf(world).front().isDucked);
    EXPECT_TRUE(cs::entityStatesOf(world).front().isOnGround);
    EXPECT_EQ(body.animation.sequence, sequences.crouchIdle);
    EXPECT_FALSE(meetsItsTop());
    run({});
    EXPECT_FALSE(body.movement.isDucked);
    EXPECT_NEAR(placed.origin.z, standing, 0.1F);
    EXPECT_EQ(solidity.box.max.z, cs::kStandingBox.max.z);
    EXPECT_FALSE(cs::entityStatesOf(world).front().isDucked);
    EXPECT_EQ(body.animation.sequence, sequences.idle);
}

TEST_F(EntityBodyTest, ABodyWalksDuckedAtAThirdOfItsSpeedButNeverTooSlowToGoAtAll) {
    constexpr int kTicks = 300;
    constexpr float kFastSpeed = 300.0F;
    auto files = nx::assets::Vfs::create(std::array{directory.path()});
    ASSERT_TRUE(files.has_value());
    cs::EntityDefinition definition = nx::test::entityDefinition();
    definition.id = "fast";
    definition.speed = nx::UnitsPerSecond{kFastSpeed};
    auto fast = catalog.add(*files, definition, cs::ModelTextures::Skipped);
    ASSERT_TRUE(fast.has_value());
    const nx::collision::BrushHulls floor = floorHulls();
    const auto speedDucked = [&](cs::EntityTypeIndex walking, float walk) {
        auto made = cs::spawnEntityBody(world, floor, walking, {.origin = {.z = 64.0F}});
        EXPECT_TRUE(made.has_value());
        const std::array commands{cs::EntityCommand{
            .entity = *made,
            .command = {.move = {.forward = nx::UnitsPerSecond{walk}}, .buttons = {cs::Button::Duck}}}};
        for (int tick = 0; tick < kTicks; ++tick) {
            std::ignore = cs::runEntityBodies(world, floor, commands);
            nx::game::advanceTick(world);
        }
        return world.get<cs::EntityBody>(*made)->movement.velocity.x;
    };
    constexpr float kLeastDuckedWalk = 80.0F;
    constexpr float kTolerance = 0.5F;
    EXPECT_NEAR(speedDucked(type, nx::test::entityDefinition().speed.value()), kLeastDuckedWalk, kTolerance)
        << "a third of 160 gained it 2.7 units a second";
    EXPECT_NEAR(speedDucked(*fast, kFastSpeed), kFastSpeed * 0.333F, kTolerance);
    EXPECT_NEAR(speedDucked(*fast, 100.0F), kLeastDuckedWalk, kTolerance) << "by the walk it asks";
}

TEST_F(EntityBodyTest, ABodyFarFromEveryPlayerMovesLessOftenAndAsFarInLongerSteps) {
    constexpr std::uint32_t kTicks = 160;
    constexpr float kFar = 8000.0F;
    constexpr std::uint32_t kFarthestTicksPerMove = 8;
    constexpr float kShare = 0.03F;
    auto near = cs::spawnEntityBody(world, *hulls, type, {});
    auto far = cs::spawnEntityBody(world, *hulls, type, {.origin = {.y = kFar}});
    ASSERT_TRUE(near.has_value() && far.has_value());
    const nx::Vec3 nearStart = world.get<nx::game::Transform>(*near)->origin;
    const nx::Vec3 farStart = world.get<nx::game::Transform>(*far)->origin;
    const std::array walks{
        cs::EntityCommand{.entity = *near, .command = {.move = {.forward = nx::UnitsPerSecond{100}}}},
        cs::EntityCommand{.entity = *far, .command = {.move = {.forward = nx::UnitsPerSecond{100}}}}};
    std::uint32_t farMoves = 0;
    nx::GameTime lastMovedAt = world.get<cs::EntityBody>(*far)->movedAt;
    for (std::uint32_t tick = 0; tick < kTicks; ++tick) {
        // A player beside the one, so that it stays near as it walks.
        const std::array watchers{world.get<nx::game::Transform>(*near)->origin};
        std::ignore = cs::runEntityBodies(world, *hulls, walks, nullptr, watchers);
        const nx::GameTime movedAt = world.get<cs::EntityBody>(*far)->movedAt;
        farMoves += movedAt != lastMovedAt ? 1U : 0U;
        lastMovedAt = movedAt;
        nx::game::advanceTick(world);
    }
    EXPECT_EQ(world.get<cs::EntityBody>(*near)->ticksPerMove, 1U);
    EXPECT_EQ(world.get<cs::EntityBody>(*far)->ticksPerMove, kFarthestTicksPerMove);
    EXPECT_LE(farMoves, (kTicks / kFarthestTicksPerMove) + 1) << "at a tick of its own, every so many";
    const float nearWent = nx::length(world.get<nx::game::Transform>(*near)->origin - nearStart);
    const float farWent = nx::length(world.get<nx::game::Transform>(*far)->origin - farStart);
    EXPECT_GT(nearWent, 0.0F);
    EXPECT_NEAR(farWent, nearWent, nearWent * kShare) << "as far in the same time";
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
    const auto isKnockedBack = [&] {
        const std::vector<cs::EntityState> states = cs::entityStatesOf(world);
        const auto found = std::ranges::find(states, *made, &cs::EntityState::entity);
        return found != states.end() && found->isKnockedBack;
    };
    EXPECT_TRUE(isKnockedBack());
    EXPECT_FLOAT_EQ(shareAfter(10), 0.5F) << "past its stagger, at half its speed";
    EXPECT_TRUE(isKnockedBack()) << "still slowed";
    EXPECT_FLOAT_EQ(shareAfter(50), 1.0F) << "back to the whole of it";
    EXPECT_FALSE(isKnockedBack());
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
