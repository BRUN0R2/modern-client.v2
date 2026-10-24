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

/// How frames are made and shown, and the original's picture (nx::render::GammaSettings).
/// How the picture is made (r_fidelity): as Counter-Strike 1.6 makes it, or the enhanced look
/// (docs/adr/0025-enhanced-rendering.md).
enum class Fidelity : std::uint8_t {
    Legacy,
    Enhanced,
    Count,
};

struct VideoCvars {
    console::Cvar<float> fpsMax; ///< `fps_max`: the most frames a second; 0 for no limit.
    console::Cvar<bool> vsync;   ///< `gl_vsync`: frames wait for the display's refresh.
    console::Cvar<float> gamma;
    console::Cvar<float> brightness;
    console::Cvar<float> textureGamma; ///< `texgamma`.
    console::Cvar<float> lightGamma;   ///< `lightgamma`.
    console::Cvar<bool> hudDraw;       ///< `hud_draw`: the HUD is drawn.
    console::Cvar<Fidelity> fidelity;  ///< `r_fidelity`: read when a map is loaded.
};

/// What is heard.
struct AudioCvars {
    console::Cvar<float> volume; ///< The gain of everything heard, 1 the sounds as they are.
};

struct ClientCvars {
    MouseCvars mouse;
    VideoCvars video;
    AudioCvars audio;
};

/// Registers the client's cvars in `console`. Errors: those of Console::registerCvar.
[[nodiscard]] auto registerClientCvars(console::Console& console) -> Result<ClientCvars, console::CvarError>;

} // namespace nx::settings
