// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.settings;

import std;
import nx.core;
import nx.console;

namespace nx::settings {

namespace {

using console::CvarDesc;
using console::CvarFlag;

constexpr console::CvarFlags kArchived{CvarFlag::Archive};

// The values Counter-Strike 1.6 starts with (its config.cfg; the engine's defaults for those the
// config does not set).
constexpr float kSensitivity = 3.0F;
constexpr float kDegreesPerCount = 0.022F;
constexpr float kFpsMax = 100.0F;
constexpr float kGamma = 2.5F;
constexpr float kBrightness = 1.0F;
constexpr float kTextureGamma = 2.0F;
constexpr float kLightGamma = 2.5F;
// The ranges the original holds the picture's cvars to (V_CheckGamma).
constexpr float kLeastGamma = 1.8F;
constexpr float kMostGamma = 3.0F;
constexpr float kMostBrightness = 3.0F;

} // namespace

auto registerClientCvars(console::Console& console) -> Result<ClientCvars, console::CvarError> {
    auto sensitivity = console.registerCvar(
        CvarDesc<float>{.name = "sensitivity",
                        .defaultValue = kSensitivity,
                        .description = "How far the mouse turns the view: a multiple of m_yaw and m_pitch.",
                        .flags = kArchived});
    auto yaw = console.registerCvar(CvarDesc<float>{
        .name = "m_yaw",
        .defaultValue = kDegreesPerCount,
        .description = "Degrees a count of the mouse across turns the view, before sensitivity.",
        .flags = kArchived});
    auto pitch = console.registerCvar(CvarDesc<float>{
        .name = "m_pitch",
        .defaultValue = kDegreesPerCount,
        .description = "Degrees a count of the mouse down turns the view down, before sensitivity; negative "
                       "turns it up.",
        .flags = kArchived});
    auto fpsMax =
        console.registerCvar(CvarDesc<float>{.name = "fps_max",
                                             .defaultValue = kFpsMax,
                                             .description = "The most frames a second; 0 for no limit.",
                                             .flags = kArchived,
                                             .minimum = 0.0F});
    auto vsync =
        console.registerCvar(CvarDesc<bool>{.name = "gl_vsync",
                                            .defaultValue = true,
                                            .description = "Whether frames wait for the display's refresh.",
                                            .flags = kArchived});
    auto gamma =
        console.registerCvar(CvarDesc<float>{.name = "gamma",
                                             .defaultValue = kGamma,
                                             .description = "The display's gamma the picture is made for.",
                                             .flags = kArchived,
                                             .minimum = kLeastGamma,
                                             .maximum = kMostGamma});
    auto brightness = console.registerCvar(CvarDesc<float>{.name = "brightness",
                                                           .defaultValue = kBrightness,
                                                           .description = "How much the light is brightened.",
                                                           .flags = kArchived,
                                                           .minimum = 0.0F,
                                                           .maximum = kMostBrightness});
    auto textureGamma = console.registerCvar(CvarDesc<float>{
        .name = "texgamma",
        .defaultValue = kTextureGamma,
        .description = "The gamma the textures' palettes are brought from; read when a map is loaded.",
        .minimum = kLeastGamma,
        .maximum = kMostGamma});
    auto lightGamma = console.registerCvar(
        CvarDesc<float>{.name = "lightgamma",
                        .defaultValue = kLightGamma,
                        .description = "The gamma the lightmaps are brought from; read when a map is loaded.",
                        .minimum = kLeastGamma,
                        .maximum = kMostGamma});
    auto hudDraw = console.registerCvar(CvarDesc<bool>{.name = "hud_draw",
                                                       .defaultValue = true,
                                                       .description = "Whether the HUD is drawn.",
                                                       .flags = kArchived});
    for (auto* const registered :
         {&sensitivity, &yaw, &pitch, &fpsMax, &gamma, &brightness, &textureGamma, &lightGamma}) {
        if (!*registered) {
            return std::unexpected{std::move(registered->error())};
        }
    }
    for (auto* const registered : {&vsync, &hudDraw}) {
        if (!*registered) {
            return std::unexpected{std::move(registered->error())};
        }
    }
    return ClientCvars{.mouse = {.sensitivity = *sensitivity, .yaw = *yaw, .pitch = *pitch},
                       .video = {.fpsMax = *fpsMax,
                                 .vsync = *vsync,
                                 .gamma = *gamma,
                                 .brightness = *brightness,
                                 .textureGamma = *textureGamma,
                                 .lightGamma = *lightGamma,
                                 .hudDraw = *hudDraw}};
}

} // namespace nx::settings
