// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.ecs;

#include "contractRecorder.hpp"

namespace {

using nx::ecs::Entity;
using nx::ecs::EntityTable;

TEST(EntityTest, DefaultIsTheNullEntity) {
    constexpr Entity entity;
    static_assert(entity.isNull());
    EXPECT_EQ(std::format("{}", entity), "null");
    static_assert(!std::is_constructible_v<Entity, std::uint32_t>, "raw indices must not become handles");
}

TEST(EntityTableTest, IssuesDistinctLiveEntities) {
    EntityTable table;
    const Entity first = table.create();
    const Entity second = table.create();

    EXPECT_NE(first, second);
    EXPECT_TRUE(table.isAlive(first));
    EXPECT_TRUE(table.isAlive(second));
    EXPECT_EQ(table.aliveCount(), 2U);
    EXPECT_EQ(std::format("{}", first), "0v0");
    EXPECT_EQ(std::format("{}", second), "1v0");
}

TEST(EntityTableTest, ADestroyedEntityIsNeverAliveAgain) {
    EntityTable table;
    const Entity entity = table.create();
    table.destroy(entity);
    EXPECT_FALSE(table.isAlive(entity));

    // The slot comes back with a new generation; the old handle still does not match.
    const Entity reused = table.create();
    EXPECT_EQ(reused.index(), entity.index());
    EXPECT_NE(reused.generation(), entity.generation());
    EXPECT_FALSE(table.isAlive(entity));
    EXPECT_TRUE(table.isAlive(reused));
}

TEST(EntityTableTest, ReusesFreedSlotsInFifoOrder) {
    EntityTable table;
    const Entity first = table.create();
    const Entity second = table.create();
    const Entity third = table.create();
    table.destroy(second);
    table.destroy(first);
    table.destroy(third);

    EXPECT_EQ(table.create().index(), second.index());
    EXPECT_EQ(table.create().index(), first.index());
    EXPECT_EQ(table.create().index(), third.index());
    EXPECT_EQ(table.slotCount(), 3U);
}

TEST(EntityTableTest, RetiresASlotWhoseGenerationsRanOut) {
    EntityTable table{2}; // generations 0 and 1
    const Entity first = table.create();
    table.destroy(first);
    const Entity second = table.create();
    ASSERT_EQ(second.index(), first.index());
    table.destroy(second);

    EXPECT_EQ(table.retiredCount(), 1U);
    const Entity fresh = table.create();
    EXPECT_NE(fresh.index(), first.index()); // the retired slot is never handed out again
    EXPECT_FALSE(table.isAlive(first));
    EXPECT_FALSE(table.isAlive(second));
    EXPECT_EQ(table.aliveCount(), 1U);
}

TEST(EntityTableTest, TheNullEntityIsNeverAlive) {
    EntityTable table;
    std::ignore = table.create();
    EXPECT_FALSE(table.isAlive(Entity{}));
}

TEST(EntityTableTest, DestroyingADeadEntityIsAViolation) {
    EntityTable table;
    const Entity entity = table.create();
    table.destroy(entity);

    const nx::test::ContractRecorder recorder;
    table.destroy(entity);
    EXPECT_EQ(nx::test::ContractRecorder::count(), 1);
    EXPECT_EQ(table.aliveCount(), 0U);
}

TEST(EntityTest, HashesDistinguishGenerations) {
    EntityTable table;
    const Entity first = table.create();
    table.destroy(first);
    const Entity second = table.create();
    EXPECT_NE(std::hash<Entity>{}(first), std::hash<Entity>{}(second));
}

} // namespace
