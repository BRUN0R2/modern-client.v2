// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The geometry a mesh is built from (docs/adr/0023-navigation.md): triangles of what is solid, in the
/// game's space (z up), each of the kind of area it is where an agent can stand on it. The module
/// knows nothing of where they come from: the game gives them, a test builds them.
export module nx.navigation:geometry;

import std;
import nx.core;
import nx.math;
import :profile;

export namespace nx::navigation {

/// A triangle of the geometry.
struct GeometryTriangle {
    /// Its corners, indices of Geometry::vertices, counterclockwise seen from the side it faces: their
    /// normal by the right-hand rule points out of the solid. One flat enough for a profile
    /// (AgentProfile::minGroundNormalZ) is floor; the others are walls and ceilings.
    std::array<std::uint32_t, 3> corners{};
    AreaKind kind = AreaKind::Floor; ///< What it is where it is floor.
};

/// What a mesh is built from: the surfaces of what is solid.
struct Geometry {
    std::vector<Vec3> vertices{};
    std::vector<GeometryTriangle> triangles{};
};

/// Adds to `geometry` the convex polygon `corners`, in the order of GeometryTriangle::corners, as a fan
/// of triangles from its first corner, each of `kind`. A polygon of fewer than three corners adds none.
void addConvexPolygon(Geometry& geometry, std::span<const Vec3> corners, AreaKind kind);

} // namespace nx::navigation
