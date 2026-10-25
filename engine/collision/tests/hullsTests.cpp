// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Traces through small trees of planes, by the server's rules and by player movement's, through the
// hulls of the BSP seed of the fuzz corpus, and through boxes made hulls.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.assets;
import nx.collision;

namespace {

using nx::Plane;
using nx::Vec3;
using nx::collision::BrushHulls;
using nx::collision::BrushHullsDesc;
using nx::collision::CollisionError;
using nx::collision::Contents;
using nx::collision::Hull;
using nx::collision::HullChild;
using nx::collision::HullNode;
using nx::collision::HullNodeIndex;
using nx::collision::HullRoots;
using nx::collision::HullTraceQuery;
using nx::collision::kWorldModel;
using nx::collision::Trace;

constexpr float kTolerance = 1.0e-5F;

/// A plane facing +X at `x`.
[[nodiscard]] constexpr auto atX(float x) -> Plane {
    return Plane{.normal = {.x = 1.0F}, .distance = x};
}

/// Hulls whose model 0 has `nodes` as hull 1 (standing), from node 0, and an empty hull 0. A last,
/// unused node keeps the root from being the tree's last, which movement takes for an empty hull.
[[nodiscard]] auto standingHull(std::vector<Plane> planes, std::vector<HullNode> nodes) -> BrushHulls {
    nodes.push_back(HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Empty}});
    auto hulls = BrushHulls::create(BrushHullsDesc{
        .planes = std::move(planes),
        .clipNodes = std::move(nodes),
        .models = {
            {.hulls = HullRoots{{Contents::Empty, HullNodeIndex{0}, Contents::Empty, Contents::Empty}}}}});
    EXPECT_TRUE(hulls.has_value());
    return std::move(hulls).value();
}

/// A floor at z = 0: solid below.
[[nodiscard]] auto floorHull() -> BrushHulls {
    return standingHull({Plane{.normal = {.z = 1.0F}, .distance = 0.0F}},
                        {HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Solid}}});
}

[[nodiscard]] auto standingQuery(Vec3 start, Vec3 end) -> HullTraceQuery {
    return HullTraceQuery{.hull = Hull::Standing, .start = start, .end = end};
}

TEST(HullTraceTest, ATraceStopsAThirtySecondOfAUnitBeforeThePlane) {
    const BrushHulls hulls = floorHull();
    for (const Trace& trace : {hulls.trace(standingQuery({.z = 10.0F}, {.z = -10.0F})),
                               hulls.traceMovement(standingQuery({.z = 10.0F}, {.z = -10.0F}))}) {
        EXPECT_FLOAT_EQ(trace.fraction, (10.0F - 0.03125F) / 20.0F);
        EXPECT_NEAR(trace.end.z, 0.03125F, kTolerance) << "rounded once by the server, twice by movement";
        EXPECT_EQ(trace.plane, (Plane{.normal = {.z = 1.0F}, .distance = 0.0F}));
        EXPECT_TRUE(trace.isInOpen);
        EXPECT_FALSE(trace.isAllSolid);
        EXPECT_FALSE(trace.startsSolid);
    }
    const Trace free = hulls.trace(standingQuery({.z = 10.0F}, {.z = 1.0F}));
    EXPECT_EQ(free.fraction, 1.0F);
    EXPECT_EQ(free.end, (Vec3{.z = 1.0F}));
}

TEST(BoxTraceTest, ABoxStopsATraceAThirtySecondOfAUnitBeforeItsFace) {
    const nx::Aabb box{.min = {.x = -10.0F, .y = -10.0F, .z = 0.0F},
                       .max = {.x = 10.0F, .y = 10.0F, .z = 62.0F}};
    const Trace across =
        nx::collision::traceBoxMovement(box, {.x = -30.0F, .z = 31.0F}, {.x = 30.0F, .z = 31.0F});
    EXPECT_FLOAT_EQ(across.fraction, (20.0F - 0.03125F) / 60.0F);
    EXPECT_NEAR(across.end.x, -10.03125F, kTolerance);
    EXPECT_EQ(across.plane, (Plane{.normal = {.x = -1.0F}, .distance = 10.0F})) << "its least x, facing out";
    EXPECT_FALSE(across.startsSolid);
    const Trace over =
        nx::collision::traceBoxMovement(box, {.x = -30.0F, .z = 70.0F}, {.x = 30.0F, .z = 70.0F});
    EXPECT_EQ(over.fraction, 1.0F) << "above it";
    const Trace onTop = nx::collision::traceBoxMovement(box, {.z = 70.0F}, {.z = 50.0F});
    EXPECT_EQ(onTop.plane, (Plane{.normal = {.z = 1.0F}, .distance = 62.0F}));
    EXPECT_NEAR(onTop.end.z, 62.03125F, kTolerance);
}

TEST(BoxTraceTest, ATraceFromInsideABoxStartsSolid) {
    const nx::Aabb box{.min = {.x = -10.0F, .y = -10.0F, .z = -10.0F},
                       .max = {.x = 10.0F, .y = 10.0F, .z = 10.0F}};
    const Trace leaving = nx::collision::traceBoxMovement(box, {}, {.x = 30.0F});
    EXPECT_TRUE(leaving.startsSolid);
    EXPECT_FALSE(leaving.isAllSolid) << "it leaves the box";
    EXPECT_EQ(leaving.fraction, 0.0F) << "movement stops it where it starts";
    EXPECT_EQ(leaving.end, Vec3{});
    const Trace within = nx::collision::traceBoxMovement(box, {}, {.x = 5.0F});
    EXPECT_TRUE(within.isAllSolid);
}

TEST(BoxTraceTest, ABoxHoldsItsLeastButNotItsMost) {
    const nx::Aabb box{.min = {.x = -10.0F, .y = -10.0F, .z = -10.0F},
                       .max = {.x = 10.0F, .y = 10.0F, .z = 10.0F}};
    EXPECT_EQ(nx::collision::boxContentsAt(box, {}), Contents::Solid);
    EXPECT_EQ(nx::collision::boxContentsAt(box, {.x = -10.0F}), Contents::Solid) << "its least";
    EXPECT_EQ(nx::collision::boxContentsAt(box, {.x = 10.0F}), Contents::Empty) << "its most";
    EXPECT_EQ(nx::collision::boxContentsAt(box, {.z = -10.5F}), Contents::Empty);
}

/// A hitbox on a bone a quarter turn about z at `origin`: its x along the world's y, its y along the
/// world's x reversed.
[[nodiscard]] auto turnedHitbox(Vec3 origin, const nx::Aabb& bounds) -> nx::collision::PlacedBox {
    return nx::collision::PlacedBox{
        .placement = {.rows = {nx::Vec4{.y = -1.0F, .w = origin.x}, nx::Vec4{.x = 1.0F, .w = origin.y},
                               nx::Vec4{.z = 1.0F, .w = origin.z}}},
        .bounds = bounds};
}

TEST(HitboxTraceTest, ATurnedHitboxStopsATraceAtItsFace) {
    // Its x from -10 to 30 is the world's y; its y and z, 5 each way, the world's x about 100 and z.
    const std::array hitbox{turnedHitbox({.x = 100.0F}, {.min = {.x = -10.0F, .y = -5.0F, .z = -5.0F},
                                                         .max = {.x = 30.0F, .y = 5.0F, .z = 5.0F}})};
    const nx::collision::HitboxTrace along =
        nx::collision::traceHitboxes(hitbox, {.x = 100.0F, .y = -50.0F}, {.x = 100.0F, .y = 50.0F});
    EXPECT_FLOAT_EQ(along.trace.fraction, (40.0F - 0.03125F) / 100.0F);
    EXPECT_NEAR(along.trace.end.y, -10.03125F, kTolerance);
    EXPECT_EQ(along.trace.plane, (Plane{.normal = {.y = -1.0F}, .distance = 10.0F}))
        << "its least x, facing out";
    EXPECT_EQ(along.hitbox, 0U);

    const nx::collision::HitboxTrace beside =
        nx::collision::traceHitboxes(hitbox, {.x = 106.0F, .y = -50.0F}, {.x = 106.0F, .y = 50.0F});
    EXPECT_EQ(beside.trace.fraction, 1.0F) << "past its y, the world's x";
}

TEST(HitboxTraceTest, TheHitboxMetFirstGivesTheTrace) {
    const nx::Aabb unit{.min = {.x = -1.0F, .y = -1.0F, .z = -1.0F},
                        .max = {.x = 1.0F, .y = 1.0F, .z = 1.0F}};
    const std::array hitboxes{
        nx::collision::PlacedBox{.placement = nx::affine(nx::Mat3{}, {.x = 50.0F}), .bounds = unit},
        nx::collision::PlacedBox{.placement = nx::affine(nx::Mat3{}, {.x = 20.0F}), .bounds = unit}};
    const nx::collision::HitboxTrace hit = nx::collision::traceHitboxes(hitboxes, {}, {.x = 100.0F});
    EXPECT_EQ(hit.hitbox, 1U);
    EXPECT_NEAR(hit.trace.end.x, 19.0F - 0.03125F, kTolerance);

    const nx::collision::HitboxTrace missed =
        nx::collision::traceHitboxes(hitboxes, {.z = 5.0F}, {.x = 100.0F, .z = 5.0F});
    EXPECT_EQ(missed.trace.fraction, 1.0F);
    EXPECT_EQ(missed.hitbox, 0U) << "the first's, none stopping it";
}

TEST(HitboxTraceTest, AHitboxTheTraceStartsInTakesThePlaceOfAnother) {
    const nx::Aabb unit{.min = {.x = -1.0F, .y = -1.0F, .z = -1.0F},
                        .max = {.x = 1.0F, .y = 1.0F, .z = 1.0F}};
    const nx::collision::PlacedBox around{.placement = nx::Mat3x4{}, .bounds = unit};
    const nx::collision::PlacedBox ahead{.placement = nx::affine(nx::Mat3{}, {.x = 50.0F}), .bounds = unit};

    const std::array aroundLast{ahead, around};
    const nx::collision::HitboxTrace replaced = nx::collision::traceHitboxes(aroundLast, {}, {.x = 100.0F});
    EXPECT_EQ(replaced.hitbox, 1U);
    EXPECT_TRUE(replaced.trace.startsSolid);
    EXPECT_EQ(replaced.trace.fraction, 1.0F) << "it leaves the box it starts in";

    const std::array aroundFirst{around, ahead};
    const nx::collision::HitboxTrace sooner = nx::collision::traceHitboxes(aroundFirst, {}, {.x = 100.0F});
    EXPECT_EQ(sooner.hitbox, 1U) << "the box ahead stops it sooner";
    EXPECT_TRUE(sooner.trace.startsSolid) << "having started in the first";
    EXPECT_LT(sooner.trace.fraction, 0.5F);
}

TEST(HullTraceTest, AnOffsetPlacesTheModel) {
    const BrushHulls hulls = floorHull();
    HullTraceQuery query = standingQuery({.z = 110.0F}, {.z = 90.0F});
    query.offset = Vec3{.z = 100.0F}; // the floor at z = 100
    const Trace trace = hulls.trace(query);
    EXPECT_FLOAT_EQ(trace.fraction, (10.0F - 0.03125F) / 20.0F);
    EXPECT_FLOAT_EQ(trace.end.z, 100.03125F);
    EXPECT_EQ(hulls.contentsAt(kWorldModel, Hull::Standing, {.z = -1.0F}), Contents::Solid)
        << "in the model's space";
}

TEST(HullTraceTest, ATurnedModelIsWhereItsAnglesTurnIt) {
    // Solid in front of x = 0 in its own space; turned a quarter turn to the left, in front of y = 0.
    const BrushHulls hulls =
        standingHull({atX(0.0F)}, {HullNode{.plane = {0}, .children = {Contents::Solid, Contents::Empty}}});
    const nx::EulerAngles quarterLeft{.yaw = nx::Degrees{90.0F}};
    for (const bool isMovement : {false, true}) {
        HullTraceQuery query = standingQuery({.y = -10.0F}, {.y = 10.0F});
        query.angles = quarterLeft;
        const Trace trace = isMovement ? hulls.traceMovement(query) : hulls.trace(query);
        EXPECT_NEAR(trace.end.y, -0.03125F, kTolerance) << isMovement;
        EXPECT_NEAR(trace.plane.normal.x, 0.0F, kTolerance) << isMovement;
        EXPECT_NEAR(trace.plane.normal.y, -1.0F, kTolerance) << "facing the way it came, in the world";
    }
    EXPECT_EQ(hulls.contentsAt(kWorldModel, Hull::Standing, {.y = 5.0F}, Vec3{}, quarterLeft),
              Contents::Solid);
    EXPECT_EQ(hulls.contentsAt(kWorldModel, Hull::Standing, {.x = 5.0F, .y = -5.0F}, Vec3{}, quarterLeft),
              Contents::Empty);
    EXPECT_EQ(hulls.movementContentsAt(kWorldModel, Hull::Standing, {.y = 5.0F}, Vec3{}, quarterLeft),
              Contents::Solid);
}

TEST(HullTraceTest, ATraceInSolidStartsSolid) {
    const BrushHulls hulls = floorHull();
    // Never out of solid: the server keeps the whole way, movement stops at the start.
    const Trace server = hulls.trace(standingQuery({.z = -5.0F}, {.z = -1.0F}));
    EXPECT_TRUE(server.startsSolid);
    EXPECT_TRUE(server.isAllSolid);
    EXPECT_EQ(server.fraction, 1.0F);
    const Trace movement = hulls.traceMovement(standingQuery({.z = -5.0F}, {.z = -1.0F}));
    EXPECT_TRUE(movement.startsSolid);
    EXPECT_EQ(movement.fraction, 0.0F);
    EXPECT_EQ(movement.end, (Vec3{.z = -5.0F}));

    // Out of solid on the way: not all solid, and nothing stops it.
    const Trace leaving = hulls.trace(standingQuery({.z = -5.0F}, {.z = 5.0F}));
    EXPECT_TRUE(leaving.startsSolid);
    EXPECT_FALSE(leaving.isAllSolid);
    EXPECT_TRUE(leaving.isInOpen);
    EXPECT_EQ(leaving.fraction, 1.0F);
    EXPECT_EQ(hulls.traceMovement(standingQuery({.z = -5.0F}, {.z = 5.0F})).fraction, 0.0F);
}

TEST(HullTraceTest, TranslucentContentsAreWaterToMovementOnly) {
    const BrushHulls hulls =
        standingHull({Plane{.normal = {.z = 1.0F}, .distance = 0.0F}},
                     {HullNode{.plane = {0}, .children = {Contents::Translucent, Contents::Water}}});
    const Trace server = hulls.trace(standingQuery({.z = 5.0F}, {.z = 10.0F}));
    EXPECT_FALSE(server.isInWater);
    EXPECT_FALSE(server.isInOpen);
    EXPECT_TRUE(hulls.traceMovement(standingQuery({.z = 5.0F}, {.z = 10.0F})).isInWater);
    EXPECT_TRUE(hulls.trace(standingQuery({.z = -5.0F}, {.z = -10.0F})).isInWater);
}

TEST(HullTraceTest, MovementTakesAHullRootedAtItsLastNodeForAnEmptyOne) {
    auto hulls = BrushHulls::create(BrushHullsDesc{
        .planes = {Plane{.normal = {.z = 1.0F}, .distance = 0.0F}},
        .clipNodes = {HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Solid}}},
        .models = {
            {.hulls = HullRoots{{Contents::Empty, HullNodeIndex{0}, Contents::Empty, Contents::Empty}}}}});
    ASSERT_TRUE(hulls.has_value());
    EXPECT_LT(hulls->trace(standingQuery({.z = 10.0F}, {.z = -10.0F})).fraction, 1.0F);
    EXPECT_EQ(hulls->traceMovement(standingQuery({.z = 10.0F}, {.z = -10.0F})).fraction, 1.0F) << "Parity";
    EXPECT_EQ(hulls->contentsAt(kWorldModel, Hull::Standing, {.z = -1.0F}), Contents::Solid);
    EXPECT_EQ(hulls->movementContentsAt(kWorldModel, Hull::Standing, {.z = -1.0F}), Contents::Empty)
        << "Parity";
}

TEST(HullTraceTest, AStopInSolidBacksUpByEachRulesStep) {
    // A thin solid at x from -0.031 to 0, then solid from x = 0.01125: the trace from x = -10 stops
    // at the second, and that stop, 1/32 before its plane, is in the first. 7% of the way past x = 0,
    // the server backs up a tenth (past its start: the stop is kept); movement a twentieth, and
    // stops still in solid.
    const BrushHulls hulls =
        standingHull({atX(0.0F), atX(-0.031F), atX(0.01125F)},
                     {HullNode{.plane = {0}, .children = {HullNodeIndex{2}, HullNodeIndex{1}}},
                      HullNode{.plane = {1}, .children = {Contents::Solid, Contents::Empty}},
                      HullNode{.plane = {2}, .children = {Contents::Solid, Contents::Empty}}});
    const HullTraceQuery query = standingQuery({.x = -10.0F}, {.x = 0.1295F});
    const Trace server = hulls.trace(query);
    const Trace movement = hulls.traceMovement(query);
    EXPECT_NEAR(server.end.x, -0.02F, 1.0e-4F);
    EXPECT_NEAR(movement.end.x, -0.028035F, 1.0e-4F);
    for (const Trace& trace : {server, movement}) {
        EXPECT_EQ(trace.plane, (Plane{.normal = {.x = -1.0F}, .distance = -0.01125F}));
        EXPECT_EQ(hulls.contentsAt(kWorldModel, Hull::Standing, trace.end), Contents::Solid)
            << "Parity: the stop stays in solid";
    }
    EXPECT_GT(server.fraction, movement.fraction);
}

TEST(HullTraceTest, RefusesBrokenTrees) {
    const auto errorOf = [](BrushHullsDesc desc) {
        auto hulls = BrushHulls::create(std::move(desc));
        return hulls ? std::nullopt : std::optional{hulls.error().code};
    };
    const std::vector planes{atX(0.0F)};
    EXPECT_EQ(errorOf({.planes = planes, .clipNodes = {HullNode{.plane = {1}}}}), CollisionError::BadIndex)
        << "plane";
    EXPECT_EQ(
        errorOf({.planes = planes,
                 .clipNodes = {HullNode{.plane = {0}, .children = {HullNodeIndex{1}, Contents::Empty}}}}),
        CollisionError::BadIndex)
        << "child";
    EXPECT_EQ(errorOf({.planes = planes,
                       .clipNodes = {HullNode{.children = {Contents::Empty, Contents::Empty}}},
                       .models = {{.hulls = HullRoots{{Contents::Empty, HullNodeIndex{3}, Contents::Empty,
                                                       Contents::Empty}}}}}),
              CollisionError::BadIndex)
        << "root";
    EXPECT_EQ(errorOf({.planes = planes,
                       .clipNodes = {HullNode{.children = {HullNodeIndex{1}, Contents::Empty}},
                                     HullNode{.children = {HullNodeIndex{0}, Contents::Empty}}}}),
              CollisionError::Cycle);
    std::vector<HullNode> chain(nx::collision::kMaxHullDepth + 1);
    for (std::uint32_t index = 0; index + 1 < chain.size(); ++index) {
        chain[index].children = {HullNodeIndex{index + 1}, Contents::Solid};
    }
    chain.back().children = {Contents::Empty, Contents::Solid};
    EXPECT_EQ(errorOf({.planes = planes, .clipNodes = std::move(chain)}), CollisionError::TooDeep);
}

TEST(HullTraceTest, ReadsTheHullsOfAMap) {
    // The fuzz corpus's box: 64 units, its floor at z = 0.
    auto file =
        nx::platform::readFile(std::filesystem::path{NX_FUZZ_CORPUS_DIRECTORY} / "bsp" / "boxEmbedded.bsp");
    ASSERT_TRUE(file.has_value()) << nx::describe(file.error());
    const auto map = nx::assets::BspView::parse(*file);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    const auto hulls = BrushHulls::create(*map);
    ASSERT_TRUE(hulls.has_value()) << nx::describe(hulls.error());
    EXPECT_EQ(hulls->modelCount(), 1U);
    EXPECT_EQ(hulls->boundsOf(kWorldModel).min, map->models()[0].bounds.min);
    EXPECT_EQ(hulls->boundsOf(kWorldModel).max, map->models()[0].bounds.max);
    for (const Hull hull : {Hull::Point, Hull::Standing}) {
        const Trace trace = hulls->trace(HullTraceQuery{.hull = hull,
                                                        .start = {.x = 32.0F, .y = 32.0F, .z = 32.0F},
                                                        .end = {.x = 32.0F, .y = 32.0F, .z = -32.0F}});
        EXPECT_FLOAT_EQ(trace.end.z, 0.03125F) << nx::enumName(hull);
        EXPECT_EQ(trace.plane.normal, (Vec3{.z = 1.0F})) << nx::enumName(hull);
    }
    EXPECT_EQ(hulls->contentsAt(kWorldModel, Hull::Point, {.x = 32.0F, .y = 32.0F, .z = 32.0F}),
              Contents::Empty);
    EXPECT_EQ(hulls->contentsAt(kWorldModel, Hull::Point, {.x = 32.0F, .y = 32.0F, .z = -1.0F}),
              Contents::Solid);
}

TEST(BoxHullTest, TheEngineTracesABoxThroughTheHullItsSizeChooses) {
    using nx::collision::boxHullOf;
    const auto expectOffset = [](Vec3 actual, Vec3 expected) {
        EXPECT_FLOAT_EQ(actual.x, expected.x);
        EXPECT_FLOAT_EQ(actual.y, expected.y);
        EXPECT_FLOAT_EQ(actual.z, expected.z);
    };
    // A hostage's box: 20 wide and 62 tall, its origin at its feet.
    const auto hostage =
        boxHullOf({.min = {.x = -10.0F, .y = -10.0F}, .max = {.x = 10.0F, .y = 10.0F, .z = 62.0F}});
    EXPECT_EQ(hostage.hull, Hull::Standing);
    expectOffset(hostage.offset, {.x = -6.0F, .y = -6.0F, .z = -36.0F});

    const auto small = boxHullOf({.min = {.x = -4.0F, .y = -4.0F}, .max = {.x = 4.0F, .y = 4.0F, .z = 8.0F}});
    EXPECT_EQ(small.hull, Hull::Point);
    expectOffset(small.offset, {});
    const auto ducked = boxHullOf(
        {.min = {.x = -16.0F, .y = -16.0F, .z = -18.0F}, .max = {.x = 16.0F, .y = 16.0F, .z = 18.0F}});
    EXPECT_EQ(ducked.hull, Hull::Ducked);
    expectOffset(ducked.offset, {});
    const auto large =
        boxHullOf({.min = {.x = -20.0F, .y = -20.0F}, .max = {.x = 20.0F, .y = 20.0F, .z = 40.0F}});
    EXPECT_EQ(large.hull, Hull::Large);
    expectOffset(large.offset, {.x = -12.0F, .y = -12.0F, .z = -32.0F});
}

} // namespace
