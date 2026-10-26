// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// An attack dealt to an entity as its kind takes it, and to one of a kind that takes none.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;
import nx.game;

namespace {

/// A kind that takes half of what an attack deals, and is killed by 10 or more.
[[nodiscard]] auto takeHalf(nx::ecs::World& /*world*/, nx::ecs::Entity /*entity*/,
                            const nx::game::Attack& attack) -> nx::game::Harm {
    const nx::Damage taken{attack.damage.value() / 2.0F};
    return nx::game::Harm{.damage = taken, .isKilled = taken.value() >= 10.0F};
}

TEST(DamageTest, AnAttackIsTakenAsTheEntitysKindTakesIt) {
    nx::ecs::World world;
    ASSERT_TRUE(nx::game::registerComponents(world).has_value());
    const nx::ecs::Entity taking = world.spawn(nx::game::DamageMethod{.takeAttack = &takeHalf});
    const nx::ecs::Entity untouched = world.spawn(nx::game::Transform{});

    const nx::game::Harm harm =
        nx::game::attackEntity(world, taking, nx::game::Attack{.damage = nx::Damage{24.0F}});
    EXPECT_EQ(harm.damage.value(), 12.0F);
    EXPECT_TRUE(harm.isKilled);
    const nx::game::Harm none =
        nx::game::attackEntity(world, untouched, nx::game::Attack{.damage = nx::Damage{24.0F}});
    EXPECT_EQ(none.damage.value(), 0.0F) << "a kind that takes no damage";
    EXPECT_FALSE(none.isKilled);
}

} // namespace
