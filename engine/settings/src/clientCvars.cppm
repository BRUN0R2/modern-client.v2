// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The client's cvars (docs/12-ui-hud-and-console.md, section 5.1), by the original's names, with the
/// values Counter-Strike 1.6 starts with and archived as it archives them.
export module nx.settings:clientCvars;

import std;
import nx.core;
import nx.console;

export namespace nx::settings {

/// How the mouse turns the view (docs/12-ui-hud-and-console.md, section 6): a count of the mouse
/// across turns it `sensitivity` times `m_yaw` degrees, and down `sensitivity` times `m_pitch`, as
/// the original's mouse turns it.
struct MouseCvars {
    console::Cvar<float> sensitivity;
    console::Cvar<float> yaw;   ///< `m_yaw`.
    console::Cvar<float> pitch; ///< `m_pitch`: negative turns the view up when the mouse goes down.
};

/// How the picture is made (r_fidelity): as Counter-Strike 1.6 makes it, or the enhanced look
/// (docs/adr/0025-enhanced-rendering.md).
enum class Fidelity : std::uint8_t {
    Legacy,
    Enhanced,
    Count,
};

/// How frames are made and shown, the original's picture (nx::render::GammaSettings), how the picture
/// is made, and how many decals stay.
struct VideoCvars {
    console::Cvar<float> fpsMax; ///< `fps_max`: the most frames a second; 0 for no limit.
    console::Cvar<bool> vsync;   ///< `gl_vsync`: frames wait for the display's refresh.
    console::Cvar<float> gamma;
    console::Cvar<float> brightness;
    console::Cvar<float> textureGamma; ///< `texgamma`.
    console::Cvar<float> lightGamma;   ///< `lightgamma`.
    console::Cvar<bool> hudDraw;       ///< `hud_draw`: the HUD is drawn.
    console::Cvar<Fidelity> fidelity;  ///< `r_fidelity`: read when a map is loaded.
    /// `r_decals`: the most decals on faces, read at each decal (nx::render::DecalPool::setLimit).
    console::Cvar<std::int32_t> decals;
    /// `mp_decals`: what r_decals is held to in a game of more than one player.
    console::Cvar<std::int32_t> multiplayerDecals;
};

/// The values `r_decals` and `mp_decals` start with: the original's.
inline constexpr std::int32_t kDefaultDecals = 4096;
inline constexpr std::int32_t kDefaultMultiplayerDecals = 300;

/// `r_decals` once a game of more than one player is entered, it being `decals` and `mp_decals`
/// `multiplayerDecals`: held to `mp_decals`, as the original's client holds it.
[[nodiscard]] constexpr auto decalsInMultiplayer(std::int32_t decals, std::int32_t multiplayerDecals) noexcept
    -> std::int32_t {
    return std::min(decals, multiplayerDecals);
}

/// The player's own settings, which the game reads.
struct PlayerCvars {
    /// `_cl_autowepswitch`: a weapon given heavier than the one held is drawn.
    console::Cvar<bool> autoWeaponSwitch;
};

/// What is heard.
struct AudioCvars {
    console::Cvar<float> volume; ///< The gain of everything heard, 1 the sounds as they are.
    /// `s_device`: the name of the sound output to play on (nx::platform::audioOutputNames); empty
    /// for the system's default.
    console::Cvar<std::string> device;
    /// `s_play_unfocused`: the game is heard while its window is not focused; else it is silent then,
    /// as the original is.
    console::Cvar<bool> playUnfocused;
};

/// The client's cvars, by what they set.
struct ClientCvars {
    MouseCvars mouse;
    VideoCvars video;
    PlayerCvars player;
    AudioCvars audio;
};

/// Registers the client's cvars in `console`. Errors: those of Console::registerCvar.
[[nodiscard]] auto registerClientCvars(console::Console& console) -> Result<ClientCvars, console::CvarError>;

/// Sets `r_decals` of `video` as the original's client sets it when it enters a game of more than one
/// player (Xash3D's CL_ParseServerData, GPL, consulted: docs/20 section 3): decalsInMultiplayer of it
/// and `mp_decals`, a value config.cfg then keeps. Errors: those of Cvar::set.
[[nodiscard]] auto enterMultiplayerGame(const VideoCvars& video) -> Result<void, console::CvarError>;

} // namespace nx::settings
