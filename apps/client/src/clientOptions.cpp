// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.core;
import nx.console;
import nx.platform;
import nx.render;
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
/// Of a channel of the crosshair's color, from 0 to the most a channel holds: Counter-Strike 1.6's 50 and 250
/// among its steps.
constexpr float kChannelStep = 5.0F;
constexpr auto kMostChannel = static_cast<float>(std::numeric_limits<std::uint8_t>::max());

/// The text of the options' that writes each way the picture is made (r_fidelity).
using FidelityTexts = nx::EnumArray<nx::settings::Fidelity, std::string OptionsTexts::*>;
constexpr FidelityTexts kFidelityTexts{
    FidelityTexts::Storage{&OptionsTexts::fidelityLegacy, &OptionsTexts::fidelityEnhanced}};

/// The text of the options' that writes each size of the crosshair (cl_crosshair_size).
using CrosshairSizeTexts = nx::EnumArray<cs::CrosshairSize, std::string OptionsTexts::*>;
constexpr CrosshairSizeTexts kCrosshairSizeTexts{
    CrosshairSizeTexts::Storage{&OptionsTexts::crosshairAuto, &OptionsTexts::crosshairSmall,
                                &OptionsTexts::crosshairMedium, &OptionsTexts::crosshairLarge}};

/// A channel of the crosshair's color, and the text of the options' that names its slider.
struct CrosshairChannel {
    std::uint8_t nx::render::ScreenColor::* channel = nullptr;
    std::string OptionsTexts::* name = nullptr;
};

/// The channels the options set the crosshair's color by, in their order.
constexpr auto kCrosshairChannels = std::to_array<CrosshairChannel>({
    {.channel = &nx::render::ScreenColor::red, .name = &OptionsTexts::crosshairRed},
    {.channel = &nx::render::ScreenColor::green, .name = &OptionsTexts::crosshairGreen},
    {.channel = &nx::render::ScreenColor::blue, .name = &OptionsTexts::crosshairBlue},
});

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

/// A choice of `cvar` among `choices`, at least one, each written as `written` writes the one at its place:
/// none is chosen while the cvar is set to another.
template <class Value>
[[nodiscard]] auto choiceOf(Cvar<Value> cvar, std::vector<Value> choices, std::vector<cs::ChoiceText> written)
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

/// The choice of how the picture is made (`fidelity`, r_fidelity), each way written by its text.
[[nodiscard]] auto fidelityOf(Cvar<nx::settings::Fidelity> fidelity) -> cs::OptionRow {
    return {.category = OptionCategory::Video,
            .name = &OptionsTexts::fidelity,
            .control = choiceOf(fidelity, FidelityTexts::keys() | std::ranges::to<std::vector>(),
                                kFidelityTexts | std::ranges::to<std::vector<cs::ChoiceText>>())};
}

/// The player's name (`name`), typed: a text the cvar takes, which the server makes one no other player has
/// as it gives it.
[[nodiscard]] auto typedNameOf(Cvar<std::string> name) -> cs::OptionRow {
    return {.category = OptionCategory::Game,
            .name = &OptionsTexts::playerName,
            .control = cs::OptionText{
                .text = [name] { return name.value(); },
                .accepts = [name](std::string_view text) { return name.accepts(std::string{text}); },
                .set = [name](std::string text) { name.setAccepted(std::move(text)); }}};
}

/// The choice of the language (`language`, cl_language) among `languages`, each written by its tag.
[[nodiscard]] auto languageOf(Cvar<std::string> language, std::span<const std::string> languages)
    -> cs::OptionRow {
    return {.category = OptionCategory::Game,
            .name = &OptionsTexts::language,
            .control = choiceOf(language, languages | std::ranges::to<std::vector>(),
                                languages | std::ranges::to<std::vector<cs::ChoiceText>>())};
}

/// The choice of the crosshair's size (`size`, cl_crosshair_size), each written by its text.
[[nodiscard]] auto crosshairSizeOf(Cvar<std::string> size) -> cs::OptionRow {
    return {.category = OptionCategory::Game,
            .name = &OptionsTexts::crosshairSize,
            .control =
                choiceOf(size, CrosshairSizeTexts::keys() | std::views::transform([](cs::CrosshairSize each) {
                                   return std::string{cs::crosshairSizeName(each)};
                               }) | std::ranges::to<std::vector>(),
                         kCrosshairSizeTexts | std::ranges::to<std::vector<cs::ChoiceText>>())};
}

/// The slider of `shade`, a channel of the crosshair's color in `hud` (cl_crosshair_color): what it sets
/// leaves the other channels as they are.
[[nodiscard]] auto crosshairChannelOf(const cs::HudCvars& hud, const CrosshairChannel& shade)
    -> cs::OptionRow {
    const auto channel = shade.channel;
    return {.category = OptionCategory::Game,
            .name = shade.name,
            .control = cs::OptionSlider{
                .value = [hud, channel] { return static_cast<float>(hud.crosshair().color.*channel); },
                .set =
                    [hud, channel](float value) {
                        nx::render::ScreenColor color = hud.crosshair().color;
                        color.*channel = nx::narrow<std::uint8_t>(std::lround(value));
                        hud.setCrosshairColor(color);
                    },
                .minimum = 0.0F,
                .maximum = kMostChannel,
                .step = kChannelStep,
                .style = OptionValueStyle::Whole}};
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
                     std::span<const std::string> languages) -> std::vector<cs::OptionRow> {
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
    rows.push_back(fidelityOf(client.video.fidelity));
    rows.push_back(sliderOf(OptionCategory::Video, &OptionsTexts::gamma, client.video.gamma,
                            nx::settings::kLeastGamma, nx::settings::kMostGamma, kPictureStep,
                            OptionValueStyle::Tenths));
    rows.push_back(sliderOf(OptionCategory::Video, &OptionsTexts::brightness, client.video.brightness,
                            nx::settings::kLeastBrightness, nx::settings::kMostBrightness, kPictureStep,
                            OptionValueStyle::Tenths));
    rows.push_back(typedNameOf(client.player.name));
    rows.push_back(switchOf(OptionCategory::Game, &OptionsTexts::autoSwitch, client.player.autoWeaponSwitch));
    rows.push_back(switchOf(OptionCategory::Game, &OptionsTexts::rightHand, settings.hud.rightHand));
    rows.push_back(languageOf(client.player.language, languages));
    rows.push_back(crosshairSizeOf(settings.hud.crosshairSize));
    rows.push_back(switchOf(OptionCategory::Game, &OptionsTexts::crosshairTranslucent,
                            settings.hud.crosshairTranslucent));
    for (const CrosshairChannel& shade : kCrosshairChannels) {
        rows.push_back(crosshairChannelOf(settings.hud, shade));
    }
    return rows;
}

} // namespace app
