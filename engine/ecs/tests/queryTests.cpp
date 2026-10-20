// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.jobs;
import nx.ecs;

#include "contractRecorder.hpp"

namespace {

using nx::ecs::Changed;
using nx::ecs::Entity;
using nx::ecs::Optional;
using nx::ecs::Query;
using nx::ecs::Version;
using nx::ecs::With;
using nx::ecs::Without;
using nx::ecs::World;

struct Position {
    float x = 0;
    float y = 0;
};

struct Velocity {
    float dx = 0;
    float dy = 0;
};

struct Frozen {};

class QueryTest : public testing::Test {
protected:
    QueryTest() {
        EXPECT_TRUE(world.registerComponent<Position>("test::Position").has_value());
        EXPECT_TRUE(world.registerComponent<Velocity>("test::Velocity").has_value());
        EXPECT_TRUE(world.registerComponent<Frozen>("test::Frozen").has_value());
    }

    World world;
};

TEST_F(QueryTest, VisitsOnlyTheEntitiesThatHaveEveryAccessedComponent) {
    const Entity moving = world.spawn(Position{.x = 1, .y = 0}, Velocity{.dx = 2, .dy = 0});
    std::ignore = world.spawn(Position{.x = 10, .y = 0});
    std::ignore = world.spawn(Velocity{.dx = 20, .dy = 0});

    Query<const Velocity&, Position&> integrate{world};
    integrate.forEach([](const Velocity& velocity, Position& position) { position.x += velocity.dx; });

    EXPECT_EQ(world.get<Position>(moving)->x, 3);
    EXPECT_EQ(integrate.count(), 1U);
}

TEST_F(QueryTest, WithAndWithoutSelectByTag) {
    std::ignore = world.spawn(Position{}, Frozen{});
    std::ignore = world.spawn(Position{});
    std::ignore = world.spawn(Position{});

    EXPECT_EQ((Query<const Position&, With<Frozen>>{world}.count()), 1U);
    EXPECT_EQ((Query<const Position&, Without<Frozen>>{world}.count()), 2U);
}

TEST_F(QueryTest, OptionalAccessGivesNullWhenAbsent) {
    const Entity withVelocity = world.spawn(Position{}, Velocity{.dx = 5, .dy = 0});
    const Entity withoutVelocity = world.spawn(Position{});

    std::map<Entity, const Velocity*> seen;
    Query<Entity, const Position&, Optional<const Velocity>> query{world};
    query.forEach(
        [&seen](Entity entity, const Position&, const Velocity* velocity) { seen[entity] = velocity; });

    ASSERT_EQ(seen.size(), 2U);
    ASSERT_NE(seen[withVelocity], nullptr);
    EXPECT_EQ(seen[withVelocity]->dx, 5);
    EXPECT_EQ(seen[withoutVelocity], nullptr);
}

TEST_F(QueryTest, ForEachChunkHandsOutTheColumns) {
    constexpr int kCount = 5000; // several chunks
    for (int index = 0; index < kCount; ++index) {
        std::ignore =
            world.spawn(Position{.x = static_cast<float>(index), .y = 0}, Velocity{.dx = 1, .dy = 0});
    }

    std::size_t rows = 0;
    std::size_t chunks = 0;
    Query<Entity, const Velocity&, Position&> query{world};
    query.forEachChunk([&](std::span<const Entity> entities, std::span<const Velocity> velocities,
                           std::span<Position> positions) {
        ASSERT_EQ(entities.size(), positions.size());
        ASSERT_EQ(velocities.size(), positions.size());
        for (std::size_t row = 0; row < positions.size(); ++row) {
            positions[row].x += velocities[row].dx;
        }
        rows += positions.size();
        ++chunks;
    });
    EXPECT_EQ(rows, static_cast<std::size_t>(kCount));
    EXPECT_GT(chunks, 1U);
}

TEST_F(QueryTest, ParallelForEachChunkCoversEveryEntityOnce) {
    constexpr int kCount = 20'000;
    for (int index = 0; index < kCount; ++index) {
        std::ignore = world.spawn(Position{.x = 0, .y = 0}, Velocity{.dx = 1, .dy = 0});
    }
    auto jobs = nx::jobs::JobSystem::create({.workerCount = 3});
    ASSERT_TRUE(jobs.has_value());

    Query<const Velocity&, Position&> integrate{world};
    integrate.parallelForEachChunk(**jobs,
                                   [](std::span<const Velocity> velocities, std::span<Position> positions) {
                                       for (std::size_t row = 0; row < positions.size(); ++row) {
                                           positions[row].x += velocities[row].dx;
                                       }
                                   });

    std::size_t updated = 0;
    Query<const Position&>{world}.forEach([&updated](const Position& position) {
        if (position.x == 1) {
            ++updated;
        }
    });
    EXPECT_EQ(updated, static_cast<std::size_t>(kCount));
}

TEST_F(QueryTest, ChangedSkipsChunksWrittenBeforeTheLastRun) {
    const Entity still = world.spawn(Position{});
    const Entity moved = world.spawn(Position{}, Velocity{}); // another archetype, so another chunk
    const Version lastRun = world.version();
    world.advanceVersion();

    world.get<Position>(moved)->x = 1; // a mutable access stamps the current version

    std::vector<Entity> changed;
    Query<Entity, Changed<Position>> query{world, lastRun, world.version()};
    query.forEach([&changed](Entity entity) { changed.push_back(entity); });
    EXPECT_EQ(changed, std::vector{moved});
    EXPECT_NE(changed.front(), still);
}

TEST_F(QueryTest, WritesStampTheRunVersionOnTheirChunks) {
    std::ignore = world.spawn(Position{});
    const Version setup = world.version();
    const Version run = world.advanceVersion();

    Query<Position&> writer{world, setup, run};
    writer.forEach([](Position& position) { position.y = 1; });

    // Changed since before the run: yes. Changed since the run itself: no.
    EXPECT_EQ((Query<Changed<Position>>{world, setup, run}.count()), 1U);
    EXPECT_EQ((Query<Changed<Position>>{world, run, run}.count()), 0U);
}

TEST_F(QueryTest, ReadOnlyIterationDoesNotStamp) {
    std::ignore = world.spawn(Position{});
    const Version setup = world.version();
    const Version run = world.advanceVersion();

    Query<const Position&> reader{world, setup, run};
    reader.forEach([](const Position&) {});
    EXPECT_EQ((Query<Changed<Position>>{world, setup, run}.count()), 0U);
}

TEST_F(QueryTest, GetReturnsTheRowOfAMatchingEntity) {
    const Entity entity = world.spawn(Position{.x = 4, .y = 0}, Velocity{.dx = 6, .dy = 0});
    const Entity other = world.spawn(Position{});

    Query<const Position&, Velocity&> query{world};
    const auto row = query.get(entity);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ(std::get<0>(*row).x, 4);
    std::get<1>(*row).dx = 7;
    EXPECT_EQ(world.get<Velocity>(entity)->dx, 7);

    EXPECT_EQ(query.get(other), std::nullopt);
    world.destroy(entity);
    EXPECT_EQ(query.get(entity), std::nullopt);
}

TEST_F(QueryTest, SeesArchetypesCreatedAfterIt) {
    Query<const Position&> query{world};
    EXPECT_EQ(query.count(), 0U);
    std::ignore = world.spawn(Position{});
    std::ignore = world.spawn(Position{}, Frozen{});
    EXPECT_EQ(query.count(), 2U);
}

TEST_F(QueryTest, AStructuralChangeDuringIterationIsAViolation) {
    std::ignore = world.spawn(Position{});
    const nx_test::ContractRecorder recorder;
    Query<const Position&>{world}.forEach([this](const Position&) {
        std::ignore = world.spawn(); // goes to another archetype, so the iteration itself stays valid
    });
    EXPECT_EQ(nx_test::ContractRecorder::count(), 1);
}

} // namespace
