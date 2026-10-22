// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.settings — the player's settings (docs/12-ui-hud-and-console.md, sections 5 and 6): the
/// configuration files of the user's folder, run when the game starts and config.cfg written when it
/// ends; the client's cvars, by the original's names and with its values; and the binds, the
/// commands the player's keys run, by the original's key names.
///
/// Today: config.cfg, userconfig.cfg and autoexec.cfg; the mouse's and the video's cvars; the binds.
/// video.toml and audio.toml come with their consumers.
export module nx.settings;

export import :keys;
export import :binds;
export import :files;
export import :clientCvars;
