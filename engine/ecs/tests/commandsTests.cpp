// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.ecs;

namespace {

using nx::ecs::Commands;
using nx::ecs::Entity;
using nx::ecs::World;

struct Position {
    float x = 0;
    float y = 0;
};

struct Owner {
    Entity entity;
};

struct Big {
    std::array<std::byte, 20'000> payload{}; // larger than a command page
};

/// Counts its destructions; a moved-from object does not count.
class Tracked {
public:
    explicit Tracked(int& destroyed) noexcept : m_destroyed{&destroyed} {}
    Tracked(Tracked&& other) noexcept : m_destroyed{std::exchange(other.m_destroyed, nullptr)} {}
    Tracked(const Tracked&) = delete;
    auto operator=(const Tracked&) -> Tracked& = delete;
    auto operator=(Tracked&&) -> Tracked& = delete;
    ~Tracked() {
        if (m_destroyed != nullptr) {
            ++*m_destroyed;
        }
    }

private:
    int* m_destroyed;
};

class CommandsTest : public testing::Test {
protected:
    CommandsTest() { registerComponents(world); }

    static void registerComponents(World& target) {
        EXPECT_TRUE(target.registerComponent<Position>("test::Position").has_value());
        EXPECT_TRUE(target.registerComponent<Owner>("test::Owner").has_value());
        EXPECT_TRUE(target.registerComponent<Big>("test::Big").has_value());
        EXPECT_TRUE(target.registerComponent<Tracked>("test::Tracked").has_value());
    }

    World world;
};

TEST_F(CommandsTest, NothingHappensUntilApply) {
    const Entity existing = world.spawn(Position{});
    Commands commands{world};
    const auto pending = commands.spawn(Position{.x = 1, .y = 2});
    commands.destroy(existing);
    commands.remove<Position>(existing);

    EXPECT_EQ(world.entityCount(), 1U);
    EXPECT_TRUE(world.isAlive(existing));
    EXPECT_EQ(commands.resolve(pending), std::nullopt);
}

TEST_F(CommandsTest, ASpawnWithComponentsGoesStraightToItsArchetype) {
    int destroyed = 0;
    Commands commands{world};
    const auto pending = commands.spawn(Position{.x = 3, .y = 4}, Owner{}, Tracked{destroyed});
    const std::size_t archetypesBefore = world.stats().archetypeCount;
    std::ignore = commands.apply(world);

    EXPECT_EQ(world.stats().archetypeCount, archetypesBefore + 1) << "no archetype per added component";
    const auto entity = commands.resolve(pending);
    ASSERT_TRUE(entity.has_value());
    ASSERT_NE(world.get<Position>(*entity), nullptr);
    EXPECT_EQ(world.get<Position>(*entity)->y, 4.0F);
    EXPECT_TRUE(world.has<Owner>(*entity));
    EXPECT_EQ(destroyed, 0) << "the component moved into the world, it was not destroyed";
    world.destroy(*entity);
    EXPECT_EQ(destroyed, 1);
}

TEST_F(CommandsTest, AppliesInRecordingOrder) {
    const Entity existing = world.spawn(Position{.x = 0, .y = 0});
    Commands commands{world};
    const auto spawned = commands.spawn(Position{.x = 1, .y = 2});
    commands.add(existing, Owner{.entity = existing});
    commands.remove<Position>(existing);

    const auto stats = commands.apply(world);
    EXPECT_EQ(stats.applied, 3U); // the spawn (with its Position), Owner, remove
    EXPECT_EQ(stats.skipped, 0U);
    EXPECT_TRUE(commands.isEmpty());

    const auto entity = commands.resolve(spawned);
    ASSERT_TRUE(entity.has_value());
    ASSERT_NE(world.get<Position>(*entity), nullptr);
    EXPECT_EQ(world.get<Position>(*entity)->y, 2);
    EXPECT_FALSE(world.has<Position>(existing));
    EXPECT_EQ(world.get<Owner>(existing)->entity, existing);
}

TEST_F(CommandsTest, SpawnsOfDifferentTypesInterleaveInRecordingOrder) {
    const Entity owner = world.spawn();
    Commands commands{world};
    const auto first = commands.spawn(Position{.x = 1, .y = 0});
    const auto second = commands.spawn(Position{.x = 2, .y = 0});
    const auto owned = commands.spawn(Owner{.entity = owner}, Position{.x = 3, .y = 0});
    const auto bare = commands.spawn();
    const auto last = commands.spawn(Position{.x = 4, .y = 0});

    const auto stats = commands.apply(world);
    EXPECT_EQ(stats.applied, 5U);
    std::vector<Entity> entities;
    for (const auto pending : {first, second, owned, bare, last}) {
        const auto entity = commands.resolve(pending);
        ASSERT_TRUE(entity.has_value());
        entities.push_back(*entity);
    }
    EXPECT_TRUE(std::ranges::is_sorted(entities)) << "ids follow the recording order";
    EXPECT_EQ(world.get<Position>(entities[1])->x, 2);
    EXPECT_EQ(world.get<Position>(entities[2])->x, 3);
    EXPECT_EQ(world.get<Owner>(entities[2])->entity, owner);
    EXPECT_FALSE(world.has<Position>(entities[3]));
    EXPECT_EQ(world.get<Position>(entities[4])->x, 4);
    EXPECT_FALSE(world.has<Owner>(entities[4]));
}

TEST_F(CommandsTest, ComponentOrderInASpawnDoesNotMatter) {
    const Entity owner = world.spawn();
    Commands commands{world};
    const auto first = commands.spawn(Position{.x = 1, .y = 0}, Owner{.entity = owner});
    const auto second = commands.spawn(Owner{.entity = owner}, Position{.x = 2, .y = 0});
    std::ignore = commands.apply(world); // every command applies: nothing targets a dead entity

    EXPECT_EQ(world.get<Position>(*commands.resolve(first))->x, 1);
    EXPECT_EQ(world.get<Position>(*commands.resolve(second))->x, 2);
    EXPECT_EQ(world.get<Owner>(*commands.resolve(second))->entity, owner);
}

TEST_F(CommandsTest, CommandsCanTargetPendingEntities) {
    const Entity grenadeOwner = world.spawn();
    Commands commands{world};
    const auto grenade = commands.spawn();
    commands.add(grenade, Owner{.entity = grenadeOwner});
    commands.add(grenade, Position{.x = 5, .y = 5});
    std::ignore = commands.apply(world);

    const auto entity = commands.resolve(grenade);
    ASSERT_TRUE(entity.has_value());
    EXPECT_EQ(world.get<Owner>(*entity)->entity, grenadeOwner);
    EXPECT_EQ(world.get<Position>(*entity)->x, 5);
}

TEST_F(CommandsTest, CommandsOnADeadEntityAreSkippedAndTheirComponentsDestroyed) {
    int destroyed = 0;
    const Entity doomed = world.spawn();
    Commands commands{world};
    commands.destroy(doomed);
    commands.add(doomed, Tracked{destroyed});
    commands.destroy(doomed);

    const auto stats = commands.apply(world);
    EXPECT_EQ(stats.applied, 1U);
    EXPECT_EQ(stats.skipped, 2U);
    EXPECT_EQ(destroyed, 1);
}

TEST_F(CommandsTest, ADestroyRunSkipsOnlyTheDeadEntities) {
    const Entity first = world.spawn(Position{});
    const Entity second = world.spawn(Position{});
    Commands commands{world};
    commands.destroy(first);
    commands.destroy(first);
    commands.destroy(second);

    const auto stats = commands.apply(world);
    EXPECT_EQ(stats.applied, 2U);
    EXPECT_EQ(stats.skipped, 1U); // first, destroyed the line before
    EXPECT_EQ(world.entityCount(), 0U);
}

TEST_F(CommandsTest, UnappliedComponentsAreDestroyedWithTheBuffer) {
    int destroyed = 0;
    {
        Commands commands{world};
        std::ignore = commands.spawn(Tracked{destroyed});
        std::ignore = commands.spawn(Tracked{destroyed}, Position{});
        std::ignore = commands.spawn(Tracked{destroyed}, Position{});
        commands.add(world.spawn(), Tracked{destroyed});
    }
    EXPECT_EQ(destroyed, 4);
}

TEST_F(CommandsTest, AMovedBufferKeepsItsCommands) {
    int destroyed = 0;
    Commands source{world};
    const auto pending = source.spawn(Tracked{destroyed}, Position{.x = 7, .y = 0});
    Commands moved{std::move(source)};
    Commands assigned{world};
    std::ignore = assigned.spawn(Tracked{destroyed});
    assigned = std::move(moved);
    EXPECT_EQ(destroyed, 1) << "the overwritten buffer destroyed its own component";

    std::ignore = assigned.apply(world); // a spawn always applies
    const auto entity = assigned.resolve(pending);
    ASSERT_TRUE(entity.has_value());
    EXPECT_EQ(world.get<Position>(*entity)->x, 7);
    EXPECT_EQ(destroyed, 1) << "the spawned component moved into the world";
    world.destroy(*entity); // before `destroyed` goes out of scope, unlike the fixture's world
    EXPECT_EQ(destroyed, 2);
}

TEST_F(CommandsTest, ComponentsLargerThanAPageAreRecorded) {
    Commands commands{world};
    Big big;
    big.payload.back() = std::byte{7};
    const auto pending = commands.spawn(std::move(big));
    std::ignore = commands.apply(world);
    EXPECT_EQ(world.get<Big>(*commands.resolve(pending))->payload.back(), std::byte{7});
}

TEST_F(CommandsTest, ManyComponentsSpanSeveralPages) {
    constexpr int kCount = 3000;
    Commands commands{world};
    std::vector<nx::ecs::PendingEntity> pending;
    for (int index = 0; index < kCount; ++index) {
        pending.push_back(commands.spawn(Position{.x = static_cast<float>(index), .y = 0}));
    }
    std::ignore = commands.apply(world);
    for (int index = 0; index < kCount; ++index) {
        const auto entity = commands.resolve(pending[static_cast<std::size_t>(index)]);
        ASSERT_TRUE(entity.has_value());
        ASSERT_EQ(world.get<Position>(*entity)->x, static_cast<float>(index));
    }
}

TEST_F(CommandsTest, EntityIdsDependOnlyOnTheRecordedCommands) {
    auto record = [](World& target) {
        Commands commands{target};
        for (int index = 0; index < 10; ++index) {
            std::ignore = commands.spawn(Position{});
        }
        std::ignore = commands.apply(target);
        std::vector<Entity> entities;
        nx::ecs::Query<Entity, const Position&>{target}.forEach(
            [&entities](Entity entity, const Position&) { entities.push_back(entity); });
        return entities;
    };
    World other;
    registerComponents(other);
    EXPECT_EQ(record(world), record(other));
}

} // namespace
