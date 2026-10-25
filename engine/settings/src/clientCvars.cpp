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
constexpr float kVolume = 0.7F;
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
    auto fidelity = console.registerCvar(CvarDesc<Fidelity>{
        .name = "r_fidelity",
        .defaultValue = Fidelity::Legacy,
        .description =
            "How the picture is made: legacy, as Counter-Strike 1.6 makes it, or enhanced; read when a "
            "map is loaded.",
        .flags = kArchived});
    auto decals = console.registerCvar(
        CvarDesc<std::int32_t>{.name = "r_decals",
                               .defaultValue = kDefaultDecals,
                               .description = "The most decals on faces; 0 stamps no more.",
                               .flags = kArchived,
                               .minimum = 0});
    auto multiplayerDecals = console.registerCvar(
        CvarDesc<std::int32_t>{.name = "mp_decals",
                               .defaultValue = kDefaultMultiplayerDecals,
                               .description = "What r_decals is held to in a game of more than one player.",
                               .flags = kArchived,
                               .minimum = 0});
    auto autoWeaponSwitch = console.registerCvar(
        CvarDesc<bool>{.name = "_cl_autowepswitch",
                       .defaultValue = true,
                       .description = "Whether a weapon given heavier than the one held is drawn.",
                       .flags = kArchived});
    auto volume = console.registerCvar(
        CvarDesc<float>{.name = "volume",
                        .defaultValue = kVolume,
                        .description = "The gain of everything heard: 1 plays the sounds as they are.",
                        .flags = kArchived,
                        .minimum = 0.0F});
    for (auto* const registered :
         {&sensitivity, &yaw, &pitch, &fpsMax, &gamma, &brightness, &textureGamma, &lightGamma, &volume}) {
        if (!*registered) {
            return std::unexpected{std::move(registered->error())};
        }
    }
    auto playUnfocused = console.registerCvar(
        CvarDesc<bool>{.name = "s_play_unfocused",
                       .defaultValue = false,
                       .description = "Whether the game is heard while its window is not focused.",
                       .flags = kArchived});
    for (auto* const registered : {&vsync, &hudDraw, &autoWeaponSwitch, &playUnfocused}) {
        if (!*registered) {
            return std::unexpected{std::move(registered->error())};
        }
    }
    for (auto* const registered : {&decals, &multiplayerDecals}) {
        if (!*registered) {
            return std::unexpected{std::move(registered->error())};
        }
    }
    if (!fidelity) {
        return std::unexpected{std::move(fidelity.error())};
    }
    return ClientCvars{.mouse = {.sensitivity = *sensitivity, .yaw = *yaw, .pitch = *pitch},
                       .video = {.fpsMax = *fpsMax,
                                 .vsync = *vsync,
                                 .gamma = *gamma,
                                 .brightness = *brightness,
                                 .textureGamma = *textureGamma,
                                 .lightGamma = *lightGamma,
                                 .hudDraw = *hudDraw,
                                 .fidelity = *fidelity,
                                 .decals = *decals,
                                 .multiplayerDecals = *multiplayerDecals},
                       .player = {.autoWeaponSwitch = *autoWeaponSwitch},
                       .audio = {.volume = *volume, .playUnfocused = *playUnfocused}};
}

auto enterMultiplayerGame(const VideoCvars& video) -> Result<void, console::CvarError> {
    return video.decals.set(decalsInMultiplayer(video.decals.value(), video.multiplayerDecals.value()));
}

} // namespace nx::settings
