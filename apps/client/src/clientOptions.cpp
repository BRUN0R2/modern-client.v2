// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.core;
import nx.console;
import nx.settings;
import cs.shared;
import cs.client;

namespace app {

namespace {

using cs::OptionCategory;
using cs::OptionsTexts;
using cs::OptionValueStyle;
using nx::console::Cvar;

// What the sliders run from and to, and by how much at a time: within what their cvars accept.
constexpr float kLeastSensitivity = 0.2F;
constexpr float kMostSensitivity = 20.0F;
constexpr float kSensitivityStep = 0.1F;
constexpr float kMostVolume = 1.0F;
constexpr float kVolumeStep = 0.05F;
constexpr float kLeastMaxFps = 30.0F;
constexpr float kMostMaxFps = 100.0F; ///< The most fps_max gives without fps_override.
constexpr float kMaxFpsStep = 10.0F;

/// A slider of `cvar` from `minimum` to `maximum` by `step`, named by `name` on the tab of `category`.
[[nodiscard]] auto sliderOf(OptionCategory category, std::string OptionsTexts::* name, Cvar<float> cvar,
                            float minimum, float maximum, float step, OptionValueStyle style)
    -> cs::OptionRow {
    return {.category = category,
            .name = name,
            .control = cs::OptionSlider{.value = [cvar] { return cvar.value(); },
                                        .set = [cvar](float value) { cvar.setAccepted(value); },
                                        .minimum = minimum,
                                        .maximum = maximum,
                                        .step = step,
                                        .style = style}};
}

/// A switch of `cvar`, named by `name` on the tab of `category`.
[[nodiscard]] auto switchOf(OptionCategory category, std::string OptionsTexts::* name, Cvar<bool> cvar)
    -> cs::OptionRow {
    return {.category = category,
            .name = name,
            .control = cs::OptionSwitch{.isOn = [cvar] { return cvar.value(); },
                                        .set = [cvar](bool isOn) { cvar.setAccepted(isOn); }}};
}

/// The switch that reverses the mouse: `pitch` (m_pitch) below zero, as the original's option sets it,
/// its size kept.
[[nodiscard]] auto reverseMouseOf(Cvar<float> pitch) -> cs::OptionRow {
    return {.category = OptionCategory::Mouse,
            .name = &OptionsTexts::reverseMouse,
            .control = cs::OptionSwitch{.isOn = [pitch] { return pitch.value() < 0.0F; },
                                        .set =
                                            [pitch](bool isReversed) {
                                                const float size = std::abs(pitch.value());
                                                pitch.setAccepted(isReversed ? -size : size);
                                            }}};
}

} // namespace

auto clientOptionsOf(const ClientSettings& settings) -> std::vector<cs::OptionRow> {
    const nx::settings::ClientCvars& client = settings.client;
    std::vector<cs::OptionRow> rows;
    rows.push_back(sliderOf(OptionCategory::Mouse, &OptionsTexts::sensitivity, client.mouse.sensitivity,
                            kLeastSensitivity, kMostSensitivity, kSensitivityStep, OptionValueStyle::Tenths));
    rows.push_back(reverseMouseOf(client.mouse.pitch));
    rows.push_back(sliderOf(OptionCategory::Audio, &OptionsTexts::volume, client.audio.volume, 0.0F,
                            kMostVolume, kVolumeStep, OptionValueStyle::Percent));
    rows.push_back(switchOf(OptionCategory::Audio, &OptionsTexts::playUnfocused, client.audio.playUnfocused));
    rows.push_back(sliderOf(OptionCategory::Video, &OptionsTexts::maxFps, client.video.fpsMax, kLeastMaxFps,
                            kMostMaxFps, kMaxFpsStep, OptionValueStyle::Whole));
    rows.push_back(switchOf(OptionCategory::Video, &OptionsTexts::showFps, client.video.showFps));
    rows.push_back(switchOf(OptionCategory::Video, &OptionsTexts::drawHud, client.video.hudDraw));
    rows.push_back(switchOf(OptionCategory::Game, &OptionsTexts::autoSwitch, client.player.autoWeaponSwitch));
    rows.push_back(switchOf(OptionCategory::Game, &OptionsTexts::rightHand, settings.hud.rightHand));
    return rows;
}

} // namespace app
