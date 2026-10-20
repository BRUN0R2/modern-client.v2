// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.ecs;

#include "contractRecorder.hpp"

namespace {

using nx::ecs::Entity;
using nx::ecs::World;

struct Position {
    float x = 0;
    float y = 0;
};

struct Velocity {
    float dx = 0;
    float dy = 0;
};

struct OnGround {};

struct Unregistered {
    int value = 0;
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

struct Big {
    std::array<std::byte, 20'000> payload{}; // larger than a 16 KiB chunk
};

class WorldTest : public testing::Test {
protected:
    WorldTest() {
        EXPECT_TRUE(world.registerComponent<Position>("test::Position").has_value());
        EXPECT_TRUE(world.registerComponent<Velocity>("test::Velocity").has_value());
        EXPECT_TRUE(world.registerComponent<OnGround>("test::OnGround").has_value());
        EXPECT_TRUE(world.registerComponent<Tracked>("test::Tracked").has_value());
        EXPECT_TRUE(world.registerComponent<Big>("test::Big").has_value());
    }

    World world;
};

TEST_F(WorldTest, SpawnsAnEntityWithoutComponents) {
    const Entity entity = world.spawn();
    EXPECT_TRUE(world.isAlive(entity));
    EXPECT_EQ(world.entityCount(), 1U);
    EXPECT_FALSE(world.has<Position>(entity));
    EXPECT_EQ(world.get<Position>(entity), nullptr);
}

TEST_F(WorldTest, SpawnsAnEntityWithItsComponents) {
    const Entity entity = world.spawn(Position{.x = 1, .y = 2}, Velocity{.dx = 3, .dy = 4}, OnGround{});

    ASSERT_NE(world.get<Position>(entity), nullptr);
    EXPECT_EQ(world.get<Position>(entity)->x, 1);
    EXPECT_EQ(world.get<Velocity>(entity)->dy, 4);
    EXPECT_TRUE(world.has<OnGround>(entity));
}

TEST_F(WorldTest, AddingAndRemovingMovesTheEntityBetweenArchetypes) {
    const Entity entity = world.spawn(Position{.x = 5, .y = 6});
    const auto archetypesBefore = world.archetypeCount();

    world.add(entity, Velocity{.dx = 7, .dy = 8});
    EXPECT_EQ(world.archetypeCount(), archetypesBefore + 1);
    EXPECT_EQ(world.get<Position>(entity)->x, 5); // kept across the move
    EXPECT_EQ(world.get<Velocity>(entity)->dx, 7);

    world.remove<Position>(entity);
    EXPECT_FALSE(world.has<Position>(entity));
    EXPECT_EQ(world.get<Velocity>(entity)->dy, 8);

    world.remove<Position>(entity); // absent: nothing happens
    EXPECT_TRUE(world.has<Velocity>(entity));
}

TEST_F(WorldTest, AddingAComponentTheEntityHasReplacesIt) {
    const Entity entity = world.spawn(Position{.x = 1, .y = 1});
    const auto archetypes = world.archetypeCount();
    world.add(entity, Position{.x = 9, .y = 9});
    EXPECT_EQ(world.get<Position>(entity)->x, 9);
    EXPECT_EQ(world.archetypeCount(), archetypes);
}

TEST_F(WorldTest, DestroyingKeepsTheOtherEntitiesIntact) {
    std::vector<Entity> entities;
    for (int index = 0; index < 5; ++index) {
        entities.push_back(world.spawn(Position{.x = static_cast<float>(index), .y = 0}));
    }
    world.destroy(entities[1]); // the last row moves into its place

    EXPECT_FALSE(world.isAlive(entities[1]));
    for (const int index : {0, 2, 3, 4}) {
        ASSERT_NE(world.get<Position>(entities[static_cast<std::size_t>(index)]), nullptr);
        EXPECT_EQ(world.get<Position>(entities[static_cast<std::size_t>(index)])->x,
                  static_cast<float>(index));
    }
    EXPECT_EQ(world.entityCount(), 4U);
}

TEST_F(WorldTest, EveryComponentIsDestroyedExactlyOnce) {
    int destroyed = 0;
    {
        World local;
        ASSERT_TRUE(local.registerComponent<Tracked>("test::Tracked").has_value());
        ASSERT_TRUE(local.registerComponent<Position>("test::Position").has_value());
        const Entity first = local.spawn(Tracked{destroyed});
        const Entity second = local.spawn(Tracked{destroyed});
        std::ignore = local.spawn(Tracked{destroyed}, Position{});

        local.add(first, Position{}); // relocates the Tracked into another archetype
        local.remove<Position>(first);
        EXPECT_EQ(destroyed, 0);

        local.destroy(second);
        EXPECT_EQ(destroyed, 1);
    } // the world destroys the two left
    EXPECT_EQ(destroyed, 3);
}

TEST_F(WorldTest, ManyEntitiesFillSeveralChunksThatReturnToThePool) {
    constexpr int kCount = 10'000;
    std::vector<Entity> entities;
    entities.reserve(kCount);
    for (int index = 0; index < kCount; ++index) {
        entities.push_back(world.spawn(Position{.x = static_cast<float>(index), .y = 0}));
    }
    EXPECT_GT(world.stats().chunkCount, 1U);

    for (const Entity entity : entities) {
        world.destroy(entity);
    }
    const auto stats = world.stats();
    EXPECT_EQ(stats.entityCount, 0U);
    EXPECT_EQ(stats.chunkCount, 0U);
    EXPECT_GT(stats.pooledChunkCount, 0U);
}

TEST_F(WorldTest, AComponentLargerThanAChunkStillFits) {
    const Entity entity = world.spawn(Big{});
    ASSERT_NE(world.get<Big>(entity), nullptr);
    world.get<Big>(entity)->payload.back() = std::byte{42};
    const Entity other = world.spawn(Big{});
    EXPECT_EQ(world.get<Big>(entity)->payload.back(), std::byte{42});
    EXPECT_EQ(world.get<Big>(other)->payload.back(), std::byte{0});
}

TEST_F(WorldTest, AnUntypedSpawnPlacesABatchInOneArchetype) {
    const auto position = *world.components().find<Position>();
    const auto velocity = *world.components().find<Velocity>();
    const auto onGround = *world.components().find<OnGround>();
    std::array types{velocity, onGround, position}; // any order
    std::array positions{Position{.x = 1, .y = 0}, Position{.x = 2, .y = 0}};
    std::array velocities{Velocity{.dx = 3, .dy = 0}, Velocity{.dx = 4, .dy = 0}};
    std::array<void*, types.size() * 2> sources{}; // the tag has no data: nullptr
    for (std::size_t entity = 0; entity < 2; ++entity) {
        for (std::size_t component = 0; component < types.size(); ++component) {
            void*& source = sources[(entity * types.size()) + component];
            if (types[component] == position) {
                source = &positions[entity];
            } else if (types[component] == velocity) {
                source = &velocities[entity];
            }
        }
    }
    const std::size_t archetypesBefore = world.stats().archetypeCount;
    std::array<Entity, 2> spawned{};
    world.spawnRelocated(types, sources, spawned);

    EXPECT_EQ(world.stats().archetypeCount, archetypesBefore + 1);
    EXPECT_LT(spawned[0], spawned[1]);
    EXPECT_EQ(world.get<Position>(spawned[0])->x, 1);
    EXPECT_EQ(world.get<Velocity>(spawned[0])->dx, 3);
    EXPECT_EQ(world.get<Position>(spawned[1])->x, 2);
    EXPECT_EQ(world.get<Velocity>(spawned[1])->dx, 4);
    EXPECT_TRUE(world.has<OnGround>(spawned[1]));
}

TEST_F(WorldTest, AnUntypedSpawnBatchFillsChunkAfterChunk) {
    constexpr std::size_t kCount = 5'000; // several 16 KiB chunks of {Entity, Position}
    const std::array types{*world.components().find<Position>()};
    const Entity before = world.spawn(Position{.x = -1, .y = 0}); // the batch starts mid-chunk
    std::vector<Position> positions(kCount);
    std::vector<void*> sources(kCount);
    for (std::size_t index = 0; index < kCount; ++index) {
        positions[index] = Position{.x = static_cast<float>(index), .y = 0};
        sources[index] = &positions[index];
    }
    std::vector<Entity> spawned(kCount);
    world.spawnRelocated(types, sources, spawned);

    EXPECT_GT(world.stats().chunkCount, 2U);
    EXPECT_EQ(world.get<Position>(before)->x, -1);
    for (std::size_t index = 0; index < kCount; ++index) {
        ASSERT_EQ(world.get<Position>(spawned[index])->x, static_cast<float>(index)) << index;
    }
    std::size_t visited = 0;
    nx::ecs::Query<Entity, const Position&>{world}.forEach(
        [&visited](Entity, const Position&) { ++visited; });
    EXPECT_EQ(visited, kCount + 1);
}

TEST_F(WorldTest, AnUntypedSpawnRejectsARepeatedType) {
    const auto position = *world.components().find<Position>();
    const std::array repeated{position, position};
    Position first{};
    Position second{};
    const std::array<void*, 2> sources{&first, &second};
    std::array<Entity, 1> spawned{};

    const nx_test::ContractRecorder recorder;
    world.spawnRelocated(repeated, sources, spawned);
    EXPECT_EQ(nx_test::ContractRecorder::count(), 1);
    EXPECT_EQ(world.entityCount(), 0U);
}

TEST_F(WorldTest, AStaleHandleNamesNothing) {
    const Entity entity = world.spawn(Position{.x = 1, .y = 1});
    world.destroy(entity);
    const Entity reused = world.spawn(Position{.x = 2, .y = 2});
    ASSERT_EQ(reused.index(), entity.index());

    EXPECT_FALSE(world.isAlive(entity));
    EXPECT_EQ(world.get<Position>(entity), nullptr);
    EXPECT_EQ(world.get<Position>(reused)->x, 2);
}

TEST_F(WorldTest, ConstAccessReadsTheComponent) {
    const Entity entity = world.spawn(Position{.x = 3, .y = 4});
    const World& view = world;
    ASSERT_NE(view.get<Position>(entity), nullptr);
    EXPECT_EQ(view.get<Position>(entity)->y, 4);
}

TEST_F(WorldTest, AnUnregisteredComponentIsAViolation) {
    const Entity entity = world.spawn(Position{});
    const nx_test::ContractRecorder recorder;
    world.add(entity, Unregistered{});
    EXPECT_EQ(nx_test::ContractRecorder::count(), 1);
    EXPECT_FALSE(world.has<Unregistered>(entity));
    EXPECT_TRUE(world.has<Position>(entity));
}

TEST_F(WorldTest, ChangingADeadEntityIsAViolation) {
    const Entity entity = world.spawn();
    world.destroy(entity);
    const nx_test::ContractRecorder recorder;
    world.add(entity, Position{});
    world.destroy(entity);
    EXPECT_EQ(nx_test::ContractRecorder::count(), 2);
}

TEST_F(WorldTest, VersionsStartAtOneAndAdvance) {
    EXPECT_EQ(world.version(), nx::ecs::Version{1});
    EXPECT_EQ(world.advanceVersion(), nx::ecs::Version{2});
    EXPECT_EQ(world.version(), nx::ecs::Version{2});
}

} // namespace
