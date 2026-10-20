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

TEST_F(CommandsTest, AppliesInRecordingOrder) {
    const Entity existing = world.spawn(Position{.x = 0, .y = 0});
    Commands commands{world};
    const auto spawned = commands.spawn(Position{.x = 1, .y = 2});
    commands.add(existing, Owner{.entity = existing});
    commands.remove<Position>(existing);

    const auto stats = commands.apply(world);
    EXPECT_EQ(stats.applied, 4U); // spawn, its Position, Owner, remove
    EXPECT_EQ(stats.skipped, 0U);
    EXPECT_TRUE(commands.isEmpty());

    const auto entity = commands.resolve(spawned);
    ASSERT_TRUE(entity.has_value());
    ASSERT_NE(world.get<Position>(*entity), nullptr);
    EXPECT_EQ(world.get<Position>(*entity)->y, 2);
    EXPECT_FALSE(world.has<Position>(existing));
    EXPECT_EQ(world.get<Owner>(existing)->entity, existing);
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

TEST_F(CommandsTest, UnappliedComponentsAreDestroyedWithTheBuffer) {
    int destroyed = 0;
    {
        Commands commands{world};
        std::ignore = commands.spawn(Tracked{destroyed});
        commands.add(world.spawn(), Tracked{destroyed});
    }
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
