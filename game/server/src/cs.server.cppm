// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// cs.server — what only Counter-Strike's server runs (docs/01-architecture.md, section 1.1): the game
/// itself, its rules, its players and what it tells their clients.
///
/// Today: the game of a map (Game, its parts over one state: docs/adr/0030-game-parts.md), played tick by
/// tick by its players' commands in the map's rounds and their money, by the cvars of its rules (GameCvars),
/// the studio models it plays (GameModels), its players' bodies as its traces meet them (PlayerBody), the
/// names it gives them (playerNameOf), the navigation profile its agents walk by (agentProfile), and
/// the snapshot it tells each player's client after each tick; played in the client's process until the
/// dedicated server and the network come (docs/09, docs/08).
export module cs.server;

export import :gameCvars;
export import :gameModels;
export import :playerBodies;
export import :playerNames;
export import :gameState;
export import :playerPose;
export import :playerSpawns;
export import :purchases;
export import :playerSnapshots;
export import :gameRules;
export import :playerHarms;
export import :playerCommands;
export import :game;
export import :entityBodies;
export import :agentNavigation;
export import :agentHearing;
export import :agentTouch;
