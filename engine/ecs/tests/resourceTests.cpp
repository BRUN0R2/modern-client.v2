// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.ecs;

namespace {

using nx::ecs::World;

struct GameRules {
    int roundSeconds = 0;
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

TEST(ResourceTest, StoresOneValuePerType) {
    World world;
    EXPECT_EQ(world.resource<GameRules>(), nullptr);

    world.insertResource(GameRules{.roundSeconds = 115});
    ASSERT_NE(world.resource<GameRules>(), nullptr);
    EXPECT_EQ(world.resource<GameRules>()->roundSeconds, 115);

    world.insertResource(GameRules{.roundSeconds = 300}); // replaces
    EXPECT_EQ(std::as_const(world).resource<GameRules>()->roundSeconds, 300);

    world.removeResource<GameRules>();
    EXPECT_EQ(world.resource<GameRules>(), nullptr);
}

TEST(ResourceTest, ResourcesAreDestroyedOnceWithTheirWorld) {
    int destroyed = 0;
    {
        World world;
        world.insertResource(Tracked{destroyed});
        world.insertResource(Tracked{destroyed}); // the first one dies here
        EXPECT_EQ(destroyed, 1);
    }
    EXPECT_EQ(destroyed, 2);
}

} // namespace
