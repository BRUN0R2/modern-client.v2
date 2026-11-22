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
constexpr float kWeaponOffsetStep = 0.1F;
constexpr float kCrosshairThicknessStep = 0.5F;
/// Of a channel of the crosshair's color, from 0 to the most a channel holds: Counter-Strike 1.6's 50 and 250
/// among its steps.
constexpr float kChannelStep = 5.0F;
constexpr auto kMostChannel = static_cast<float>(std::numeric_limits<std::uint8_t>::max());

/// The text of the options' that writes each way the picture is made (r_fidelity).
using FidelityTexts = nx::EnumArray<nx::settings::Fidelity, std::string OptionsTexts::*>;
constexpr FidelityTexts kFidelityTexts{
    FidelityTexts::Storage{&OptionsTexts::fidelityLegacy, &OptionsTexts::fidelityEnhanced}};

using WindowModeTexts = nx::EnumArray<nx::platform::WindowMode, std::string OptionsTexts::*>;
constexpr WindowModeTexts kWindowModeTexts{
    WindowModeTexts::Storage{&OptionsTexts::windowed, &OptionsTexts::borderless, &OptionsTexts::fullscreen}};

/// The text of the options' that writes each size of the crosshair (cl_crosshair_size).
using CrosshairSizeTexts = nx::EnumArray<cs::CrosshairSize, std::string OptionsTexts::*>;
constexpr CrosshairSizeTexts kCrosshairSizeTexts{
    CrosshairSizeTexts::Storage{&OptionsTexts::crosshairAuto, &OptionsTexts::crosshairSmall,
                                &OptionsTexts::crosshairMedium, &OptionsTexts::crosshairLarge}};

using CrosshairTypeTexts = nx::EnumArray<cs::CrosshairType, std::string OptionsTexts::*>;
constexpr CrosshairTypeTexts kCrosshairTypeTexts{
    CrosshairTypeTexts::Storage{&OptionsTexts::crosshairStatic, &OptionsTexts::crosshairDynamic,
                                &OptionsTexts::crosshairTee, &OptionsTexts::crosshairDotOnly}};

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
[[nodiscard]] auto choiceOf(Cvar<Value> cvar, std::vector<Value> choices, std::vector<cs::ChoiceText> written,
                            bool opensMenu = false) -> cs::OptionChoice {
    nx::expects(!choices.empty() && choices.size() == written.size(), "choiceOf: a text for each choice");
    return {.values = std::move(written),
            .chosen = [cvar, choices]() -> std::optional<std::size_t> {
                const auto found = std::ranges::find(choices, cvar.value());
                if (found == choices.end()) {
                    return std::nullopt;
                }
                return nx::narrow<std::size_t>(std::ranges::distance(choices.begin(), found));
            },
            .choose = [cvar,
                       choices = std::move(choices)](std::size_t index) { cvar.setAccepted(choices[index]); },
            .opensMenu = opensMenu};
}

/// The active display's resolutions, with the desktop fixed in borderless mode.
[[nodiscard]] auto resolutionChoiceOf(const nx::settings::VideoCvars& video,
                                      const nx::platform::Window& window) -> cs::OptionChoice {
    std::vector<std::string> choices{"auto"};
    std::vector<cs::ChoiceText> labels{&OptionsTexts::automaticResolution};
    for (const nx::platform::PixelSize mode : window.displayResolutions()) {
        auto value = std::format("{}x{}", mode.width, mode.height);
        if (nx::settings::resolutionOf(value)) {
            choices.push_back(std::move(value));
            labels.emplace_back(std::format("{} × {}", mode.width, mode.height));
        }
    }
    auto choice = choiceOf(video.resolution, std::move(choices), std::move(labels), true);
    choice.fixedValue = [&window, mode = video.windowMode]() -> std::optional<cs::ChoiceText> {
        if (mode.value() != nx::platform::WindowMode::Borderless) {
            return std::nullopt;
        }
        if (const auto size = window.pixelSize(); size && size->width > 0 && size->height > 0) {
            return std::format("{} × {}", size->width, size->height);
        }
        return &OptionsTexts::automaticResolution;
    };
    return choice;
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

/// The choice of the language (`language`, cl_language) among `languages`.
[[nodiscard]] auto languageOf(Cvar<std::string> language, std::span<const std::string> languages)
    -> cs::OptionRow {
    auto written = languages | std::views::transform([](const std::string& tag) -> cs::ChoiceText {
                       if (tag == "en") {
                           return &OptionsTexts::languageEnglish;
                       }
                       if (tag == "pt-BR") {
                           return &OptionsTexts::languagePortuguese;
                       }
                       return tag;
                   }) |
                   std::ranges::to<std::vector>();
    return {.category = OptionCategory::Game,
            .name = &OptionsTexts::language,
            .control = choiceOf(language, languages | std::ranges::to<std::vector>(), std::move(written))};
}

template <class Choice>
[[nodiscard]] auto crosshairChoiceOf(const std::shared_ptr<cs::CrosshairStyle>& draft,
                                     std::string OptionsTexts::* name, Choice cs::CrosshairStyle::* field,
                                     const nx::EnumArray<Choice, std::string OptionsTexts::*>& labels)
    -> cs::OptionRow {
    return {
        .category = OptionCategory::Crosshair,
        .name = name,
        .control = cs::OptionChoice{
            .values = labels | std::ranges::to<std::vector<cs::ChoiceText>>(),
            .chosen = [draft,
                       field] { return static_cast<std::size_t>(std::to_underlying((*draft).*field)); },
            .choose = [draft, field](std::size_t index) { (*draft).*field = static_cast<Choice>(index); }}};
}

[[nodiscard]] auto crosshairSwitchOf(const std::shared_ptr<cs::CrosshairStyle>& draft,
                                     std::string OptionsTexts::* name, bool cs::CrosshairStyle::* field)
    -> cs::OptionRow {
    return {.category = OptionCategory::Crosshair,
            .name = name,
            .control = cs::OptionSwitch{.isOn = [draft, field] { return (*draft).*field; },
                                        .set = [draft, field](bool value) { (*draft).*field = value; }}};
}

[[nodiscard]] auto crosshairSliderOf(const std::shared_ptr<cs::CrosshairStyle>& draft,
                                     std::string OptionsTexts::* name, float cs::CrosshairStyle::* field,
                                     float minimum, float maximum, float step, OptionValueStyle style)
    -> cs::OptionRow {
    return {.category = OptionCategory::Crosshair,
            .name = name,
            .control = cs::OptionSlider{.value = [draft, field] { return (*draft).*field; },
                                        .set = [draft, field](float value) { (*draft).*field = value; },
                                        .minimum = minimum,
                                        .maximum = maximum,
                                        .step = step,
                                        .style = style}};
}

[[nodiscard]] auto crosshairChannelOf(const std::shared_ptr<cs::CrosshairStyle>& draft,
                                      const CrosshairChannel& shade) -> cs::OptionRow {
    const auto channel = shade.channel;
    return {
        .category = OptionCategory::Crosshair,
        .name = shade.name,
        .control = cs::OptionSlider{
            .value = [draft, channel] { return static_cast<float>(draft->color.*channel); },
            .set = [draft, channel](
                       float value) { draft->color.*channel = nx::narrow<std::uint8_t>(std::lround(value)); },
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

/// The names of the keys of `bindings` that run `command`, as the options write them: as `bind` names
/// them, in capitals.
[[nodiscard]] auto keyNamesOf(const nx::settings::KeyBindings& bindings, std::string_view command)
    -> std::string {
    std::string names;
    for (const nx::console::KeyId key : bindings.keysOf(command)) {
        const std::string_view name = nx::settings::nameOf(key).value_or("");
        names += names.empty() ? "" : cs::kOptionKeySeparator;
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

/// The button that gives every key of `bindings` back what it runs as the client first starts
/// (defaultClientBindings): Counter-Strike's binds, those its options' defaults list names, a key the player
/// bound to anything else let go.
[[nodiscard]] auto restoredKeysOf(nx::settings::KeyBindings& bindings) -> cs::OptionRow {
    return {.category = OptionCategory::Keyboard,
            .name = &OptionsTexts::defaultKeys,
            .control = cs::OptionButton{.label = &OptionsTexts::restore,
                                        .press = [&bindings] { bindings = defaultClientBindings(); }}};
}

} // namespace

auto clientOptionsOf(const ClientSettings& settings, nx::settings::KeyBindings& bindings,
                     std::span<const std::string> languages, const nx::platform::Window& window,
                     const std::shared_ptr<cs::CrosshairStyle>& draft) -> std::vector<cs::OptionRow> {
    const nx::settings::ClientCvars& client = settings.client;
    std::vector<cs::OptionRow> rows;
    for (const KeyAction& action : kKeyActions) {
        rows.push_back(keysOf(action, bindings, client.keyboard.keymap));
    }
    rows.push_back(restoredKeysOf(bindings));
    rows.push_back(sliderOf(OptionCategory::Mouse, &OptionsTexts::sensitivity, client.mouse.sensitivity,
                            kLeastSensitivity, kMostSensitivity, kSensitivityStep, OptionValueStyle::Tenths));
    rows.push_back(reverseMouseOf(client.mouse.pitch));
    rows.push_back(sliderOf(OptionCategory::Audio, &OptionsTexts::volume, client.audio.volume, 0.0F,
                            kMostVolume, kVolumeStep, OptionValueStyle::Percent));
    rows.push_back(switchOf(OptionCategory::Audio, &OptionsTexts::playUnfocused, client.audio.playUnfocused));
    rows.push_back({.category = OptionCategory::Video,
                    .name = &OptionsTexts::windowMode,
                    .control = choiceOf(
                        client.video.windowMode, WindowModeTexts::keys() | std::ranges::to<std::vector>(),
                        kWindowModeTexts | std::ranges::to<std::vector<cs::ChoiceText>>(), true)});
    rows.push_back({.category = OptionCategory::Video,
                    .name = &OptionsTexts::resolution,
                    .control = resolutionChoiceOf(client.video, window)});
    auto maxFps = sliderOf(OptionCategory::Video, &OptionsTexts::maxFps, client.video.fpsMax, kLeastMaxFps,
                           nx::settings::kMostFps, kMaxFpsStep, OptionValueStyle::Whole);
    auto& fpsSlider = std::get<cs::OptionSlider>(maxFps.control);
    fpsSlider.value = [fpsMax = client.video.fpsMax] {
        return std::min(fpsMax.value(), nx::settings::kMostFps);
    };
    fpsSlider.set = [video = client.video](float value) {
        if (value > nx::settings::kMostFpsWithoutOverride) {
            video.fpsOverride.setAccepted(true);
        }
        video.fpsMax.setAccepted(value);
    };
    rows.push_back(std::move(maxFps));
    rows.push_back(switchOf(OptionCategory::Video, &OptionsTexts::vsync, client.video.vsync));
    rows.push_back(switchOf(OptionCategory::Video, &OptionsTexts::showFps, client.video.showFps));
    rows.push_back(switchOf(OptionCategory::Video, &OptionsTexts::drawHud, client.video.hudDraw));
    rows.push_back(switchOf(OptionCategory::Video, &OptionsTexts::bulletTracers, settings.hud.bulletTracers));
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
    const cs::ViewModelCvars& viewModel = settings.viewModel;
    rows.push_back(sliderOf(OptionCategory::Game, &OptionsTexts::weaponOffsetX, viewModel.right,
                            -cs::kMaxViewModelSideOffset, cs::kMaxViewModelSideOffset, kWeaponOffsetStep,
                            OptionValueStyle::Tenths));
    rows.push_back(sliderOf(OptionCategory::Game, &OptionsTexts::weaponOffsetY, viewModel.forward,
                            -cs::kMaxViewModelDepthOffset, cs::kMaxViewModelDepthOffset, kWeaponOffsetStep,
                            OptionValueStyle::Tenths));
    rows.push_back(sliderOf(OptionCategory::Game, &OptionsTexts::weaponOffsetZ, viewModel.up,
                            -cs::kMaxViewModelDepthOffset, cs::kMaxViewModelDepthOffset, kWeaponOffsetStep,
                            OptionValueStyle::Tenths));
    rows.push_back(languageOf(client.player.language, languages));
    rows.push_back(crosshairChoiceOf(draft, &OptionsTexts::crosshairType, &cs::CrosshairStyle::type,
                                     kCrosshairTypeTexts));
    rows.push_back(
        crosshairSwitchOf(draft, &OptionsTexts::crosshairFollowRecoil, &cs::CrosshairStyle::followsRecoil));
    rows.push_back(crosshairChoiceOf(draft, &OptionsTexts::crosshairSize, &cs::CrosshairStyle::size,
                                     kCrosshairSizeTexts));
    rows.push_back(crosshairSliderOf(draft, &OptionsTexts::crosshairGap, &cs::CrosshairStyle::gap, 0.0F,
                                     cs::kMaxCrosshairGap, 1.0F, OptionValueStyle::Whole));
    rows.push_back(crosshairSliderOf(draft, &OptionsTexts::crosshairThickness, &cs::CrosshairStyle::thickness,
                                     1.0F, cs::kMaxCrosshairThickness, kCrosshairThicknessStep,
                                     OptionValueStyle::Tenths));
    rows.push_back(crosshairSwitchOf(draft, &OptionsTexts::crosshairDot, &cs::CrosshairStyle::hasDot));
    rows.push_back(
        crosshairSwitchOf(draft, &OptionsTexts::crosshairOutline, &cs::CrosshairStyle::hasOutline));
    rows.push_back(
        crosshairSwitchOf(draft, &OptionsTexts::crosshairTranslucent, &cs::CrosshairStyle::isTranslucent));
    for (const CrosshairChannel& shade : kCrosshairChannels) {
        rows.push_back(crosshairChannelOf(draft, shade));
    }
    return rows;
}

} // namespace app
