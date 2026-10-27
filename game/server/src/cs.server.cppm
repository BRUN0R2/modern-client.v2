// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// cs.server — what only Counter-Strike's server runs (docs/01-architecture.md, section 1.1): the game
/// itself, its rules, its players and what it tells their clients.
///
/// Today: the game of a map (Game), played tick by tick by its players' commands in the map's rounds and
/// their money, by the cvars of its rules (GameCvars), the studio models it plays (GameModels), and the
/// snapshot it tells each player's client after each tick; played in the client's process until the
/// dedicated server and the network come (docs/09, docs/08).
export module cs.server;

export import :gameCvars;
export import :gameModels;
export import :game;
export import :entityBodies;
