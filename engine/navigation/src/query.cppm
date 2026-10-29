// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Queries of a navigation mesh (docs/adr/0023-navigation.md): the way from one point to another, A*
/// over the polygons, then the funnel that straightens it; the nearest point of the mesh; how far a
/// straight walk goes; a random point within a radius; and whether one point reaches another. The same
/// inputs give the same answers, for replays and tests.
export module nx.navigation:query;

import std;
import nx.core;
import nx.math;
import :profile;
import :mesh;

export namespace nx::navigation {

/// A place a way keeps away from where another way goes (PathRequest::avoided): the polygons of the mesh
/// within `radius` across of `position`, reached from the one nearest it, cost `cost` times as much. A
/// place off the mesh avoids nothing.
struct AvoidedPlace {
    Vec3 position{};
    HammerUnits radius{};
    float cost = 1.0F;
};

/// What a way is asked for: from where, to where, what each kind of area costs it, the mesh's profile's
/// costs when none are given, and the places it keeps away from, a polygon several of them reach costing
/// the most any of them makes it.
struct PathRequest {
    Vec3 from{};
    Vec3 to{};
    std::optional<AreaCosts> costs{};
    std::span<const AvoidedPlace> avoided{};
};

/// A way on a mesh: the points an agent walks straight between, the first on the mesh nearest the
/// start and the last nearest the end.
struct Path {
    std::vector<Vec3> points{};
};

/// How far a straight walk goes: the share of the way it goes before an edge of the mesh stops it, 1
/// when nothing does, and the point where it stops, on the mesh.
struct StraightWalk {
    float fraction = 1.0F;
    Vec3 end{};

    [[nodiscard]] inline auto isClear() const noexcept -> bool { return fraction >= 1.0F; }
};

/// What queries a mesh: the search Detour keeps between queries. One thread at a time asks each, and
/// any number of queries may share a mesh.
///
/// A point is near the mesh when the polygon under it is within the profile's standing box about it:
/// its radius across, its standing height up and down. Errors name the failure's position.
class NavigationQuery {
public:
    /// Query inputs must be finite, sampling radii nonnegative, and costs finite and at least one.
    /// Violations return InvalidQuery before entering Detour.
    /// A query of `mesh`, which must outlive it, whose search reaches every polygon it has, up to the
    /// 65535 nodes Detour's search counts: a polygon at the edge of its tile is one for each side it
    /// is entered from, up to 4.
    explicit NavigationQuery(const NavigationMesh& mesh);

    NavigationQuery(NavigationQuery&& other) noexcept;
    auto operator=(NavigationQuery&& other) noexcept -> NavigationQuery&;
    NavigationQuery(const NavigationQuery&) = delete;
    auto operator=(const NavigationQuery&) -> NavigationQuery& = delete;
    ~NavigationQuery();

    [[nodiscard]] inline auto mesh() const noexcept -> const NavigationMesh& { return *navigationMesh; }

    /// The cheapest way of `request` by its costs, straightened. Errors: InvalidQuery (a cost below 1 or
    /// not finite, an avoided place not finite or of a negative radius), OutsideMesh (its start or its
    /// end), Unreachable (also a way past the polygons the search reaches, the detail says so).
    [[nodiscard]] auto findPath(const PathRequest& request) -> Result<Path, NavigationError>;

    /// The point of the mesh nearest `position`. Errors: OutsideMesh.
    [[nodiscard]] auto nearestPoint(Vec3 position) -> Result<Vec3, NavigationError>;

    /// How far a walk straight from `from` towards `to` goes on the mesh, across its polygons. Errors:
    /// OutsideMesh (`from`).
    [[nodiscard]] auto raycast(Vec3 from, Vec3 to) -> Result<StraightWalk, NavigationError>;

    /// A point of the mesh within `radius` of `center` across, drawn from `random`: a polygon the circle
    /// reaches from the one nearest `center`, each as likely as its area, then a point of it, each as
    /// likely; one beyond the radius is drawn back along the polygon to it. Errors: OutsideMesh
    /// (`center`).
    [[nodiscard]] auto randomPoint(Vec3 center, HammerUnits radius, Random& random)
        -> Result<Vec3, NavigationError>;

    /// Whether a way on the mesh joins `from` to `to`. Errors: OutsideMesh (either).
    [[nodiscard]] auto isReachable(Vec3 from, Vec3 to) -> Result<bool, NavigationError>;

private:
    friend class Corridor;
    struct Detour; // Detour's search, its filter and the room of its results

    std::unique_ptr<Detour> detourState;
    const NavigationMesh* navigationMesh;
};

} // namespace nx::navigation
