// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What nav_draw draws over the scene (docs/adr/0023-navigation.md): the agents' mesh of the map played,
/// its polygons see-through in the color of their kind of area and their edges, and the way each agent
/// moving along its corridor follows on it, in the color of why it moves, a mark standing where it ends;
/// in the enhanced look, the edges glow a little and the ways brightly (nx::render::OverlayVertex's
/// brightness).
export module cs.client:navigationDrawing;

import std;
import nx.core;
import nx.math;
import nx.navigation;
import nx.agents;
import nx.render;
import :scene;

export namespace cs {

/// What nav_draw draws of a mesh and of the ways its agents follow on it, for the scene's overlay
/// (SceneView::overlay).
class NavigationDrawing {
public:
    /// The drawing of `mesh`, which must outlive it, `height` above it: the corners of its polygons lie
    /// up to a voxel's height under the floor they cover (nx::navigation::BuildSettings::cellHeight).
    NavigationDrawing(const nx::navigation::NavigationMesh& mesh, nx::HammerUnits height);

    /// Draws no way from now on until showWayOf.
    void clearWays();
    /// Draws the way `agent` follows on the mesh, which its brain must decide on, while its last decision
    /// follows its corridor (nx::agents::Agent::way): red while it chases a target it sees, orange while
    /// it goes where it saw one last, white while it moves to its goal; a mark stands at its end.
    void showWayOf(const nx::agents::Agent& agent);

    /// What it draws over the frame.
    [[nodiscard]] auto overlay() const noexcept -> SceneOverlay;

private:
    nx::navigation::NavigationQuery query; ///< Its own, which finds the agents' ways.
    nx::Vec3 lift;
    std::vector<nx::render::OverlayVertex> polygons; ///< Every three a triangle.
    std::vector<nx::render::OverlayVertex> lines;    ///< The mesh's edges, then the ways: every two a line.
    std::size_t edgeVertices = 0;                    ///< Of lines, the mesh's edges'.
};

} // namespace cs
