// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.core;
import nx.console;
import nx.platform;
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
constexpr float kMaxFpsStep = 10.0F;
constexpr float kPictureStep = 0.1F; ///< Of gamma and brightness.

/// The text of the options' that writes each way the picture is made (r_fidelity).
using FidelityTexts = nx::EnumArray<nx::settings::Fidelity, std::string OptionsTexts::*>;
constexpr FidelityTexts kFidelityTexts{
    FidelityTexts::Storage{&OptionsTexts::fidelityLegacy, &OptionsTexts::fidelityEnhanced}};

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

/// `row`, a change of which takes effect as `takesEffect` says.
[[nodiscard]] auto takingEffect(cs::TakesEffect takesEffect, cs::OptionRow row) -> cs::OptionRow {
    row.takesEffect = takesEffect;
    return row;
}

/// A choice of `cvar` among `choices`, at least one, each written as `written` writes the one at its place:
/// none is chosen while the cvar is set to another.
template <class Value>
[[nodiscard]] auto choiceOf(Cvar<Value> cvar, std::vector<Value> choices, std::vector<std::string> written)
    -> cs::OptionChoice {
    nx::expects(!choices.empty() && choices.size() == written.size(), "choiceOf: a text for each choice");
    return {.values = std::move(written),
            .chosen = [cvar, choices]() -> std::optional<std::size_t> {
                const auto found = std::ranges::find(choices, cvar.value());
                if (found == choices.end()) {
                    return std::nullopt;
                }
                return nx::narrow<std::size_t>(std::ranges::distance(choices.begin(), found));
            },
            .choose = [cvar, choices = std::move(choices)](
                          std::size_t index) { cvar.setAccepted(choices[index]); }};
}

/// The choice of how the picture is made (`fidelity`, r_fidelity), each way written as `texts` writes it;
/// read as a map is loaded.
[[nodiscard]] auto fidelityOf(Cvar<nx::settings::Fidelity> fidelity, const OptionsTexts& texts)
    -> cs::OptionRow {
    return {.category = OptionCategory::Video,
            .name = &OptionsTexts::fidelity,
            .control =
                choiceOf(fidelity, FidelityTexts::keys() | std::ranges::to<std::vector>(),
                         kFidelityTexts | std::views::transform([&texts](std::string OptionsTexts::* way) {
                             return texts.*way;
                         }) | std::ranges::to<std::vector>()),
            .takesEffect = cs::TakesEffect::OnNextMap};
}

/// The choice of the language (`language`, cl_language) among `languages`, each written by its tag; read
/// as the game starts.
[[nodiscard]] auto languageOf(Cvar<std::string> language, const std::vector<std::string>& languages)
    -> cs::OptionRow {
    return {.category = OptionCategory::Game,
            .name = &OptionsTexts::language,
            .control = choiceOf(language, languages, languages),
            .takesEffect = cs::TakesEffect::OnNextStart};
}

/// An action the keys run: the text that names it, and its command.
struct KeyAction {
    std::string OptionsTexts::* name = nullptr;
    std::string_view command{};
};

/// The actions the options list, in their order: those of the commands the client has.
constexpr auto kKeyActions = std::to_array<KeyAction>({
    {.name = &OptionsTexts::moveForward, .command = "+forward"},
    {.name = &OptionsTexts::moveBack, .command = "+back"},
    {.name = &OptionsTexts::moveLeft, .command = "+moveleft"},
    {.name = &OptionsTexts::moveRight, .command = "+moveright"},
    {.name = &OptionsTexts::jump, .command = "+jump"},
    {.name = &OptionsTexts::duck, .command = "+duck"},
    {.name = &OptionsTexts::walk, .command = "+speed"},
    {.name = &OptionsTexts::use, .command = "+use"},
    {.name = &OptionsTexts::fire, .command = "+attack"},
    {.name = &OptionsTexts::secondaryFire, .command = "+attack2"},
    {.name = &OptionsTexts::reload, .command = "+reload"},
    {.name = &OptionsTexts::lastWeapon, .command = "lastinv"},
    {.name = &OptionsTexts::buyMenu, .command = "buy"},
    {.name = &OptionsTexts::buyEquipment, .command = "buyequip"},
    {.name = &OptionsTexts::buyPrimaryAmmo, .command = "buyammo1"},
    {.name = &OptionsTexts::buyPistolAmmo, .command = "buyammo2"},
    {.name = &OptionsTexts::rebuy, .command = "rebuy"},
    {.name = &OptionsTexts::showScores, .command = "+showscores"},
    {.name = &OptionsTexts::toggleConsole, .command = "toggleconsole"},
    {.name = &OptionsTexts::noclip, .command = "noclip"},
});

/// What stands between the names of an action's keys.
constexpr std::string_view kKeySeparator = "  ";

/// The names of the keys of `bindings` that run `command`, as the options write them: as `bind` names
/// them, in capitals.
[[nodiscard]] auto keyNamesOf(const nx::settings::KeyBindings& bindings, std::string_view command)
    -> std::string {
    std::string names;
    for (const nx::console::KeyId key : bindings.keysOf(command)) {
        const std::string_view name = nx::settings::nameOf(key).value_or("");
        names += names.empty() ? "" : kKeySeparator;
        names.append_range(name | std::views::transform(nx::toUpperAscii));
    }
    return names;
}

/// The keys of `action` in `bindings`, a key pressed read as `keymap` says (in_keymap).
[[nodiscard]] auto keysOf(const KeyAction& action, nx::settings::KeyBindings& bindings,
                          Cvar<nx::settings::KeyMapping> keymap) -> cs::OptionRow {
    const std::string_view command = action.command;
    return {.category = OptionCategory::Keyboard,
            .name = action.name,
            .control =
                cs::OptionKeys{.keys = [&bindings, command] { return keyNamesOf(bindings, command); },
                               .bind =
                                   [&bindings, command, keymap](const nx::platform::WindowEvent& event) {
                                       const std::optional<nx::console::KeyId> key =
                                           nx::settings::pressedKeyOf(event, keymap.value());
                                       // A key no bind can name is none the player can give.
                                       if (!key || !nx::settings::nameOf(*key)) {
                                           return false;
                                       }
                                       bindings.give(*key, std::string{command});
                                       return true;
                                   },
                               .clear = [&bindings, command] { bindings.release(command); }}};
}

} // namespace

auto clientOptionsOf(const ClientSettings& settings, nx::settings::KeyBindings& bindings,
                     const cs::OptionsTexts& texts, const std::vector<std::string>& languages)
    -> std::vector<cs::OptionRow> {
    const nx::settings::ClientCvars& client = settings.client;
    std::vector<cs::OptionRow> rows;
    for (const KeyAction& action : kKeyActions) {
        rows.push_back(keysOf(action, bindings, client.keyboard.keymap));
    }
    rows.push_back(sliderOf(OptionCategory::Mouse, &OptionsTexts::sensitivity, client.mouse.sensitivity,
                            kLeastSensitivity, kMostSensitivity, kSensitivityStep, OptionValueStyle::Tenths));
    rows.push_back(reverseMouseOf(client.mouse.pitch));
    rows.push_back(sliderOf(OptionCategory::Audio, &OptionsTexts::volume, client.audio.volume, 0.0F,
                            kMostVolume, kVolumeStep, OptionValueStyle::Percent));
    rows.push_back(switchOf(OptionCategory::Audio, &OptionsTexts::playUnfocused, client.audio.playUnfocused));
    rows.push_back(sliderOf(OptionCategory::Video, &OptionsTexts::maxFps, client.video.fpsMax, kLeastMaxFps,
                            nx::settings::kMostFpsWithoutOverride, kMaxFpsStep, OptionValueStyle::Whole));
    rows.push_back(switchOf(OptionCategory::Video, &OptionsTexts::vsync, client.video.vsync));
    rows.push_back(switchOf(OptionCategory::Video, &OptionsTexts::showFps, client.video.showFps));
    rows.push_back(switchOf(OptionCategory::Video, &OptionsTexts::drawHud, client.video.hudDraw));
    rows.push_back(fidelityOf(client.video.fidelity, texts));
    rows.push_back(takingEffect(cs::TakesEffect::OnNextStart,
                                sliderOf(OptionCategory::Video, &OptionsTexts::gamma, client.video.gamma,
                                         nx::settings::kLeastGamma, nx::settings::kMostGamma, kPictureStep,
                                         OptionValueStyle::Tenths)));
    rows.push_back(
        takingEffect(cs::TakesEffect::OnNextStart,
                     sliderOf(OptionCategory::Video, &OptionsTexts::brightness, client.video.brightness,
                              nx::settings::kLeastBrightness, nx::settings::kMostBrightness, kPictureStep,
                              OptionValueStyle::Tenths)));
    rows.push_back(switchOf(OptionCategory::Game, &OptionsTexts::autoSwitch, client.player.autoWeaponSwitch));
    rows.push_back(switchOf(OptionCategory::Game, &OptionsTexts::rightHand, settings.hud.rightHand));
    rows.push_back(languageOf(client.player.language, languages));
    return rows;
}

} // namespace app
