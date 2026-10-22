// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// cs.client — what only Counter-Strike's client runs (docs/01-architecture.md, section 1.1): the
/// player's input, the HUD, menus, effects, prediction and interpolation.
///
/// Today: the player's input, the buttons the binds hold, the commands they make and the view the
/// keys turn.
export module cs.client;

export import :input;
