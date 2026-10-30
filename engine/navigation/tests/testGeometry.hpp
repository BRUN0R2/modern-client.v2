// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Test helper: geometry built in the tests (floors, boxes, ramps), the player's profile as
// data/cstrike/navigation.toml gives it, with its wall clearance or without, and build settings of small
// tiles, so that small worlds span several. Include after <gtest/gtest.h>, `import std;`, `import nx.core;`,
// `import nx.math;`, `import nx.jobs;` and `import nx.navigation;`.

namespace nx::test {

/// The player's profile, but its wall clearance (clearedPlayerProfile): its polygons all its floor's kinds.
[[nodiscard]] inline auto playerProfile() -> nx::navigation::AgentProfile {
    return nx::navigation::AgentProfile{
        .name = "player",
        .radius = nx::HammerUnits{16.0F},
        .standingHeight = nx::HammerUnits{72.0F},
        .crouchedHeight = nx::HammerUnits{36.0F},
        .stepHeight = nx::HammerUnits{18.0F},
        .jumpHeight = nx::HammerUnits{41.8F},
        .dropHeight = nx::HammerUnits{156.25F},
        .minGroundNormalZ = 0.7F,
        .areaCosts = {{.area = nx::navigation::AreaKind::Crouch, .cost = 3.0F}}};
}

/// The player's profile with its wall clearance and the cost of the floor along walls.
[[nodiscard]] inline auto clearedPlayerProfile() -> nx::navigation::AgentProfile {
    nx::navigation::AgentProfile profile = playerProfile();
    profile.wallClearance = nx::HammerUnits{16.0F};
    profile.areaCosts.push_back({.area = nx::navigation::AreaKind::Edge, .cost = 2.0F});
    return profile;
}

/// Cells of 4 units by 2 in tiles of 32 cells: 128 units.
[[nodiscard]] inline auto testSettings() -> nx::navigation::BuildSettings {
    return nx::navigation::BuildSettings{.cellSize = nx::HammerUnits{4.0F},
                                         .cellHeight = nx::HammerUnits{2.0F},
                                         .tileCells = 32,
                                         .smallestIsland = nx::HammerUnits{32.0F},
                                         .smallestRegion = nx::HammerUnits{80.0F},
                                         .longestEdge = nx::HammerUnits{96.0F},
                                         .edgeError = nx::HammerUnits{5.0F},
                                         .detailSampleDistance = nx::HammerUnits{24.0F},
                                         .detailHeightError = nx::HammerUnits{2.0F}};
}

/// A floor from `low` to `high` across at height `z`, facing up.
inline void addFloor(nx::navigation::Geometry& geometry, nx::Vec3 low, nx::Vec3 high, float z,
                     nx::navigation::AreaKind kind = nx::navigation::AreaKind::Floor) {
    const std::array corners{
        nx::Vec3{.x = low.x, .y = low.y, .z = z}, nx::Vec3{.x = high.x, .y = low.y, .z = z},
        nx::Vec3{.x = high.x, .y = high.y, .z = z}, nx::Vec3{.x = low.x, .y = high.y, .z = z}};
    nx::navigation::addConvexPolygon(geometry, corners, kind);
}

/// A ceiling from `low` to `high` across at height `z`, facing down.
inline void addCeiling(nx::navigation::Geometry& geometry, nx::Vec3 low, nx::Vec3 high, float z) {
    const std::array corners{
        nx::Vec3{.x = low.x, .y = low.y, .z = z}, nx::Vec3{.x = low.x, .y = high.y, .z = z},
        nx::Vec3{.x = high.x, .y = high.y, .z = z}, nx::Vec3{.x = high.x, .y = low.y, .z = z}};
    nx::navigation::addConvexPolygon(geometry, corners, nx::navigation::AreaKind::Floor);
}

/// A solid box from `low` to `high`: its six faces, facing out.
inline void addBox(nx::navigation::Geometry& geometry, nx::Vec3 low, nx::Vec3 high) {
    addFloor(geometry, low, high, high.z);
    addCeiling(geometry, low, high, low.z);
    const auto side = [&geometry](nx::Vec3 a, nx::Vec3 b, float bottom, float top) {
        const std::array corners{
            nx::Vec3{.x = a.x, .y = a.y, .z = bottom}, nx::Vec3{.x = b.x, .y = b.y, .z = bottom},
            nx::Vec3{.x = b.x, .y = b.y, .z = top}, nx::Vec3{.x = a.x, .y = a.y, .z = top}};
        nx::navigation::addConvexPolygon(geometry, corners, nx::navigation::AreaKind::Floor);
    };
    const nx::Vec3 corner00{.x = low.x, .y = low.y};
    const nx::Vec3 corner10{.x = high.x, .y = low.y};
    const nx::Vec3 corner11{.x = high.x, .y = high.y};
    const nx::Vec3 corner01{.x = low.x, .y = high.y};
    side(corner00, corner10, low.z, high.z);
    side(corner10, corner11, low.z, high.z);
    side(corner11, corner01, low.z, high.z);
    side(corner01, corner00, low.z, high.z);
}

/// A job system of `workerCount` workers.
[[nodiscard]] inline auto jobSystem(std::uint32_t workerCount = 3) -> std::unique_ptr<nx::jobs::JobSystem> {
    auto created = nx::jobs::JobSystem::create({.workerCount = workerCount});
    EXPECT_TRUE(created.has_value());
    return std::move(created).value();
}

/// The mesh of `geometry` for the player, `profile` when given, which the test needs.
[[nodiscard]] inline auto playerMesh(const nx::navigation::Geometry& geometry, std::uint32_t workerCount = 3,
                                     const nx::navigation::AgentProfile& profile = playerProfile())
    -> nx::navigation::NavigationMesh {
    auto jobs = jobSystem(workerCount);
    auto mesh = nx::navigation::NavigationMesh::build(*jobs, geometry, profile, testSettings());
    EXPECT_TRUE(mesh.has_value()) << (mesh ? "" : nx::describe(mesh.error()));
    return std::move(mesh).value();
}

/// The length of the way through `points`.
[[nodiscard]] inline auto lengthOf(std::span<const nx::Vec3> points) -> float {
    float total = 0.0F;
    for (std::size_t point = 1; point < points.size(); ++point) {
        total += nx::distance(points[point - 1], points[point]);
    }
    return total;
}

/// The distance from `a` to `b` across, z left out.
[[nodiscard]] inline auto distanceAcross(nx::Vec3 a, nx::Vec3 b) -> float {
    return nx::distance(nx::Vec3{.x = a.x, .y = a.y}, nx::Vec3{.x = b.x, .y = b.y});
}

} // namespace nx::test
