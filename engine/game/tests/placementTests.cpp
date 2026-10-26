// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// An entity dropped to the floor, as the engine drops it: a hostage's box over a floor at z = 0.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;
import nx.game;

namespace {

using nx::Vec3;
using nx::collision::BrushHulls;
using nx::collision::Contents;
using nx::collision::HullNode;
using nx::collision::HullNodeIndex;
using nx::collision::HullRoots;

/// A hostage's box: 20 units wide and 62 tall, its origin at its feet.
constexpr nx::Aabb kHostageBox{.min = {.x = -10.0F, .y = -10.0F},
                               .max = {.x = 10.0F, .y = 10.0F, .z = 62.0F}};
/// The server's trace stops this far before what stops it.
constexpr float kStopDistance = 0.03125F;

/// A world whose standing hull is solid below z = 36: a floor at z = 0 for a standing box.
[[nodiscard]] auto floorHulls() -> BrushHulls {
    auto hulls = BrushHulls::create(nx::collision::BrushHullsDesc{
        .planes = {nx::Plane{.normal = {.z = 1.0F}, .distance = 36.0F}},
        .clipNodes = {HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Solid}}},
        .models = {nx::collision::BrushModel{
            .hulls = HullRoots{{Contents::Empty, HullNodeIndex{0}, Contents::Empty, Contents::Empty}}}}});
    EXPECT_TRUE(hulls.has_value());
    return std::move(hulls).value();
}

TEST(PlacementTest, ABoxDropsToTheFloorThroughTheHullItsSizeChooses) {
    nx::ecs::World world;
    ASSERT_TRUE(nx::game::registerComponents(world).has_value());
    const BrushHulls hulls = floorHulls();

    const Vec3 dropped = nx::game::droppedToFloor(world, hulls, kHostageBox,
                                                  {.x = 4.0F, .y = 5.0F, .z = 100.0F}, std::nullopt);
    EXPECT_FLOAT_EQ(dropped.x, 4.0F);
    EXPECT_FLOAT_EQ(dropped.y, 5.0F);
    EXPECT_NEAR(dropped.z, kStopDistance, 1e-4F) << "its feet a thirty-second above the floor";
}

TEST(PlacementTest, ABoxThatMeetsNoFloorOrStartsInSolidStays) {
    nx::ecs::World world;
    ASSERT_TRUE(nx::game::registerComponents(world).has_value());
    const BrushHulls hulls = floorHulls();

    const Vec3 high{.z = 400.0F};
    EXPECT_FLOAT_EQ(nx::game::droppedToFloor(world, hulls, kHostageBox, high, std::nullopt).z, 400.0F)
        << "256 units reach no floor";
    const Vec3 buried{.z = -50.0F};
    EXPECT_FLOAT_EQ(nx::game::droppedToFloor(world, hulls, kHostageBox, buried, std::nullopt).z, -50.0F)
        << "all in solid";
}

TEST(PlacementTest, ABoxDropsOntoAnotherButItself) {
    nx::ecs::World world;
    ASSERT_TRUE(nx::game::registerComponents(world).has_value());
    const BrushHulls hulls = floorHulls();
    const nx::ecs::Entity below = world.spawn(nx::game::Transform{.origin = {.x = 4.0F, .y = 5.0F}},
                                              nx::game::BoxSolidity{.box = kHostageBox});

    const Vec3 dropped = nx::game::droppedToFloor(world, hulls, kHostageBox,
                                                  {.x = 4.0F, .y = 5.0F, .z = 100.0F}, std::nullopt);
    EXPECT_NEAR(dropped.z, kHostageBox.max.z + kStopDistance, 1e-4F) << "on the other's head";
    const Vec3 itself =
        nx::game::droppedToFloor(world, hulls, kHostageBox, {.x = 4.0F, .y = 5.0F, .z = 30.0F}, below);
    EXPECT_NEAR(itself.z, kStopDistance, 1e-4F) << "through itself, to the floor";
}

} // namespace
