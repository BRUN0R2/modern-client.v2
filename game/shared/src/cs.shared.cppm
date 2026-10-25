// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// cs.shared — what the server and the client's prediction run alike (docs/01-architecture.md,
/// section 1.1): player movement, weapons and the rules they share. Compiled once, into the one
/// library both link (docs/07-simulation-and-physics.md, section 6).
///
/// Today: player movement in the world, what a player has as it spawns, what each weapon is, the
/// weapons a player has (given, drawn, idling, fired, reloaded, switched to their alternates and
/// striking), the sounds the game makes, as data, the rounds of a map: their freeze, their time
/// and its end, and the money they pay.
export module cs.shared;

export import :movement;
export import :studio;
export import :player;
export import :sounds;
export import :weapons;
export import :loadouts;
export import :inventory;
export import :rounds;
export import :economy;
