// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Meshes built from geometry made here: where the polygons are and of which kind, the same mesh with
// any number of workers or started and finished later, what a profile and a geometry may not be, and the
// meshes of each profile.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.navigation;

#include "testGeometry.hpp"

namespace {

using nx::Vec3;
using nx::navigation::AreaKind;
using nx::navigation::Geometry;
using nx::navigation::MeshBuildError;
using nx::navigation::MeshLink;
using nx::navigation::MeshOutline;
using nx::navigation::NavigationError;
using nx::navigation::NavigationMesh;
using nx::navigation::NavigationMeshes;

/// The player's radius, and the cell a polygon's corner may stray by from where the floor ends.
constexpr float kRadius = 16.0F;
constexpr float kCell = 4.0F;

/// Whether some polygon of `outline` is of `kind`.
[[nodiscard]] auto hasKind(const MeshOutline& outline, AreaKind kind) -> bool {
    return std::ranges::any_of(outline.polygons, [kind](const nx::navigation::MeshPolygon& polygon) {
        return polygon.kind == kind;
    });
}

/// Whether some polygon of `outline` has a corner over the square from `low` to `high` across.
[[nodiscard]] auto hasCornerOver(const MeshOutline& outline, Vec3 low, Vec3 high) -> bool {
    return std::ranges::any_of(outline.corners, [low, high](Vec3 corner) {
        return corner.x > low.x && corner.x < high.x && corner.y > low.y && corner.y < high.y;
    });
}

TEST(NavigationMeshTest, AFloorWithNoWallIsCoveredToItsEdges) {
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 512.0F, .y = 384.0F}, 0.0F);
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    const MeshOutline outline = mesh.outline();
    ASSERT_FALSE(outline.polygons.empty());
    EXPECT_EQ(mesh.polygonCount(), outline.polygons.size());
    EXPECT_GT(mesh.tileCount(), 1U); // 512 units are 4 tiles of 128
    // A body's box holds it up with its middle at a ledge.
    Vec3 least{.x = std::numeric_limits<float>::max(), .y = std::numeric_limits<float>::max()};
    Vec3 most{.x = std::numeric_limits<float>::lowest(), .y = std::numeric_limits<float>::lowest()};
    for (const Vec3 corner : outline.corners) {
        least = {.x = std::min(least.x, corner.x), .y = std::min(least.y, corner.y)};
        most = {.x = std::max(most.x, corner.x), .y = std::max(most.y, corner.y)};
        EXPECT_NEAR(corner.z, 0.0F, 2.0F * kCell);
    }
    EXPECT_NEAR(least.x, 0.0F, kCell);
    EXPECT_NEAR(most.x, 512.0F, kCell);
    EXPECT_NEAR(least.y, 0.0F, kCell);
    EXPECT_NEAR(most.y, 384.0F, kCell);
    EXPECT_TRUE(outline.links.empty()) << "its floor goes on past the sides of its tiles, and nowhere else";
    EXPECT_TRUE(std::ranges::all_of(outline.polygons, [](const nx::navigation::MeshPolygon& polygon) {
        return polygon.kind == AreaKind::Floor && polygon.cornerCount >= 3;
    }));
}

TEST(NavigationMeshTest, TheSameMeshIsBuiltWithAnyNumberOfWorkers) {
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = -640.0F, .y = -512.0F}, {.x = 640.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = -96.0F, .y = -300.0F, .z = 0.0F},
                     {.x = 96.0F, .y = 200.0F, .z = 128.0F});
    nx::test::addBox(geometry, {.x = 300.0F, .y = 100.0F, .z = 0.0F}, {.x = 340.0F, .y = 140.0F, .z = 10.0F});
    const MeshOutline serial = nx::test::playerMesh(geometry, 0).outline();
    for (const std::uint32_t workers : {1U, 3U, 15U}) {
        EXPECT_EQ(nx::test::playerMesh(geometry, workers).outline(), serial) << workers << " workers";
    }
}

TEST(NavigationMeshTest, ABuildStartedGoesOnWithoutItsGeometryAndGivesTheSameMesh) {
    const auto floorAndBox = [] {
        Geometry geometry;
        nx::test::addFloor(geometry, {.x = -640.0F, .y = -512.0F}, {.x = 640.0F, .y = 512.0F}, 0.0F);
        nx::test::addBox(geometry, {.x = -96.0F, .y = -300.0F, .z = 0.0F},
                         {.x = 96.0F, .y = 200.0F, .z = 128.0F});
        return geometry;
    };
    const MeshOutline expected = nx::test::playerMesh(floorAndBox()).outline();
    for (const std::uint32_t workers : {0U, 3U}) {
        auto jobs = nx::test::jobSystem(workers);
        auto started = [&jobs, &floorAndBox] {
            nx::navigation::AgentProfile profile = nx::test::playerProfile();
            nx::navigation::BuildSettings settings = nx::test::testSettings();
            return NavigationMesh::startBuild(*jobs, floorAndBox(), profile, settings);
        }(); // the geometry, profile and settings it was started with are gone
        ASSERT_TRUE(started.has_value()) << nx::describe(started.error());
        auto mesh = std::move(*started).finish();
        ASSERT_TRUE(mesh.has_value()) << nx::describe(mesh.error());
        EXPECT_EQ(mesh->outline(), expected) << workers << " workers";
        EXPECT_EQ(mesh->profile().name, nx::test::playerProfile().name);
    }
}

TEST(NavigationMeshTest, ABuildLeftUnfinishedWaitsForItsJobsAsItGoes) {
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = -640.0F, .y = -512.0F}, {.x = 640.0F, .y = 512.0F}, 0.0F);
    auto jobs = nx::test::jobSystem(3);
    {
        auto started =
            NavigationMesh::startBuild(*jobs, geometry, nx::test::playerProfile(), nx::test::testSettings());
        ASSERT_TRUE(started.has_value());
    } // its jobs ran before what they write went
    const nx::jobs::JobSystemStats stats = jobs->stats();
    const std::uint64_t executed =
        std::ranges::fold_left(stats.workers | std::views::transform(&nx::jobs::WorkerStats::executedJobs),
                               stats.jobsExecutedByWaiters, std::plus{});
    EXPECT_GT(executed, 1U); // the tiles' and the mesh's
}

TEST(NavigationMeshTest, ABuildIsRefusedAsItStartsForWhatItCannotBuild) {
    auto jobs = nx::test::jobSystem(0);
    nx::navigation::AgentProfile taller = nx::test::playerProfile();
    taller.crouchedHeight = nx::HammerUnits{80.0F};
    const auto refusedProfile =
        NavigationMesh::startBuild(*jobs, Geometry{}, taller, nx::test::testSettings());
    ASSERT_FALSE(refusedProfile.has_value());
    EXPECT_EQ(refusedProfile.error().code, MeshBuildError::InvalidProfile);
    Geometry geometry;
    geometry.triangles.push_back({.corners = {0, 1, 2}});
    const auto refusedGeometry =
        NavigationMesh::startBuild(*jobs, geometry, nx::test::playerProfile(), nx::test::testSettings());
    ASSERT_FALSE(refusedGeometry.has_value());
    EXPECT_EQ(refusedGeometry.error().code, MeshBuildError::InvalidGeometry);
}

TEST(NavigationMeshTest, AFloorSteeperThanTheProfilesIsNone) {
    // Two ramps rising 1 unit for every 1 across (normal 0.71 up) and 2 across (0.45 up).
    Geometry geometry;
    const std::array gentle{Vec3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, Vec3{.x = 256.0F, .y = 0.0F, .z = 256.0F},
                            Vec3{.x = 256.0F, .y = 256.0F, .z = 256.0F},
                            Vec3{.x = 0.0F, .y = 256.0F, .z = 0.0F}};
    const std::array steep{Vec3{.x = 512.0F, .y = 0.0F, .z = 0.0F}, Vec3{.x = 640.0F, .y = 0.0F, .z = 256.0F},
                           Vec3{.x = 640.0F, .y = 256.0F, .z = 256.0F},
                           Vec3{.x = 512.0F, .y = 256.0F, .z = 0.0F}};
    nx::navigation::addConvexPolygon(geometry, gentle, AreaKind::Floor);
    nx::navigation::addConvexPolygon(geometry, steep, AreaKind::Floor);
    const MeshOutline outline = nx::test::playerMesh(geometry).outline();
    EXPECT_TRUE(hasCornerOver(outline, {.x = 0.0F, .y = 0.0F}, {.x = 256.0F, .y = 256.0F}));
    EXPECT_FALSE(hasCornerOver(outline, {.x = 500.0F, .y = 0.0F}, {.x = 700.0F, .y = 256.0F}));
}

TEST(NavigationMeshTest, LowRoomIsCrouchedAndLowerIsNone) {
    // A floor under a ceiling 50 units up (crouched only) from x = 256, and 30 up (no room) from 512.
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 768.0F, .y = 256.0F}, 0.0F);
    nx::test::addCeiling(geometry, {.x = 256.0F, .y = -64.0F}, {.x = 512.0F, .y = 320.0F}, 50.0F);
    nx::test::addCeiling(geometry, {.x = 512.0F, .y = -64.0F}, {.x = 832.0F, .y = 320.0F}, 30.0F);
    const MeshOutline outline = nx::test::playerMesh(geometry).outline();
    EXPECT_TRUE(hasKind(outline, AreaKind::Floor));
    EXPECT_TRUE(hasKind(outline, AreaKind::Crouch));
    for (const nx::navigation::MeshPolygon& polygon : outline.polygons) {
        for (const Vec3 corner :
             std::span{outline.corners}.subspan(polygon.firstCorner, polygon.cornerCount)) {
            EXPECT_LE(corner.x, 512.0F + kCell);
            if (polygon.kind == AreaKind::Crouch) {
                EXPECT_GE(corner.x, 256.0F - kCell);
            }
        }
    }
}

TEST(NavigationMeshTest, FloorIsSortedByTheBodiesThatFitOnItThoughNoSurfaceShowsWhy) {
    // No ceiling at all: from x = 256 to 512 a standing body's hull alone is solid, and past that a crouched
    // one's too.
    constexpr float kStandingMiddle = 36.0F;
    constexpr float kCrouchedMiddle = 18.0F;
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 768.0F, .y = 256.0F}, 0.0F);
    geometry.bodyHull =
        nx::test::bodyHullOfBox({.min = {.x = 256.0F, .y = -64.0F, .z = kStandingMiddle - 1.0F},
                                 .max = {.x = 832.0F, .y = 320.0F, .z = 128.0F}});
    geometry.crouchedBodyHull =
        nx::test::bodyHullOfBox({.min = {.x = 512.0F, .y = -64.0F, .z = kCrouchedMiddle - 1.0F},
                                 .max = {.x = 832.0F, .y = 320.0F, .z = 128.0F}});
    const MeshOutline outline = nx::test::playerMesh(geometry).outline();
    EXPECT_TRUE(hasKind(outline, AreaKind::Floor));
    EXPECT_TRUE(hasKind(outline, AreaKind::Crouch));
    for (const nx::navigation::MeshPolygon& polygon : outline.polygons) {
        for (const Vec3 corner :
             std::span{outline.corners}.subspan(polygon.firstCorner, polygon.cornerCount)) {
            EXPECT_LE(corner.x, 512.0F + kCell) << "where no body fits is no floor";
            if (polygon.kind == AreaKind::Crouch) {
                EXPECT_GE(corner.x, 256.0F - kCell);
            } else {
                EXPECT_LE(corner.x, 256.0F + kCell);
            }
        }
    }
    // A profile that does not crouch has none of it.
    nx::navigation::AgentProfile standing = nx::test::playerProfile();
    standing.crouchedHeight = standing.standingHeight;
    const MeshOutline upright = nx::test::playerMesh(geometry, 3, standing).outline();
    EXPECT_FALSE(hasKind(upright, AreaKind::Crouch));
}

TEST(NavigationMeshTest, UnderALowCeilingACrouchedBodysHullDecides) {
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 768.0F, .y = 256.0F}, 0.0F);
    nx::test::addCeiling(geometry, {.x = 256.0F, .y = -64.0F}, {.x = 512.0F, .y = 320.0F}, 50.0F);
    // Its standing hull is solid under any such ceiling, and says nothing there.
    geometry.bodyHull = nx::test::bodyHullOfBox(
        {.min = {.x = 240.0F, .y = -128.0F, .z = -64.0F}, .max = {.x = 528.0F, .y = 384.0F, .z = 128.0F}});
    EXPECT_TRUE(hasKind(nx::test::playerMesh(geometry).outline(), AreaKind::Crouch));
    geometry.crouchedBodyHull = geometry.bodyHull;
    EXPECT_FALSE(hasKind(nx::test::playerMesh(geometry).outline(), AreaKind::Crouch));
}

TEST(NavigationMeshTest, TheFloorAlongWhatStopsABodyIsOfTheKindEdge) {
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 768.0F, .y = 256.0F}, 0.0F);
    nx::test::stopBodies(geometry, {.min = {.x = 352.0F, .y = -64.0F, .z = -64.0F},
                                    .max = {.x = 416.0F, .y = 320.0F, .z = 256.0F}});
    const MeshOutline outline = nx::test::playerMesh(geometry, 3, nx::test::clearedPlayerProfile()).outline();
    const auto touchesBarrier = [&outline](const nx::navigation::MeshPolygon& polygon, float x) {
        return std::ranges::any_of(
            std::span{outline.corners}.subspan(polygon.firstCorner, polygon.cornerCount),
            [x](Vec3 corner) { return std::abs(corner.x - x) <= kCell; });
    };
    for (const nx::navigation::MeshPolygon& polygon : outline.polygons) {
        for (const Vec3 corner :
             std::span{outline.corners}.subspan(polygon.firstCorner, polygon.cornerCount)) {
            EXPECT_FALSE(corner.x > 352.0F + kCell && corner.x < 416.0F - kCell) << std::format("{}", corner);
        }
        if (touchesBarrier(polygon, 352.0F) || touchesBarrier(polygon, 416.0F)) {
            EXPECT_EQ(polygon.kind, AreaKind::Edge);
        }
    }
}

TEST(NavigationMeshTest, ABodyHullOutOfOrderOrNotFiniteIsRefused) {
    using nx::navigation::BodyHull;
    using nx::navigation::BodyHullLeaf;
    const nx::Plane plane{.normal = {.z = 1.0F}};
    const nx::Plane notFinite{.normal = {.z = std::numeric_limits<float>::quiet_NaN()}};
    const std::array refused{
        BodyHull{.root = std::uint32_t{0}},
        BodyHull{.root = std::uint32_t{0},
                 .nodes = {{.plane = plane, .children = {std::uint32_t{0}, BodyHullLeaf::Open}}}},
        BodyHull{.root = std::uint32_t{0},
                 .nodes = {{.plane = plane, .children = {BodyHullLeaf::Open, std::uint32_t{7}}}}},
        BodyHull{.root = std::uint32_t{0},
                 .nodes = {{.plane = notFinite, .children = {BodyHullLeaf::Open, BodyHullLeaf::Solid}}}},
    };
    auto jobs = nx::test::jobSystem();
    for (const BodyHull& hull : refused) {
        Geometry geometry;
        nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 256.0F, .y = 256.0F}, 0.0F);
        geometry.bodyHull = hull;
        const auto mesh =
            NavigationMesh::build(*jobs, geometry, nx::test::playerProfile(), nx::test::testSettings());
        ASSERT_FALSE(mesh.has_value());
        EXPECT_EQ(mesh.error().code, MeshBuildError::InvalidGeometry);
    }
}

TEST(NavigationMeshTest, TheFloorWithinTheWallClearanceOfItsEdgesIsOfTheKindEdge) {
    constexpr float kClearance = 16.0F;
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 512.0F, .y = 384.0F}, 0.0F);
    const MeshOutline outline = nx::test::playerMesh(geometry, 3, nx::test::clearedPlayerProfile()).outline();
    ASSERT_TRUE(hasKind(outline, AreaKind::Edge));
    ASSERT_TRUE(hasKind(outline, AreaKind::Floor));
    // The floor past the clearance from the edges; the edge's polygons between, as far as the very edges.
    bool reachesTheEdge = false;
    for (const nx::navigation::MeshPolygon& polygon : outline.polygons) {
        for (const Vec3 corner :
             std::span{outline.corners}.subspan(polygon.firstCorner, polygon.cornerCount)) {
            if (polygon.kind == AreaKind::Floor) {
                EXPECT_GE(corner.x, kClearance - kCell);
                EXPECT_LE(corner.x, 512.0F - kClearance + kCell);
                EXPECT_GE(corner.y, kClearance - kCell);
                EXPECT_LE(corner.y, 384.0F - kClearance + kCell);
            }
            reachesTheEdge = reachesTheEdge || corner.x <= kCell;
        }
    }
    EXPECT_TRUE(reachesTheEdge) << "the floor along the edges is still walked";
}

/// Whether `point` is over the box from `low` to `high` across.
[[nodiscard]] auto isOver(Vec3 point, Vec3 low, Vec3 high) -> bool {
    return point.x >= low.x && point.x <= high.x && point.y >= low.y && point.y <= high.y;
}

TEST(NavigationMeshTest, ALedgeItJumpsOntoIsLinkedBothWaysAndOneItDropsFromDownOnly) {
    // On a floor at 0, a box 40 tall, which the player jumps onto, and apart one 120 tall, which it drops
    // from unhurt.
    constexpr Vec3 kLowLow{.x = 256.0F, .y = 128.0F, .z = -16.0F};
    constexpr Vec3 kLowHigh{.x = 512.0F, .y = 384.0F, .z = 40.0F};
    constexpr Vec3 kHighLow{.x = 1024.0F, .y = 128.0F, .z = -16.0F};
    constexpr Vec3 kHighHigh{.x = 1280.0F, .y = 384.0F, .z = 120.0F};
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1536.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, kLowLow, kLowHigh);
    nx::test::addBox(geometry, kHighLow, kHighHigh);
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    const MeshOutline outline = mesh.outline();
    EXPECT_FALSE(hasKind(outline, AreaKind::Jump)) << "a link is no polygon of the outline's";
    EXPECT_FALSE(hasKind(outline, AreaKind::Drop));
    ASSERT_FALSE(outline.links.empty());
    std::size_t jumps = 0;
    std::size_t drops = 0;
    for (const MeshLink& link : outline.links) {
        const bool isLow = isOver(link.start, kLowLow, kLowHigh) || isOver(link.end, kLowLow, kLowHigh);
        const Vec3 low = isLow ? kLowLow : kHighLow;
        const Vec3 high = isLow ? kLowHigh : kHighHigh;
        EXPECT_EQ(link.kind, isLow ? AreaKind::Jump : AreaKind::Drop) << std::format("{}", link.start);
        EXPECT_TRUE(isOver(link.start, low, high)) << "from the box's top";
        EXPECT_NEAR(link.start.z, high.z, kCell);
        EXPECT_FALSE(isOver(link.end, low, high)) << "to the floor beside it";
        EXPECT_NEAR(link.end.z, 0.0F, kCell);
        // It lands its radius and a cell past the box's side, where the floor's mesh starts its radius off.
        const float past =
            std::max({low.x - link.end.x, link.end.x - high.x, low.y - link.end.y, link.end.y - high.y});
        EXPECT_NEAR(past, kRadius + kCell, kCell);
        ++(link.kind == AreaKind::Jump ? jumps : drops);
    }
    EXPECT_GT(jumps, 0U);
    EXPECT_GT(drops, 0U);
}

TEST(NavigationMeshTest, ABoxIsLinkedDownFromEachOfItsSidesAlongTheSidesOfTilesOrNot) {
    constexpr float kTileSide = 0.0F;
    constexpr float kInsideTiles = 20.0F;
    for (const float inset : {kTileSide, kInsideTiles}) {
        const Vec3 low{.x = 384.0F + inset, .y = 128.0F + inset, .z = -16.0F};
        const Vec3 high{.x = 640.0F - inset, .y = 384.0F - inset, .z = 120.0F};
        Geometry geometry;
        nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
        nx::test::addBox(geometry, low, high);
        const MeshOutline outline = nx::test::playerMesh(geometry).outline();
        const auto hasLinkDownTo = [&outline](auto isThere) {
            return std::ranges::any_of(outline.links, [&isThere](const MeshLink& link) {
                return link.start.z > link.end.z && isThere(link.end);
            });
        };
        const auto past = [](float side, float position, float way) {
            return (position - side) * way > 0.0F;
        };
        EXPECT_TRUE(hasLinkDownTo([&](Vec3 end) { return past(high.x, end.x, 1.0F); })) << inset;
        EXPECT_TRUE(hasLinkDownTo([&](Vec3 end) { return past(low.x, end.x, -1.0F); })) << inset;
        EXPECT_TRUE(hasLinkDownTo([&](Vec3 end) { return past(high.y, end.y, 1.0F); })) << inset;
        EXPECT_TRUE(hasLinkDownTo([&](Vec3 end) { return past(low.y, end.y, -1.0F); })) << inset;
    }
}

TEST(NavigationMeshTest, AWallKeepsTheFloorOffAsFarAsABodysRadius) {
    constexpr Vec3 kLow{.x = 256.0F, .y = 128.0F, .z = 0.0F};
    constexpr Vec3 kHigh{.x = 512.0F, .y = 384.0F, .z = 128.0F};
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 768.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, kLow, kHigh);
    const MeshOutline outline = nx::test::playerMesh(geometry).outline();
    constexpr float kOff = kRadius - kCell;
    float nearestOff = std::numeric_limits<float>::max();
    for (const Vec3 corner : outline.corners) {
        const float off =
            std::max({kLow.x - corner.x, corner.x - kHigh.x, kLow.y - corner.y, corner.y - kHigh.y});
        // On top of the box, or inside it, where the floor its faces do not cover goes on just as well.
        if (corner.z >= kHigh.z / 2.0F || off <= -kOff) {
            continue;
        }
        EXPECT_GE(off, kOff) << std::format("{}", corner);
        nearestOff = std::min(nearestOff, off);
    }
    EXPECT_LE(nearestOff, kRadius + (2.0F * kCell)) << "and no farther";
}

TEST(NavigationMeshTest, ABlockABodysBoxStandsOnIsFloorItJumpsOntoAndDownFrom) {
    constexpr Vec3 kLow{.x = 544.0F, .y = 160.0F, .z = -16.0F};
    constexpr Vec3 kHigh{.x = 584.0F, .y = 200.0F, .z = 30.0F};
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, kLow, kHigh);
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    EXPECT_TRUE(
        std::ranges::any_of(mesh.outline().corners, [](Vec3 corner) { return corner.z > kHigh.z / 2.0F; }));
    nx::navigation::NavigationQuery query{mesh};
    const Vec3 top{.x = 564.0F, .y = 180.0F, .z = kHigh.z};
    const Vec3 floor{.x = 100.0F, .y = 100.0F};
    EXPECT_TRUE(query.isReachable(floor, top).value_or(false));
    EXPECT_TRUE(query.isReachable(top, floor).value_or(false));
}

TEST(NavigationMeshTest, AGapALeapSpansIsLinkedBothWaysAndOneWiderIsNot) {
    // Two floors with nothing under what is between them. A leap at 230 units a second, 41.8 high, flies 148
    // units.
    const auto meshOver = [](float gap) {
        Geometry geometry;
        nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 512.0F, .y = 512.0F}, 0.0F);
        nx::test::addFloor(geometry, {.x = 512.0F + gap, .y = 0.0F}, {.x = 1024.0F + gap, .y = 512.0F}, 0.0F);
        return nx::test::playerMesh(geometry);
    };
    constexpr float kSpanned = 100.0F;
    constexpr float kTooWide = 200.0F;
    const Vec3 here{.x = 256.0F, .y = 256.0F};
    {
        const NavigationMesh mesh = meshOver(kSpanned);
        const MeshOutline outline = mesh.outline();
        ASSERT_FALSE(outline.links.empty());
        for (const MeshLink& link : outline.links) {
            EXPECT_EQ(link.kind, AreaKind::Jump);
            EXPECT_NEAR(nx::test::distanceAcross(link.start, link.end), kSpanned, 2.0F * kCell)
                << "edge to edge";
        }
        nx::navigation::NavigationQuery query{mesh};
        const Vec3 there{.x = 512.0F + kSpanned + 256.0F, .y = 256.0F};
        EXPECT_TRUE(query.isReachable(here, there).value_or(false));
        EXPECT_TRUE(query.isReachable(there, here).value_or(false));
    }
    const NavigationMesh mesh = meshOver(kTooWide);
    EXPECT_TRUE(mesh.outline().links.empty());
    nx::navigation::NavigationQuery query{mesh};
    EXPECT_FALSE(query.isReachable(here, {.x = 512.0F + kTooWide + 256.0F, .y = 256.0F}).value_or(true));
}

TEST(NavigationMeshTest, AWayGoesOverNoLinkItShunsWhileItHasAnother) {
    constexpr Vec3 kLow{.x = 256.0F, .y = 128.0F, .z = -16.0F};
    constexpr Vec3 kHigh{.x = 512.0F, .y = 384.0F, .z = 30.0F};
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 768.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, kLow, kHigh);
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    const MeshOutline outline = mesh.outline();
    nx::navigation::NavigationQuery query{mesh};
    const Vec3 here{.x = 128.0F, .y = 256.0F};
    const Vec3 there{.x = 384.0F, .y = 256.0F, .z = kHigh.z};
    const auto way = query.findPath({.from = here, .to = there});
    ASSERT_TRUE(way.has_value());
    ASSERT_GE(way->points.size(), 3U);
    // Either way: it goes up a link that leaves from the box.
    const auto isOnTheWay = [&way](Vec3 end) {
        return nx::test::distanceAcross(end, way->points[1]) < 1.0F;
    };
    const auto taken = std::ranges::find_if(outline.links, [&isOnTheWay](const MeshLink& link) {
        return isOnTheWay(link.start) || isOnTheWay(link.end);
    });
    ASSERT_NE(taken, outline.links.end());
    const std::array shunned{*taken};
    const auto around = query.findPath({.from = here, .to = there, .shunned = shunned});
    ASSERT_TRUE(around.has_value()) << nx::describe(around.error());
    EXPECT_GT(nx::test::distanceAcross(around->points[1], way->points[1]), 1.0F);
    const auto none = query.findPath({.from = here, .to = there, .shunned = outline.links});
    ASSERT_FALSE(none.has_value());
    EXPECT_EQ(none.error().code, nx::navigation::NavigationError::Unreachable);
}

TEST(NavigationMeshTest, ALedgeWithRoomOnlyForADuckedBodyIsACrouchedJumpThoughAStandingOneIsHighEnough) {
    // A box 30 tall, what stops a standing body over it alone: a ceiling that no surface shows.
    constexpr Vec3 kLow{.x = 256.0F, .y = 128.0F, .z = -16.0F};
    constexpr Vec3 kHigh{.x = 512.0F, .y = 384.0F, .z = 30.0F};
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 768.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, kLow, kHigh);
    geometry.bodyHull = nx::test::bodyHullOfBox(
        {.min = {.x = kLow.x, .y = kLow.y, .z = 60.0F}, .max = {.x = kHigh.x, .y = kHigh.y, .z = 256.0F}});
    nx::navigation::AgentProfile jumping = nx::test::playerProfile();
    jumping.crouchedJumpHeight = nx::HammerUnits{62.5F};
    const NavigationMesh mesh = nx::test::playerMesh(geometry, 3, jumping);
    const MeshOutline outline = mesh.outline();
    ASSERT_FALSE(outline.links.empty());
    for (const MeshLink& link : outline.links) {
        EXPECT_EQ(link.kind, AreaKind::CrouchedJump) << std::format("{}", link.start);
    }
    EXPECT_TRUE(hasKind(outline, AreaKind::Crouch));
    nx::navigation::NavigationQuery query{mesh};
    const Vec3 top{.x = 384.0F, .y = 256.0F, .z = kHigh.z};
    const Vec3 floor{.x = 128.0F, .y = 256.0F};
    EXPECT_TRUE(query.isReachable(floor, top).value_or(false));
    EXPECT_TRUE(query.isReachable(top, floor).value_or(false));
}

TEST(NavigationMeshTest, AFloorABrushEntityLeavesNoRoomOnIsNoFloor) {
    constexpr nx::Aabb kStops{.min = {.x = 160.0F, .y = 160.0F, .z = 0.0F},
                              .max = {.x = 352.0F, .y = 352.0F, .z = 128.0F}};
    constexpr Vec3 kMiddle{.x = 256.0F, .y = 256.0F};
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 512.0F, .y = 512.0F}, 0.0F);
    {
        const NavigationMesh mesh = nx::test::playerMesh(geometry);
        nx::navigation::NavigationQuery query{mesh};
        const auto nearest = query.nearestPoint(kMiddle);
        ASSERT_TRUE(nearest.has_value()) << nx::describe(nearest.error());
        EXPECT_LT(nx::test::distanceAcross(*nearest, kMiddle), kCell) << "with none, it is floor";
    }
    const nx::navigation::PlacedBodyHull placed{.bounds = kStops, .hull = nx::test::bodyHullOfBox(kStops)};
    geometry.brushBodyHulls.push_back(placed);
    geometry.crouchedBrushBodyHulls.push_back(placed);
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    nx::navigation::NavigationQuery query{mesh};
    const auto nearest = query.nearestPoint(kMiddle);
    ASSERT_TRUE(nearest.has_value()) << nx::describe(nearest.error());
    EXPECT_GE(nx::test::distanceAcross(*nearest, kMiddle), (kStops.max.x - kStops.min.x) / 2.0F)
        << "the nearest floor is past where it stops a body";
}

TEST(NavigationMeshTest, ADoorJoinsTheFloorsOnItsTwoSidesBothWaysAndNamesItself) {
    // Two rooms, a wall across them with a door in it, which stands closed, solid like the wall.
    constexpr nx::Aabb kDoor{.min = {.x = 496.0F, .y = 224.0F, .z = 0.0F},
                             .max = {.x = 528.0F, .y = 288.0F, .z = 96.0F}};
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 496.0F, .y = -32.0F, .z = 0.0F},
                     {.x = 528.0F, .y = 224.0F, .z = 128.0F});
    nx::test::addBox(geometry, {.x = 496.0F, .y = 288.0F, .z = 0.0F},
                     {.x = 528.0F, .y = 544.0F, .z = 128.0F});
    nx::test::addBox(geometry, kDoor.min, kDoor.max);
    const Vec3 here{.x = 128.0F, .y = 256.0F};
    const Vec3 there{.x = 896.0F, .y = 256.0F};
    {
        const NavigationMesh mesh = nx::test::playerMesh(geometry);
        nx::navigation::NavigationQuery query{mesh};
        EXPECT_FALSE(query.isReachable(here, there).value_or(true)) << "no way but the door";
    }
    geometry.doors.push_back(kDoor);
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    const MeshOutline outline = mesh.outline();
    const auto isOfDoor = [](const MeshLink& link) {
        return link.kind == AreaKind::Door;
    };
    ASSERT_TRUE(std::ranges::any_of(outline.links, isOfDoor));
    for (const MeshLink& link : outline.links | std::views::filter(isOfDoor)) {
        EXPECT_NEAR(nx::test::distanceAcross(link.through, (kDoor.min + kDoor.max) * 0.5F), 0.0F, 1.0F);
    }
    nx::navigation::NavigationQuery query{mesh};
    EXPECT_TRUE(query.isReachable(here, there).value_or(false));
    EXPECT_TRUE(query.isReachable(there, here).value_or(false));

    // A way asked to go through no door closed among those it is told of.
    std::array gates{nx::navigation::Gate{.middle = (kDoor.min + kDoor.max) * 0.5F, .isOpen = false}};
    const auto closed = query.findPath({.from = here, .to = there, .gates = gates});
    ASSERT_FALSE(closed.has_value());
    EXPECT_EQ(closed.error().code, nx::navigation::NavigationError::Unreachable);
    gates.front().isOpen = true;
    EXPECT_TRUE(query.findPath({.from = here, .to = there, .gates = gates}).has_value());
    EXPECT_TRUE(query.findPath({.from = here, .to = there}).has_value()) << "none told of";
}

TEST(NavigationMeshTest, ALedgeOnlyACrouchedJumpReachesIsLinkedBothWaysOfItsOwnKind) {
    // On a floor at 0, a box 50 tall: higher than a jump and within one ducking in the air.
    constexpr Vec3 kLow{.x = 256.0F, .y = 128.0F, .z = -16.0F};
    constexpr Vec3 kHigh{.x = 512.0F, .y = 384.0F, .z = 50.0F};
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 768.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, kLow, kHigh);
    nx::navigation::AgentProfile jumping = nx::test::playerProfile();
    jumping.crouchedJumpHeight = nx::HammerUnits{58.0F};
    const Vec3 top{.x = 384.0F, .y = 256.0F, .z = kHigh.z};
    const Vec3 floor{.x = 128.0F, .y = 256.0F};
    {
        const NavigationMesh mesh = nx::test::playerMesh(geometry, 3, jumping);
        const MeshOutline outline = mesh.outline();
        ASSERT_FALSE(outline.links.empty());
        for (const MeshLink& link : outline.links) {
            EXPECT_EQ(link.kind, AreaKind::CrouchedJump);
        }
        nx::navigation::NavigationQuery query{mesh};
        EXPECT_TRUE(query.isReachable(floor, top).value_or(false)) << "up";
        EXPECT_TRUE(query.isReachable(top, floor).value_or(false)) << "down";
    }
    // None but a drop for a profile that does not, down only.
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    for (const MeshLink& link : mesh.outline().links) {
        EXPECT_EQ(link.kind, AreaKind::Drop);
    }
    nx::navigation::NavigationQuery query{mesh};
    EXPECT_FALSE(query.isReachable(floor, top).value_or(true));
    EXPECT_TRUE(query.isReachable(top, floor).value_or(false));
}

TEST(NavigationMeshTest, NoLinkLeavesALedgeHigherThanItDropsFromNorTheFloorAlongAWall) {
    // A box 200 tall on a floor: its top is too high to drop from unhurt, and its sides are walls to the
    // floor about it.
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 1024.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 384.0F, .y = 128.0F, .z = -16.0F},
                     {.x = 640.0F, .y = 384.0F, .z = 200.0F});
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    const MeshOutline outline = mesh.outline();
    EXPECT_TRUE(hasCornerOver(outline, {.x = 384.0F, .y = 128.0F}, {.x = 640.0F, .y = 384.0F}))
        << "the box's top is a floor of the mesh";
    EXPECT_TRUE(outline.links.empty());
}

TEST(NavigationMeshTest, AFloorKeepsItsKind) {
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 256.0F, .y = 256.0F}, 0.0F, AreaKind::Water);
    const MeshOutline outline = nx::test::playerMesh(geometry).outline();
    ASSERT_FALSE(outline.polygons.empty());
    EXPECT_TRUE(std::ranges::all_of(outline.polygons, [](const nx::navigation::MeshPolygon& polygon) {
        return polygon.kind == AreaKind::Water;
    }));
}

TEST(NavigationMeshTest, NoGeometryBuildsAnEmptyMesh) {
    auto jobs = nx::test::jobSystem();
    const auto mesh =
        NavigationMesh::build(*jobs, Geometry{}, nx::test::playerProfile(), nx::test::testSettings());
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(mesh->polygonCount(), 0U);
    EXPECT_TRUE(mesh->outline().polygons.empty());
}

TEST(NavigationMeshTest, RejectsNonfiniteAndUnrepresentableBuildDimensionsBeforeIntegerConversions) {
    const auto profile = nx::test::playerProfile();
    auto settings = nx::test::testSettings();
    settings.cellSize = nx::HammerUnits{std::numeric_limits<float>::infinity()};
    EXPECT_FALSE(nx::navigation::checkSettings(settings).has_value());
    settings = nx::test::testSettings();
    settings.smallestIsland = nx::HammerUnits{std::numeric_limits<float>::max()};
    EXPECT_FALSE(nx::navigation::checkSettings(settings).has_value());
    settings = nx::test::testSettings();
    settings.tileCells = std::numeric_limits<std::uint32_t>::max();
    EXPECT_FALSE(nx::navigation::checkProfile(profile, settings).has_value());
    auto oversized = profile;
    oversized.radius = nx::HammerUnits{std::numeric_limits<float>::infinity()};
    EXPECT_FALSE(nx::navigation::checkProfile(oversized, nx::test::testSettings()).has_value());
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = -1.0e20F, .y = -100}, {.x = 1.0e20F, .y = 100}, 0);
    auto jobs = nx::test::jobSystem(0);
    const auto mesh = NavigationMesh::build(*jobs, geometry, profile, nx::test::testSettings());
    ASSERT_FALSE(mesh.has_value());
    EXPECT_EQ(mesh.error().code, MeshBuildError::TooLarge);
}

TEST(NavigationMeshTest, ATriangleOfAMissingVertexIsRefused) {
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 256.0F, .y = 256.0F}, 0.0F);
    geometry.triangles.push_back({.corners = {0, 1, 99}});
    auto jobs = nx::test::jobSystem();
    const auto mesh =
        NavigationMesh::build(*jobs, geometry, nx::test::playerProfile(), nx::test::testSettings());
    ASSERT_FALSE(mesh.has_value());
    EXPECT_EQ(mesh.error().code, MeshBuildError::InvalidGeometry);
}

TEST(NavigationMeshTest, ACrouchedJumpOutOfItsRangeIsRefused) {
    const nx::navigation::BuildSettings settings = nx::test::testSettings();
    nx::navigation::AgentProfile lower = nx::test::playerProfile();
    lower.crouchedJumpHeight = nx::HammerUnits{lower.jumpHeight.value() - 1.0F};
    nx::navigation::AgentProfile higher = nx::test::playerProfile();
    higher.crouchedJumpHeight = nx::HammerUnits{higher.dropHeight.value() + 1.0F};
    nx::navigation::AgentProfile notFinite = nx::test::playerProfile();
    notFinite.crouchedJumpHeight = nx::HammerUnits{std::numeric_limits<float>::infinity()};
    for (const nx::navigation::AgentProfile& profile : {lower, higher, notFinite}) {
        const auto checked = nx::navigation::checkProfile(profile, settings);
        ASSERT_FALSE(checked.has_value());
        EXPECT_EQ(checked.error().code, MeshBuildError::InvalidProfile);
    }
    nx::navigation::AgentProfile within = nx::test::playerProfile();
    within.crouchedJumpHeight = nx::HammerUnits{58.0F};
    EXPECT_TRUE(nx::navigation::checkProfile(within, settings).has_value());
}

TEST(NavigationMeshTest, AProfileBreakingItsRulesIsRefused) {
    const nx::navigation::BuildSettings settings = nx::test::testSettings();
    nx::navigation::AgentProfile taller = nx::test::playerProfile();
    taller.crouchedHeight = nx::HammerUnits{80.0F};
    nx::navigation::AgentProfile cheaper = nx::test::playerProfile();
    cheaper.areaCosts.push_back({.area = AreaKind::Water, .cost = 0.5F});
    nx::navigation::AgentProfile twice = nx::test::playerProfile();
    twice.areaCosts.push_back({.area = AreaKind::Crouch, .cost = 2.0F});
    nx::navigation::AgentProfile flat = nx::test::playerProfile();
    flat.minGroundNormalZ = 0.0F;
    nx::navigation::AgentProfile hugging = nx::test::playerProfile();
    hugging.wallClearance = nx::HammerUnits{-1.0F};
    for (const nx::navigation::AgentProfile& profile : {taller, cheaper, twice, flat, hugging}) {
        const auto checked = nx::navigation::checkProfile(profile, settings);
        ASSERT_FALSE(checked.has_value());
        EXPECT_EQ(checked.error().code, MeshBuildError::InvalidProfile);
    }
    EXPECT_TRUE(nx::navigation::checkProfile(nx::test::playerProfile(), settings).has_value());
    EXPECT_TRUE(nx::navigation::checkProfile(nx::test::clearedPlayerProfile(), settings).has_value());
    nx::navigation::BuildSettings noCells = settings;
    noCells.cellSize = nx::HammerUnits{0.0F};
    const auto checked = nx::navigation::checkSettings(noCells);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().code, MeshBuildError::InvalidSettings);
}

TEST(NavigationMeshesTest, AProfileIsFoundByItsNameAndHasAMeshOnceBuilt) {
    nx::navigation::AgentProfile boss = nx::test::playerProfile();
    boss.name = "boss";
    boss.radius = nx::HammerUnits{64.0F};
    boss.standingHeight = nx::HammerUnits{128.0F};
    boss.crouchedHeight = nx::HammerUnits{128.0F};
    auto meshes = NavigationMeshes::create({nx::test::playerProfile(), boss}, nx::test::testSettings());
    ASSERT_TRUE(meshes.has_value());

    const auto unknown = meshes->profileNamed("zombie");
    ASSERT_FALSE(unknown.has_value());
    EXPECT_EQ(unknown.error().code, NavigationError::UnknownProfile);
    EXPECT_NE(unknown.error().detail.find("zombie"), std::string::npos);

    const auto player = meshes->profileNamed("player");
    const auto bossIndex = meshes->profileNamed("boss");
    ASSERT_TRUE(player.has_value());
    ASSERT_TRUE(bossIndex.has_value());
    EXPECT_EQ(meshes->meshOf(*player).error().code, NavigationError::NoMesh);

    // A corridor 64 units wide between walls: the player passes, the boss, 128 wide, does not fit.
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 512.0F, .y = 64.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = 0.0F, .y = -32.0F, .z = 0.0F}, {.x = 512.0F, .y = 0.0F, .z = 128.0F});
    nx::test::addBox(geometry, {.x = 0.0F, .y = 64.0F, .z = 0.0F}, {.x = 512.0F, .y = 96.0F, .z = 128.0F});
    auto jobs = nx::test::jobSystem();
    for (const nx::navigation::ProfileIndex profile : {*player, *bossIndex}) {
        const auto built = meshes->build(*jobs, geometry, profile);
        ASSERT_TRUE(built.has_value()) << nx::describe(built.error());
    }
    const auto playerMesh = meshes->meshOf(*player);
    const auto bossMesh = meshes->meshOf(*bossIndex);
    ASSERT_TRUE(playerMesh.has_value());
    ASSERT_TRUE(bossMesh.has_value());
    const auto isOnCorridorFloor = [](Vec3 corner) {
        return corner.z < 64.0F;
    };
    EXPECT_TRUE(std::ranges::any_of(playerMesh->get().outline().corners, isOnCorridorFloor));
    EXPECT_TRUE(std::ranges::none_of(bossMesh->get().outline().corners, isOnCorridorFloor))
        << "the tops of the walls alone";
    EXPECT_EQ(playerMesh->get().profile().name, "player");

    meshes->clear();
    EXPECT_EQ(meshes->meshOf(*player).error().code, NavigationError::NoMesh);
}

TEST(NavigationMeshesTest, TwoProfilesOfOneNameAreRefused) {
    const auto meshes = NavigationMeshes::create({nx::test::playerProfile(), nx::test::playerProfile()},
                                                 nx::test::testSettings());
    ASSERT_FALSE(meshes.has_value());
    EXPECT_EQ(meshes.error().code, MeshBuildError::InvalidProfile);
}

} // namespace
