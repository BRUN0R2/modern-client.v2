// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.game — the entities of a map, as the game makes them, and the generic rules they follow:
/// triggers, movers, spawn (docs/01-architecture.md, section 1.1; docs/03-ecs.md, section 11).
///
/// Today: the game's clock, in ticks; the map's brush entities, made in an ECS world when the map
/// starts, each with its brush model, its place and how solid it is, by the classes
/// data/cstrike/entities.toml describes; doors, which a player's touch opens and the engine's pusher
/// physics moves; buttons a player presses by use or touch; triggers a player walks into, firing the
/// entities named by their targets, at once or after a delay, as the entities' think runs; what a
/// player uses; an entity dropped to the floor; and the decals a map stamps as its game starts.
export module nx.game;

export import :clock;
export import :components;
export import :links;
export import :solids;
export import :thinking;
export import :targets;
export import :triggers;
export import :toggles;
export import :doors;
export import :buttons;
export import :use;
export import :brushEntities;
export import :movers;
export import :placement;
export import :decals;
export import :damage;
