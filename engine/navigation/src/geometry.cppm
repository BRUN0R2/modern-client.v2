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

/// Where a way down a BodyHull ends.
enum class BodyHullLeaf : std::uint8_t {
    Open,  ///< A body's middle may be here.
    Solid, ///< It may not.
};

/// A side of a node of a BodyHull: a node further down, by its index in BodyHull::nodes, or where the way
/// ends.
using BodyHullChild = std::variant<std::uint32_t, BodyHullLeaf>;

/// A node of a BodyHull: its plane splits space; children[0] is in front of it, the plane itself included.
struct BodyHullNode {
    Plane plane{};
    std::array<BodyHullChild, 2> children{};
};

/// Where the middle of a standing body of a mesh's profile cannot be, as the game's movement has it: a tree
/// of planes grown by the body's box. It knows what no surface shows, such as an invisible wall of a map,
/// which stops a body and has no face: a floor no body fits on is no floor of the mesh, and no link crosses
/// such a place. None stops nothing but the surfaces.
struct BodyHull {
    BodyHullChild root = BodyHullLeaf::Open;
    std::vector<BodyHullNode> nodes{}; ///< Each before those it names, so that none is under itself.
};

/// Whether a body's middle at `middle` is in the solid of `hull`, which must keep BodyHull's order.
[[nodiscard]] auto isSolidAt(const BodyHull& hull, Vec3 middle) noexcept -> bool;

/// What a mesh is built from: the surfaces of what is solid, the ladders that join its floors, each the box
/// of its volume, upright, which a body climbs where it touches it (AreaKind::Ladder), and what stops a body
/// besides the surfaces.
struct Geometry {
    std::vector<Vec3> vertices{};
    std::vector<GeometryTriangle> triangles{};
    std::vector<Aabb> ladders{};
    BodyHull bodyHull{};
};

/// Adds to `geometry` the convex polygon `corners`, in the order of GeometryTriangle::corners, as a fan
/// of triangles from its first corner, each of `kind`. A polygon of fewer than three corners adds none.
void addConvexPolygon(Geometry& geometry, std::span<const Vec3> corners, AreaKind kind);

} // namespace nx::navigation
