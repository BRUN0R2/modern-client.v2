// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// A corridor (docs/adr/0023-navigation.md): the polygons of a way an agent follows, which follow the
/// agent as it moves, and its target as that moves, instead of a search again; the next corners it
/// steers to along them, and the whole way left, which nav_draw draws; and how it steers among the bodies
/// and the walls near it (ADR-0024's local avoidance). Detour's path corridor, local boundary and
/// obstacle avoidance.
export module nx.navigation:corridor;

import std;
import nx.core;
import nx.math;
import :mesh;
import :query;

export namespace nx::navigation {

/// How far about it an agent looks for the walls and the bodies it steers around, in its radii, and the
/// most bodies it weighs, the nearest: DetourCrowd's sample's (its collision query range, its neighbours).
inline constexpr float kSteeringRangeRadii = 12.0F;
inline constexpr std::size_t kMostSteeringBodies = 6;

/// A body an agent steers around: where its feet are, how it moves, and how far it reaches across from its
/// middle.
struct SteeringBody {
    Vec3 feet{};
    Vec3 velocity{};
    HammerUnits radius{};
};

/// What an agent steering along its corridor asks (Corridor::steer): how it moves, the velocity it wants,
/// how fast it may go, and the bodies near it, the nearest first.
struct SteeringRequest {
    Vec3 velocity{};
    Vec3 desired{};
    UnitsPerSecond maxSpeed{};
    std::span<const SteeringBody> bodies{};
};

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
    /// is among them; a link's start last when the agent reaches it first.
    [[nodiscard]] auto corners(NavigationQuery& query, std::size_t count) -> std::vector<Vec3>;
    /// The link the agent crosses next, when the next corner it steers to is the link's start: the way it
    /// crosses it, from that start to the end it lands on; none.
    [[nodiscard]] auto nextLink(NavigationQuery& query) -> std::optional<MeshLink>;
    /// Crosses the link nextLink gives: the corridor goes on from its end, the agent there. Returns that
    /// link; none when none is next.
    auto crossLink(NavigationQuery& query) -> std::optional<MeshLink>;

    /// The velocity the agent takes, near request.desired, that meets neither request.bodies, the first
    /// kMostSteeringBodies of them, nor the walls and ledges of the mesh about it within the time ahead it
    /// looks: velocities about its own sampled as DetourCrowd samples them (dtObstacleAvoidanceQuery), each
    /// body a circle moving at its velocity, each wall within kSteeringRangeRadii of its radii that it does
    /// not have behind it a segment (dtLocalBoundary, found again as it moves a quarter of that). Across
    /// the ground; the same for the same inputs.
    [[nodiscard]] auto steer(NavigationQuery& query, const SteeringRequest& request) -> Vec3;
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
