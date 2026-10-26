// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The thinks of a kind's entities: each run in the tick it comes due, at the time it was due, or at
// the tick's start when it was due before, as the original's engine runs them.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.ecs;
import nx.collision;
import nx.game;

namespace {

using nx::GameTime;
using nx::collision::Contents;

/// A think of an entity, and when it ran.
struct Thought {
    nx::ecs::Entity entity{};
    GameTime at{};
};

/// The thinks recordThought ran, in their order.
[[nodiscard]] auto thoughts() -> std::vector<Thought>& {
    static std::vector<Thought> recorded;
    return recorded;
}

void recordThought(nx::ecs::World& /*world*/, const nx::collision::BrushHulls& /*hulls*/,
                   nx::ecs::Entity entity, GameTime at) {
    thoughts().push_back(Thought{.entity = entity, .at = at});
}

/// A world that is empty to every hull.
[[nodiscard]] auto emptyHulls() -> nx::collision::BrushHulls {
    auto hulls = nx::collision::BrushHulls::create(nx::collision::BrushHullsDesc{
        .models = {nx::collision::BrushModel{
            .hulls = nx::collision::HullRoots{
                {Contents::Empty, Contents::Empty, Contents::Empty, Contents::Empty}}}}});
    EXPECT_TRUE(hulls.has_value());
    return std::move(hulls).value();
}

TEST(ThinkingTest, AKindsThinkRunsInTheTickItComesDueAtTheTimeItWasDue) {
    thoughts().clear();
    nx::ecs::World world;
    ASSERT_TRUE(nx::game::registerComponents(world).has_value());
    const nx::collision::BrushHulls hulls = emptyHulls();
    const nx::ecs::Entity soon = world.spawn(nx::game::ThinkMethod{.think = &recordThought});
    const nx::ecs::Entity later = world.spawn(nx::game::ThinkMethod{.think = &recordThought});
    nx::game::setNextThink(world, soon, GameTime::atTick(0, 0.5F));
    nx::game::setNextThink(world, later, GameTime::atTick(3));

    nx::game::runThinks(world, hulls);
    ASSERT_EQ(thoughts().size(), 1U);
    EXPECT_EQ(thoughts()[0].entity, soon);
    EXPECT_EQ(thoughts()[0].at, GameTime::atTick(0, 0.5F)) << "at the time it was due";
    EXPECT_EQ(world.get<nx::game::NextThink>(soon), nullptr) << "its think is taken off";

    for (int tick = 0; tick < 5; ++tick) {
        nx::game::advanceTick(world);
    }
    nx::game::setNextThink(world, soon, GameTime::atTick(1));
    nx::game::runThinks(world, hulls);
    ASSERT_EQ(thoughts().size(), 3U);
    // The earliest first: the one due at 1, then the one due at 3, both before the tick of 5.
    EXPECT_EQ(thoughts()[1].entity, soon);
    EXPECT_EQ(thoughts()[1].at, GameTime::atTick(5)) << "due before: at the tick's start";
    EXPECT_EQ(thoughts()[2].entity, later);
    EXPECT_EQ(thoughts()[2].at, GameTime::atTick(5));
}

} // namespace
