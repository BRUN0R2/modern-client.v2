// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Meshes kept as bytes and read back: the same mesh and the same ways from them, the same bytes from the
// same build, what is refused for another key or for damaged bytes, and what a key changes with.

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
using nx::navigation::MeshFileError;
using nx::navigation::MeshKey;
using nx::navigation::NavigationMesh;

/// A floor with a box on it, which a way from one side to the other goes around.
[[nodiscard]] auto floorAndBox() -> Geometry {
    Geometry geometry;
    nx::test::addFloor(geometry, {.x = -640.0F, .y = -512.0F}, {.x = 640.0F, .y = 512.0F}, 0.0F);
    nx::test::addBox(geometry, {.x = -96.0F, .y = -300.0F, .z = 0.0F},
                     {.x = 96.0F, .y = 200.0F, .z = 128.0F});
    return geometry;
}

[[nodiscard]] auto keyOf(const Geometry& geometry) -> MeshKey {
    return nx::navigation::meshKeyOf(geometry, nx::test::playerProfile(), nx::test::testSettings());
}

/// The points of the way from left of the box to right of it on `mesh`.
[[nodiscard]] auto wayAcross(const NavigationMesh& mesh) -> std::vector<Vec3> {
    nx::navigation::NavigationQuery query{mesh};
    auto path = query.findPath({.from = {.x = -400.0F, .y = 0.0F}, .to = {.x = 400.0F, .y = 0.0F}});
    EXPECT_TRUE(path.has_value()) << (path ? "" : nx::describe(path.error()));
    return std::move(path).value_or(nx::navigation::Path{}).points;
}

TEST(MeshFileTest, AMeshReadBackIsTheSameMeshWithTheSameWays) {
    const Geometry geometry = floorAndBox();
    const NavigationMesh built = nx::test::playerMesh(geometry);
    const MeshKey key = keyOf(geometry);
    const std::vector<std::byte> bytes = built.save(key);
    const auto read = NavigationMesh::load(bytes, key, nx::test::playerProfile());
    ASSERT_TRUE(read.has_value()) << nx::describe(read.error());
    EXPECT_EQ(read->outline(), built.outline());
    EXPECT_EQ(read->tileCount(), built.tileCount());
    EXPECT_EQ(read->polygonCount(), built.polygonCount());
    const std::vector<Vec3> way = wayAcross(built);
    EXPECT_GT(way.size(), 2U) << "around the box";
    EXPECT_EQ(wayAcross(*read), way);
    EXPECT_EQ(read->save(key), bytes) << "read back, it keeps as it was kept";
}

TEST(MeshFileTest, TheSameBuildKeepsTheSameBytesWithAnyNumberOfWorkers) {
    const Geometry geometry = floorAndBox();
    const MeshKey key = keyOf(geometry);
    const std::vector<std::byte> serial = nx::test::playerMesh(geometry, 0).save(key);
    EXPECT_EQ(nx::test::playerMesh(geometry, 3).save(key), serial);
}

TEST(MeshFileTest, AnEmptyMeshIsKeptToo) {
    const MeshKey key = keyOf(Geometry{});
    const auto read =
        NavigationMesh::load(nx::test::playerMesh(Geometry{}).save(key), key, nx::test::playerProfile());
    ASSERT_TRUE(read.has_value()) << nx::describe(read.error());
    EXPECT_EQ(read->polygonCount(), 0U);
}

TEST(MeshFileTest, BytesOfAnotherKeyOrOfNoMeshAreRefused) {
    const Geometry geometry = floorAndBox();
    const MeshKey key = keyOf(geometry);
    const std::vector<std::byte> bytes = nx::test::playerMesh(geometry).save(key);
    const auto other = NavigationMesh::load(bytes, MeshKey{key.value + 1}, nx::test::playerProfile());
    ASSERT_FALSE(other.has_value());
    EXPECT_EQ(other.error().code, MeshFileError::OtherMesh);
    std::vector<std::byte> otherVersion = bytes;
    otherVersion[8] = std::byte{0xFF}; // the version follows the eight bytes of the magic
    for (const std::span<const std::byte> notAMesh :
         {std::span<const std::byte>{}, std::span<const std::byte>{bytes}.first(16),
          std::span<const std::byte>{bytes}.subspan(1), std::span<const std::byte>{otherVersion}}) {
        const auto refused = NavigationMesh::load(notAMesh, key, nx::test::playerProfile());
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().code, MeshFileError::NotAMesh) << notAMesh.size() << " bytes";
    }
}

TEST(MeshFileTest, BytesCutShortOrChangedAreDamaged) {
    const Geometry geometry = floorAndBox();
    const MeshKey key = keyOf(geometry);
    const std::vector<std::byte> bytes = nx::test::playerMesh(geometry).save(key);
    std::vector<std::byte> changed = bytes;
    changed[changed.size() / 2] ^= std::byte{0x01};
    for (const std::span<const std::byte> damaged :
         {std::span<const std::byte>{bytes}.first(bytes.size() - 1), std::span<const std::byte>{changed}}) {
        const auto refused = NavigationMesh::load(damaged, key, nx::test::playerProfile());
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().code, MeshFileError::Damaged);
    }
}

TEST(MeshFileTest, AKeyChangesWithWhatShapesTheTilesAlone) {
    const Geometry geometry = floorAndBox();
    const MeshKey key = keyOf(geometry);
    EXPECT_EQ(keyOf(floorAndBox()), key);

    Geometry moved = geometry;
    moved.vertices.back().z += 1.0F;
    Geometry otherKind = geometry;
    otherKind.triangles.front().kind = AreaKind::Water;
    nx::navigation::AgentProfile wider = nx::test::playerProfile();
    wider.radius = nx::HammerUnits{wider.radius.value() + 1.0F};
    nx::navigation::BuildSettings finer = nx::test::testSettings();
    finer.cellSize = nx::HammerUnits{finer.cellSize.value() / 2.0F};
    nx::navigation::BuildSettings largerTiles = nx::test::testSettings();
    largerTiles.tileCells *= 2;
    EXPECT_NE(keyOf(moved), key);
    EXPECT_NE(keyOf(otherKind), key);
    EXPECT_NE(nx::navigation::meshKeyOf(geometry, wider, nx::test::testSettings()), key);
    EXPECT_NE(nx::navigation::meshKeyOf(geometry, nx::test::playerProfile(), finer), key);
    EXPECT_NE(nx::navigation::meshKeyOf(geometry, nx::test::playerProfile(), largerTiles), key);

    // Its name, its jump and its costs do not shape the tiles: queries weigh the costs.
    nx::navigation::AgentProfile renamed = nx::test::playerProfile();
    renamed.name = "boss";
    renamed.jumpHeight = nx::HammerUnits{renamed.jumpHeight.value() + 10.0F};
    renamed.areaCosts.clear();
    EXPECT_EQ(nx::navigation::meshKeyOf(geometry, renamed, nx::test::testSettings()), key);
}

} // namespace
