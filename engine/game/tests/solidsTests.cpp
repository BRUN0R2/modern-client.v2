// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Traces through what is solid, as the server's game traces a line: the world and the solid brush
// entities, and which of them stopped the line.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.assets;
import nx.collision;
import nx.game;

#include "solidsWorld.hpp"

namespace {

using nx::test::SolidsWorld;

/// A trace stops this short of what it meets (DIST_EPSILON).
constexpr float kBackOff = 0.03125F;
constexpr float kTolerance = 1.0e-3F;

TEST(SolidsTest, ALineStoppedByABrushEntityTellsWhichOne) {
    SolidsWorld solids{{}};
    const nx::game::SolidHit hit = nx::game::traceSolidsHit(
        solids.m_world, solids.m_hulls,
        nx::game::hullTrace(nx::collision::Hull::Point, {.x = -50.0F}, {.x = 200.0F}));
    EXPECT_NEAR(hit.trace.end.x, -kBackOff, kTolerance) << "the block's face, before the wall";
    ASSERT_TRUE(hit.entity.has_value());
    EXPECT_EQ(*hit.entity, solids.m_block);
    const nx::collision::Trace same = nx::game::traceSolids(
        solids.m_world, solids.m_hulls, nx::collision::Hull::Point, {.x = -50.0F}, {.x = 200.0F});
    EXPECT_EQ(same.fraction, hit.trace.fraction) << "the trace traceSolids gives";
}

TEST(SolidsTest, ALineStoppedByTheWorldTellsNoEntity) {
    // The block moved past the wall.
    SolidsWorld solids{{.x = 500.0F}};
    const nx::game::SolidHit hit = nx::game::traceSolidsHit(
        solids.m_world, solids.m_hulls,
        nx::game::hullTrace(nx::collision::Hull::Point, {.x = -50.0F}, {.x = 200.0F}));
    EXPECT_NEAR(hit.trace.end.x, 100.0F - kBackOff, kTolerance) << "the wall";
    EXPECT_FALSE(hit.entity.has_value());
    const nx::game::SolidHit missed = nx::game::traceSolidsHit(
        solids.m_world, solids.m_hulls,
        nx::game::hullTrace(nx::collision::Hull::Point, {.x = -50.0F}, {.x = 50.0F}));
    EXPECT_EQ(missed.trace.fraction, 1.0F);
    EXPECT_FALSE(missed.entity.has_value()) << "nothing met";
}

/// An entity solid as a box of 16 units about `origin`.
[[nodiscard]] auto boxAt(nx::ecs::World& world, nx::Vec3 origin) -> nx::ecs::Entity {
    return world.spawn(nx::game::Transform{.origin = origin},
                       nx::game::BoxSolidity{.box = {.min = {.x = -8.0F, .y = -8.0F, .z = -8.0F},
                                                     .max = {.x = 8.0F, .y = 8.0F, .z = 8.0F}}});
}

TEST(SolidsTest, AnEntitySolidAsABoxStopsAWayButOneThatMeetsNoMonsters) {
    SolidsWorld solids{{.x = 500.0F}};
    const nx::ecs::Entity box = boxAt(solids.m_world, {.x = 30.0F});
    const nx::game::SolidHit hit = nx::game::traceSolidsHit(
        solids.m_world, solids.m_hulls,
        nx::game::hullTrace(nx::collision::Hull::Point, {.x = -50.0F}, {.x = 200.0F}));
    ASSERT_TRUE(hit.entity.has_value());
    EXPECT_EQ(*hit.entity, box);
    EXPECT_NEAR(hit.trace.end.x, 22.0F - kBackOff, kTolerance);
    // Its fraction is of the whole way, though it was met on the way to where the world stopped it.
    EXPECT_NEAR(hit.trace.fraction, (72.0F - kBackOff) / 250.0F, kTolerance);

    nx::game::ServerTrace monsterless =
        nx::game::hullTrace(nx::collision::Hull::Point, {.x = -50.0F}, {.x = 200.0F});
    monsterless.type = nx::game::TraceType::NoMonsters;
    EXPECT_FALSE(nx::game::traceSolidsHit(solids.m_world, solids.m_hulls, monsterless).entity.has_value())
        << "the wall";
    nx::game::ServerTrace leftOut =
        nx::game::hullTrace(nx::collision::Hull::Point, {.x = -50.0F}, {.x = 200.0F});
    leftOut.ignored = box;
    EXPECT_FALSE(nx::game::traceSolidsHit(solids.m_world, solids.m_hulls, leftOut).entity.has_value());
}

TEST(SolidsTest, ABoxGrowsByTheBoxTraced) {
    SolidsWorld solids{{.x = 500.0F}};
    std::ignore = boxAt(solids.m_world, {.x = 30.0F});
    const nx::game::SolidHit hit =
        nx::game::traceSolidsHit(solids.m_world, solids.m_hulls,
                                 nx::game::ServerTrace{.box = {.min = {.x = -4.0F, .y = -4.0F, .z = -4.0F},
                                                               .max = {.x = 4.0F, .y = 4.0F, .z = 4.0F}},
                                                       .start = {.x = -50.0F},
                                                       .end = {.x = 60.0F}});
    EXPECT_NEAR(hit.trace.end.x, 18.0F - kBackOff, kTolerance);
}

TEST(SolidsTest, AWayTheWorldStopsAtOnceMeetsNoEntity) {
    // From where the world stops a line before the wall at x = 100, into it, in a box.
    SolidsWorld solids{{.x = 500.0F}};
    std::ignore = boxAt(solids.m_world, {.x = 95.0F});
    const nx::game::SolidHit hit = nx::game::traceSolidsHit(
        solids.m_world, solids.m_hulls,
        nx::game::hullTrace(nx::collision::Hull::Point, {.x = 100.0F - kBackOff}, {.x = 110.0F}));
    EXPECT_EQ(hit.trace.fraction, 0.0F);
    EXPECT_FALSE(hit.trace.startsSolid) << "the box it starts in is not looked at";
    EXPECT_FALSE(hit.entity.has_value());
}

TEST(SolidsTest, AWayAllInSolidMeetsNoEntity) {
    // A way all in the wall, where a box is too.
    SolidsWorld solids{{.x = 500.0F}};
    std::ignore = boxAt(solids.m_world, {.x = 150.0F});
    const nx::game::SolidHit hit = nx::game::traceSolidsHit(
        solids.m_world, solids.m_hulls,
        nx::game::hullTrace(nx::collision::Hull::Point, {.x = 140.0F}, {.x = 160.0F}));
    EXPECT_TRUE(hit.trace.isAllSolid);
    EXPECT_FALSE(hit.entity.has_value());
}

} // namespace
