// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// A corridor (docs/adr/0023-navigation.md): the polygons of a way an agent follows, which follow the
/// agent as it moves, and its target as that moves, instead of a search again; the next corners it
/// steers to along them, and the whole way left, which nav_draw draws. Detour's path corridor.
export module nx.navigation:corridor;

import std;
import nx.core;
import nx.math;
import :mesh;
import :query;

export namespace nx::navigation {

/// The polygons an agent follows to its target, on the mesh of the queries it is given, which must be
/// the mesh of the query that planned it.
class Corridor {
public:
    /// The corridor of the way `query` finds for `request`, the agent at its start and its target at its
    /// end. Errors: those of NavigationQuery::findPath.
    [[nodiscard]] static auto plan(NavigationQuery& query, const PathRequest& request)
        -> Result<Corridor, NavigationError>;

    Corridor(Corridor&& other) noexcept;
    auto operator=(Corridor&& other) noexcept -> Corridor&;
    Corridor(const Corridor&) = delete;
    auto operator=(const Corridor&) -> Corridor& = delete;
    ~Corridor();

    /// Where the agent is, on the mesh.
    [[nodiscard]] auto position() const noexcept -> Vec3;
    /// Where it goes, on the mesh.
    [[nodiscard]] auto target() const noexcept -> Vec3;
    /// How many polygons are left from the agent's to the target's.
    [[nodiscard]] auto polygonCount() const noexcept -> std::size_t;

    /// Moves the agent towards `position` as far as the mesh lets it go straight from where it was: the
    /// corridor drops the polygons it left and takes those it entered. Returns where it is.
    auto moveAgent(NavigationQuery& query, Vec3 position) -> Vec3;
    /// Moves the target likewise, sampling its floor height after clamping. Returns where it is.
    auto moveTarget(NavigationQuery& query, Vec3 target) -> Vec3;

    /// The next corners, at most `count`, the agent steers to along the corridor, the target last when it
    /// is among them.
    [[nodiscard]] auto corners(NavigationQuery& query, std::size_t count) -> std::vector<Vec3>;
    /// Appends to `points` the way left along the corridor, straightened as findPath straightens a way:
    /// from where the agent is to the target, both among its points. It allocates nothing once `points`
    /// and the query hold the longest way.
    void appendWay(NavigationQuery& query, std::vector<Vec3>& points) const;

private:
    struct Detour; // Detour's path corridor

    Corridor(std::unique_ptr<Detour> detour, const NavigationMesh& mesh) noexcept;

    std::unique_ptr<Detour> detourState;
    const NavigationMesh* navigationMesh;
};

} // namespace nx::navigation
