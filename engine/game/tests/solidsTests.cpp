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
        solids.m_world, solids.m_hulls, nx::collision::Hull::Point, {.x = -50.0F}, {.x = 200.0F});
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
        solids.m_world, solids.m_hulls, nx::collision::Hull::Point, {.x = -50.0F}, {.x = 200.0F});
    EXPECT_NEAR(hit.trace.end.x, 100.0F - kBackOff, kTolerance) << "the wall";
    EXPECT_FALSE(hit.entity.has_value());
    const nx::game::SolidHit missed = nx::game::traceSolidsHit(
        solids.m_world, solids.m_hulls, nx::collision::Hull::Point, {.x = -50.0F}, {.x = 50.0F});
    EXPECT_EQ(missed.trace.fraction, 1.0F);
    EXPECT_FALSE(missed.entity.has_value()) << "nothing met";
}

} // namespace
