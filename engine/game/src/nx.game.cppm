// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.game — the entities of a map, as the game makes them, and the generic rules they follow:
/// triggers, movers, spawn (docs/01-architecture.md, section 1.1; docs/03-ecs.md, section 11).
///
/// Today: the map's brush entities, made in an ECS world when the map starts, each with its brush
/// model, its place and how solid it is, by the classes data/cstrike/entities.toml describes.
export module nx.game;

export import :brushEntities;
