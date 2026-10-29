// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What nav_draw draws over the scene (docs/adr/0023-navigation.md): the agents' mesh of the map played,
/// its polygons see-through in the color of their kind of area and their edges, which the overlay keeps on
/// the GPU; and the way each living agent moving along its corridor follows on it, as arrows pointing to
/// where it goes, as the original's beams of arrows draw a way, their shafts in the color of its team and
/// their heads in the color of why it goes there, found again once a tick in batches in jobs. In the
/// enhanced look the edges glow a little and the arrows brightly (nx::render::OverlayVertex's and
/// OverlayArrows' brightness).
export module cs.client:navigationDrawing;

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.navigation;
import nx.agents;
import nx.render;
import cs.shared;
import :scene;

export namespace cs {

/// An agent whose way NavigationDrawing::showWays shows, and the team of the player whose body it drives,
/// none for a body of no player (the test zombies').
struct ShownAgent {
    const nx::agents::Agent* agent = nullptr;
    std::optional<Team> team{};
};

/// What nav_draw draws of a mesh and of the ways its agents follow on it, for the scene's overlay
/// (SceneView::overlay).
class NavigationDrawing {
public:
    /// The drawing of `mesh`, which must outlive it, `height` above it: the corners of its polygons lie up
    /// to a voxel's height under the floor they cover (nx::navigation::BuildSettings::cellHeight). It shows
    /// no way yet.
    NavigationDrawing(const nx::navigation::NavigationMesh& mesh, nx::HammerUnits height);

    /// The mesh's polygons and edges, for the overlay to keep (Scene::keepOverlay).
    [[nodiscard]] auto mesh() const noexcept -> nx::render::OverlayMesh;

    /// Whether the ways it shows are those of `now`'s tick.
    [[nodiscard]] inline auto showsWaysOf(nx::GameTime now) const noexcept -> bool { return waysTime == now; }
    /// Shows from now on the way each of `agents`, living, follows on the mesh, which their brains must
    /// decide on, while its last decision follows its corridor (nx::agents::Agent::appendWay), as of `now`'s
    /// tick: as arrows whose shafts are of its team's color (a counter-terrorist's blue, a terrorist's red,
    /// purple for no team) and whose heads are of why it goes there (cyan along its route to its goal,
    /// amber where it saw an enemy last, white to an enemy it sees), magenta while it is stuck on it
    /// (nx::agents::StuckWatch). They are found in batches in jobs of
    /// `jobs`, each with a query of its own, which the calling thread runs too; a few agents, on the
    /// calling thread alone. Nothing is allocated once the ways have had their longest.
    void showWays(nx::jobs::JobSystem& jobs, nx::GameTime now, std::span<const ShownAgent> agents);

    /// What it draws over the frame: the mesh, and the arrows of the ways it shows.
    [[nodiscard]] auto overlay() const noexcept -> SceneOverlay;

private:
    /// What a batch of showWays finds: the points of its agents' ways, and their arrows.
    struct WayBatch {
        nx::navigation::NavigationQuery query;
        std::vector<nx::Vec3> points{};
        std::vector<nx::render::OverlayArrows> arrows{};
    };

    /// Appends to `batch`'s arrows those of the way `shown` follows.
    void addWayOf(const ShownAgent& shown, WayBatch& batch) const;

    const nx::navigation::NavigationMesh* drawnMesh;
    nx::Vec3 lift;                                   ///< Of the mesh's polygons and edges.
    std::vector<nx::render::OverlayVertex> polygons; ///< Every three a triangle.
    std::vector<nx::render::OverlayVertex> edges;    ///< Every two a line.
    std::vector<WayBatch> batches;                   ///< Made as showWays needs them.
    std::vector<nx::render::OverlayArrows> arrows;   ///< Of every batch, one after another.
    std::optional<nx::GameTime> waysTime;            ///< Of the tick whose ways it shows.
};

} // namespace cs
