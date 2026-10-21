// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.collision — traces against the hulls of the map and of its entities
/// (docs/07-simulation-and-physics.md, section 4).
///
/// Today: the hulls of a map's brush models, and traces through them by the server's rules and by
/// player movement's. Entity collision, the spatial partition and hitboxes come with M3 (TODO.md).
export module nx.collision;

export import :hulls;
