// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The client's main loop (docs/01-architecture.md, section 5.2): the player's settings, the window,
/// the GPU, and the map the `map` command plays, frame after frame until the client ends.
export module app.client:client;

import std;
import nx.core;
import :commandLine;

export namespace app {

/// Why the client ended before it was asked to.
enum class ClientError : std::uint8_t {
    SettingsUnavailable, ///< The user's folder, or the console's cvars and commands (cause).
    ContentUnavailable,  ///< The game's content or the shaders (cause).
    WindowUnavailable,   ///< The window could not be opened or read (cause).
    GpuFailed,           ///< The GPU refused the device, the swapchain or a frame (cause).
    ValidationFailed,    ///< The validation layer reported errors; the detail has their count.
};

/// Plays as `options` say until the window is closed, Escape is pressed or `quit` runs: the player's
/// settings from the user's folder (config.cfg, userconfig.cfg, autoexec.cfg), then the command
/// line's `+` commands; a map once `map <name>` loads it, the player walking through it with the
/// weapons its team spawns with, firing, reloading and drawing them as the binds ask, `noclip`
/// making it fly, its health, armor, money and ammo on the HUD while `hud_draw` says so, the frames a
/// second in the top left corner while `cl_showfps` does; the console's window as `toggleconsole` opens
/// it; the weapons' sounds, `play` and `playvol` heard on
/// the system's sound output at `volume`.
/// config.cfg is written as it ends. A map that cannot be loaded, a HUD whose pictures cannot be, or
/// a sound output that cannot be opened, is reported, and the client goes on. Errors: SettingsUnavailable,
/// ContentUnavailable, WindowUnavailable, GpuFailed, ValidationFailed.
[[nodiscard]] auto runClient(const ClientOptions& options) -> nx::Result<void, ClientError>;

} // namespace app
