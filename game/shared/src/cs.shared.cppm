// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// cs.shared — what the server and the client's prediction run alike (docs/01-architecture.md,
/// section 1.1): player movement, weapons and the rules they share. Compiled once, into the one
/// library both link (docs/07-simulation-and-physics.md, section 6).
///
/// Today: player movement in the world, the sounds of its feet, by what the textures are made of, and
/// of its breath in water, what a player has as it spawns, what each weapon is, the weapons a player
/// has (given, drawn, idling, fired, reloaded, switched to their alternates and striking), the sounds
/// the game makes, as data, the rounds of a map: their freeze, their time and its end, and the money
/// they pay, its hostages, idling, hurt and killed, the damage the shots and the knife deal, and a
/// player hurt by its falls, drowning, slime and lava, and killed; what the player hears as it uses;
/// the files of studio models; the game's content and a map as the game plays it, which the server and
/// the client of one process share; the characters the players play as and how their models play; and
/// what the server tells a player's client after each tick.
export module cs.shared;

export import :movement;
export import :studio;
export import :player;
export import :sounds;
export import :weapons;
export import :loadouts;
export import :inventory;
export import :bullets;
export import :rounds;
export import :economy;
export import :materials;
export import :footsteps;
export import :breath;
export import :hitGroups;
export import :hostages;
export import :playerHarm;
export import :use;
export import :modelFiles;
export import :lightStyles;
export import :gameContent;
export import :gameMap;
export import :characters;
export import :playerAnimation;
export import :snapshot;
export import :entityCatalog;
