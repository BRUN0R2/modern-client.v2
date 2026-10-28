// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.navigation — navigation meshes of convex polygons for every kind of agent, built from the
/// geometry it is given, and the paths and corridors agents follow on them
/// (docs/adr/0023-navigation.md).
///
/// Today: the agents' profiles; meshes built by Recast in tiles, on the Job System's workers while the
/// thread that asked for them goes on, from triangles each of a kind of area; the paths Detour finds on
/// them (A* over the polygons, then the funnel that straightens it), the nearest point, a straight walk,
/// a random point within a radius, whether one point reaches another, and a corridor that follows an
/// agent as it moves; and a mesh kept as bytes under the key of what it was built from (meshKeyOf,
/// NavigationMesh::save), read back without building it again. It knows nothing of maps, entities or
/// files: the game gives it their geometry, and keeps the bytes where it will. Links (ladders, jumps,
/// teleporters), areas a door turns on and off and the tactical layer come next (TODO.md). Recast and
/// Detour stay in its implementation units: no type of theirs crosses this interface.
export module nx.navigation;

export import :profile;
export import :geometry;
export import :mesh;
export import :query;
export import :corridor;
