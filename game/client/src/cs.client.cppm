// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// cs.client — what only Counter-Strike's client runs (docs/01-architecture.md, section 1.1): the
/// player's input, the HUD, menus, effects, prediction and interpolation.
///
/// Today: the player's input, the buttons the binds hold, the commands they make and the view the
/// keys turn; the game's content and a level of it, what its entities show as the game sets them up
/// until the server comes (the light of the map, its sprites and studio models, the knife in hand);
/// the scene that draws it; the level's game, played by the client alone; the HUD of its player;
/// the console's window's font and place; and the sound output with the sounds the client plays.
export module cs.client;

export import :input;
export import :entityKeys;
export import :modelFiles;
export import :mapLight;
export import :mapSprites;
export import :mapModels;
export import :viewModel;
export import :level;
export import :scene;
export import :localGame;
export import :hud;
export import :console;
export import :audio;
