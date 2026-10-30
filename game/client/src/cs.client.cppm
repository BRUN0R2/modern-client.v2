// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// cs.client — what only Counter-Strike's client runs (docs/01-architecture.md, section 1.1): the
/// player's input, the HUD, menus, effects, prediction and interpolation.
///
/// Today: the player's input, the buttons the binds hold, the commands they make and the view the
/// keys turn; a level of the game's map, what its entities show as the game sets them up until the
/// server tells them (its sprites and studio models, the weapon in hand and its
/// muzzle flashes, its decals and the marks bullets leave), and the models the other players are drawn
/// with; the scene that draws it, and how a frame
/// of it and the screen over it are drawn (FrameRenderer); the game as its player's client knows it,
/// from what its server tells after each tick (ClientGame); the HUD of its player; the fonts of the
/// installation's windows, the console's window's place and cl_showfps's label; the sound output with
/// the sounds the client plays; the console's weapon commands, which make weapons and set their values
/// as the game runs; and what nav_draw draws over the scene, the agents' mesh and their ways
/// (NavigationDrawing).
export module cs.client;

export import :input;
export import :entityKeys;
export import :mapLight;
export import :mapSprites;
export import :posedModels;
export import :mapModels;
export import :playerModels;
export import :entityModels;
export import :modelMaterials;
export import :viewModel;
export import :effectSprites;
export import :muzzleFlash;
export import :tempEntities;
export import :particles;
export import :bloodEffects;
export import :levelDecals;
export import :bulletMarks;
export import :level;
export import :scene;
export import :frame;
export import :clientGame;
export import :hud;
export import :fogCvars;
export import :windowFonts;
export import :console;
export import :fpsLabel;
export import :audio;
export import :weaponCommands;
export import :navigationDrawing;
