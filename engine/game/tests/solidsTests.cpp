// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Traces through what is solid, as the server's game traces a line: the world and the solid brush
// entities, and which of them stopped the line.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.jobs;
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

TEST(SolidsTest, AWayThatMissesABrushEntitysLinkedBoxDoesNotMeetIt) {
    // The block's hull is solid across all y and z, its bounds 64 each way: only what the engine links it by
    // tells a way beside it from one through it (SV_ClipToLinks).
    SolidsWorld solids{{}};
    constexpr float kBeside = 500.0F;
    const nx::game::SolidHit hit =
        nx::game::traceSolidsHit(solids.m_world, solids.m_hulls,
                                 nx::game::hullTrace(nx::collision::Hull::Point, {.x = -50.0F, .y = kBeside},
                                                     {.x = 90.0F, .y = kBeside}));
    EXPECT_EQ(hit.trace.fraction, 1.0F);
    EXPECT_FALSE(hit.entity.has_value());
}

TEST(SolidsTest, ALineReachesWhereATraceOfItGoesTheWholeWay) {
    SolidsWorld solids{{}};
    const nx::game::Solids gathered = nx::game::solidsOf(solids.m_world, solids.m_hulls);
    ASSERT_EQ(gathered.brushes.size(), 1U);
    EXPECT_EQ(gathered.brushes.front().linked,
              nx::game::linkedBoxOf(solids.m_hulls.boundsOf(gathered.brushes.front().model), {}, {}));
    const auto expectAsATrace = [&](nx::Vec3 start, nx::Vec3 end, bool reaches) {
        const nx::game::SolidHit traced =
            nx::game::traceSolidsHit(solids.m_world, solids.m_hulls, gathered,
                                     {.start = start, .end = end, .type = nx::game::TraceType::NoMonsters});
        EXPECT_EQ(traced.trace.fraction >= 1.0F, reaches);
        EXPECT_EQ(nx::game::lineReaches(solids.m_hulls, gathered.brushes, start, end), reaches);
    };
    expectAsATrace({.x = -50.0F}, {.x = -10.0F}, true);
    expectAsATrace({.x = -50.0F}, {.x = 50.0F}, false);                          // the block
    expectAsATrace({.x = 70.0F}, {.x = 200.0F}, false);                          // the world
    expectAsATrace({.x = -50.0F, .y = 500.0F}, {.x = 90.0F, .y = 500.0F}, true); // beside the block
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

/// The entities of `boxes` that forEachAbout gives for `area` that meet it, by entity, each must be given
/// once.
[[nodiscard]] auto boxesMeeting(const nx::game::SolidBoxes& boxes, const nx::Aabb& area)
    -> std::vector<nx::ecs::Entity> {
    std::vector<nx::ecs::Entity> met;
    boxes.forEachAbout(area, [&met, &area](const nx::game::SolidBoxEntity& solid) {
        if (area.intersects(solid.box.translated(solid.origin))) {
            met.push_back(solid.entity);
        }
    });
    std::ranges::sort(met);
    return met;
}

TEST(SolidBoxesTest, GivesWhatMeetsAnAreaAsLookingAtThemAllDoesWhereverTheyMove) {
    constexpr std::size_t kBoxes = 4096;
    constexpr std::size_t kLooks = 512;
    constexpr float kWorld = 32768.0F;
    constexpr float kWidestBox = 64.0F;
    constexpr float kFarthestLook = 2048.0F;
    nx::Random random{1, "solid boxes"};
    const auto drawn = [&random](float reach) {
        return random.between(-reach, reach);
    };
    nx::ecs::World world;
    nx::game::SolidBoxes boxes;
    std::vector<nx::game::SolidBoxEntity> all;
    for (std::size_t index = 0; index < kBoxes; ++index) {
        const float half = random.between(1.0F, kWidestBox);
        const nx::game::SolidBoxEntity solid{
            .entity = world.spawn(),
            .origin = {.x = drawn(kWorld), .y = drawn(kWorld), .z = drawn(kWorld)},
            .box = {.min = {.x = -half, .y = -half, .z = -half}, .max = {.x = half, .y = half, .z = half}}};
        boxes.add(solid);
        all.push_back(solid);
    }
    for (std::size_t look = 0; look < kLooks; ++look) {
        // One moves, one goes and comes back elsewhere, as bodies do between looks.
        nx::game::SolidBoxEntity& moved = all[look % all.size()];
        moved.origin = {.x = drawn(kWorld), .y = drawn(kWorld), .z = drawn(kWorld)};
        ASSERT_TRUE(boxes.place(moved.entity, moved.origin, moved.box));
        nx::game::SolidBoxEntity& gone = all[(look * 3) % all.size()];
        boxes.forget(gone.entity);
        EXPECT_FALSE(boxes.place(gone.entity, gone.origin, gone.box)) << "it holds it no more";
        gone.origin = {.x = drawn(kWorld), .y = drawn(kWorld), .z = drawn(kWorld)};
        boxes.add(gone);
        const nx::Vec3 at{.x = drawn(kWorld), .y = drawn(kWorld), .z = drawn(kWorld)};
        const float reach = random.between(1.0F, kFarthestLook);
        const nx::Aabb area{.min = at - nx::Vec3{.x = reach, .y = reach, .z = reach},
                            .max = at + nx::Vec3{.x = reach, .y = reach, .z = reach}};
        std::vector<nx::ecs::Entity> expected;
        for (const nx::game::SolidBoxEntity& solid : all) {
            if (area.intersects(solid.box.translated(solid.origin))) {
                expected.push_back(solid.entity);
            }
        }
        std::ranges::sort(expected);
        ASSERT_EQ(boxesMeeting(boxes, area), expected) << "look " << look;
    }
    EXPECT_EQ(boxes.size(), kBoxes);
}

TEST(SolidBoxesTest, FindsABoxPastTheEdgeOfWhatItsSquaresCover) {
    // Farther out than the coordinates of a map go: all those squares hold it, as the edge's.
    constexpr std::size_t kMany = 512;
    constexpr float kFarOut = 100000.0F;
    nx::ecs::World world;
    nx::game::SolidBoxes boxes;
    const nx::Aabb box{.min = {.x = -16.0F, .y = -16.0F, .z = -16.0F},
                       .max = {.x = 16.0F, .y = 16.0F, .z = 16.0F}};
    const nx::ecs::Entity far = world.spawn();
    boxes.add({.entity = far, .origin = {.x = kFarOut, .y = kFarOut}, .box = box});
    for (std::size_t index = 0; index < kMany; ++index) {
        boxes.add({.entity = world.spawn(), .origin = {.x = static_cast<float>(index)}, .box = box});
    }
    const nx::Aabb area{.min = {.x = kFarOut - 1.0F, .y = kFarOut - 1.0F, .z = -1.0F},
                        .max = {.x = kFarOut + 1.0F, .y = kFarOut + 1.0F, .z = 1.0F}};
    EXPECT_EQ(boxesMeeting(boxes, area), std::vector{far});
}

TEST(SolidBoxesTest, GatheringTwiceGivesTheSameAsGatheringOnceInJobsOrNot) {
    constexpr std::size_t kMany = 2048;
    constexpr float kSpacing = 37.0F;
    nx::ecs::World world;
    ASSERT_TRUE(nx::game::registerComponents(world).has_value());
    const nx::Aabb box{.min = {.x = -16.0F, .y = -16.0F, .z = -16.0F},
                       .max = {.x = 16.0F, .y = 16.0F, .z = 16.0F}};
    std::vector<nx::ecs::Entity> made;
    for (std::size_t index = 0; index < kMany; ++index) {
        made.push_back(world.spawn(nx::game::Transform{.origin = {.x = static_cast<float>(index) * kSpacing}},
                                   nx::game::BoxSolidity{.box = box}));
    }
    auto jobs = nx::jobs::JobSystem::create({.workerCount = 3});
    ASSERT_TRUE(jobs.has_value());
    nx::game::SolidBoxes alone;
    alone.gather(world);
    nx::game::SolidBoxes shared;
    shared.gather(world, jobs->get());
    shared.gather(world, jobs->get());
    EXPECT_EQ(shared.size(), kMany);
    const nx::Aabb area{.min = {.x = 1000.0F, .y = -1.0F, .z = -1.0F},
                        .max = {.x = 2000.0F, .y = 1.0F, .z = 1.0F}};
    const std::vector<nx::ecs::Entity> met = boxesMeeting(alone, area);
    EXPECT_FALSE(met.empty());
    EXPECT_EQ(boxesMeeting(shared, area), met);
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
