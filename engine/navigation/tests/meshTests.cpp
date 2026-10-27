// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Meshes built from geometry made here: where the polygons are and of which kind, the same mesh with
// any number of workers, what a profile and a geometry may not be, and the meshes of each profile.

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

TEST(NavigationMeshTest, AFloorIsCoveredAsFarAsTheRadiusFromItsEdges) {
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 512.0F, .y = 384.0F}, 0.0F);
    const NavigationMesh mesh = nx::test::playerMesh(geometry);
    const MeshOutline outline = mesh.outline();
    ASSERT_FALSE(outline.polygons.empty());
    EXPECT_EQ(mesh.polygonCount(), outline.polygons.size());
    EXPECT_GT(mesh.tileCount(), 1U); // 512 units are 4 tiles of 128
    for (const Vec3 corner : outline.corners) {
        EXPECT_GE(corner.x, kRadius - kCell);
        EXPECT_LE(corner.x, 512.0F - kRadius + kCell);
        EXPECT_GE(corner.y, kRadius - kCell);
        EXPECT_LE(corner.y, 384.0F - kRadius + kCell);
        EXPECT_NEAR(corner.z, 0.0F, 2.0F * kCell);
    }
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
    for (const nx::navigation::AgentProfile& profile : {taller, cheaper, twice, flat}) {
        const auto checked = nx::navigation::checkProfile(profile, settings);
        ASSERT_FALSE(checked.has_value());
        EXPECT_EQ(checked.error().code, MeshBuildError::InvalidProfile);
    }
    EXPECT_TRUE(nx::navigation::checkProfile(nx::test::playerProfile(), settings).has_value());
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

    // A corridor 64 units wide: the player passes, the boss, 128 wide, does not fit.
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0.0F, .y = 0.0F}, {.x = 512.0F, .y = 64.0F}, 0.0F);
    auto jobs = nx::test::jobSystem();
    for (const nx::navigation::ProfileIndex profile : {*player, *bossIndex}) {
        const auto built = meshes->build(*jobs, geometry, profile);
        ASSERT_TRUE(built.has_value()) << nx::describe(built.error());
    }
    const auto playerMesh = meshes->meshOf(*player);
    const auto bossMesh = meshes->meshOf(*bossIndex);
    ASSERT_TRUE(playerMesh.has_value());
    ASSERT_TRUE(bossMesh.has_value());
    EXPECT_GT(playerMesh->get().polygonCount(), 0U);
    EXPECT_EQ(bossMesh->get().polygonCount(), 0U);
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
