// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.render;

namespace cs {

namespace {

/// The numbers `_light` may have: red, green, blue and brightness.
constexpr std::size_t kLightNumbers = 4;
constexpr std::size_t kChannels = 3;
/// The brightest a channel or the brightness of `_light` is.
constexpr double kBrightest = 255.0;
/// The game turns the compilers' light into the engine's: pow(level / kCompilerLevel,
/// kLightExponent) * kEngineLevel, the engine's normal brightness.
constexpr double kCompilerLevel = 114.0;
constexpr double kLightExponent = 0.6;
constexpr double kEngineLevel = 264.0;
/// The integers at the start of `text`, each after spaces, as many as `_light` may have, as sscanf's
/// "%d"s read them.
[[nodiscard]] auto leadingIntegers(std::string_view text) -> std::vector<int> {
    constexpr std::string_view kSpaces = " \t\n\v\f\r";
    std::vector<int> numbers;
    while (numbers.size() < kLightNumbers) {
        const std::size_t start = text.find_first_not_of(kSpaces);
        if (start == std::string_view::npos) {
            break;
        }
        text.remove_prefix(start);
        if (text.starts_with('+')) {
            text.remove_prefix(1);
        }
        int value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{}) {
            break;
        }
        numbers.push_back(value);
        text.remove_prefix(nx::narrow<std::size_t>(end - text.data()));
    }
    return numbers;
}

/// The sky's color of a `_light` value: one gray level, its three channels, or its three channels
/// scaled by its brightness, each cut to a whole number; the channels it lacks are 0. Each then
/// becomes the engine's light, cut to a whole number.
[[nodiscard]] auto skyColorOf(std::string_view light) -> nx::Vec3 {
    const std::vector<int> numbers = leadingIntegers(light);
    std::array<double, kChannels> channels{};
    for (std::size_t channel = 0; channel < std::min(numbers.size(), kChannels); ++channel) {
        channels.at(channel) = static_cast<double>(numbers[channel]);
    }
    if (numbers.size() == 1) {
        channels.fill(channels[0]);
    } else if (numbers.size() == kLightNumbers) {
        const double brightness = static_cast<double>(numbers.back()) / kBrightest;
        for (double& channel : channels) {
            channel = std::trunc(channel * brightness);
        }
    }
    const auto engineLight = [](double level) {
        return static_cast<float>(
            std::trunc(std::pow(std::max(level, 0.0) / kCompilerLevel, kLightExponent) * kEngineLevel));
    };
    return {.x = engineLight(channels[0]), .y = engineLight(channels[1]), .z = engineLight(channels[2])};
}

} // namespace

auto mapLightStyles(const nx::assets::BspView& map) -> nx::render::LightStyles {
    nx::render::LightStyles styles;
    for (const auto [style, pattern] : std::to_array<std::pair<std::size_t, std::string_view>>({
             {0, "m"},                                                   // normal
             {1, "mmnmmommommnonmmonqnmmo"},                             // flicker
             {2, "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba"}, // slow strong pulse
             {3, "mmmmmaaaaammmmmaaaaaabcdefgabcdefg"},                  // candle
             {4, "mamamamamama"},                                        // fast strobe
             {5, "jklmnopqrstuvwxyzyxwvutsrqponmlkj"},                   // gentle pulse
             {6, "nmonqnmomnmomomno"},                                   // flicker
             {7, "mmmaaaabcdefgmmmmaaaammmaamm"},                        // candle
             {8, "mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa"},          // candle
             {9, "aaaaaaaazzzzzzzz"},                                    // slow strobe
             {10, "mmamammmmammamamaaamammma"},                          // fluorescent flicker
             {11, "abcdefghijklmnopqrrqponmlkjihgfedcba"},               // slow pulse, not to black
             {63, "a"},
         })) {
        styles.set(style, pattern);
    }
    // A light with a name and a style from 32 on can be switched: 'a' when it starts off, else 'm', as
    // Quake's light.qc sets them, or the `pattern` the FGDs document.
    constexpr std::size_t kFirstSwitchableStyle = 32;
    constexpr int kStartsOff = 1;
    for (const nx::assets::BspEntity& entity : map.entities()) {
        const std::string_view classname = entity.find("classname").value_or("");
        if ((classname != "light" && classname != "light_spot") || !entity.find("targetname")) {
            continue;
        }
        const std::optional<int> style = entity.findInteger("style");
        if (!style || std::cmp_less(*style, kFirstSwitchableStyle) ||
            std::cmp_greater_equal(*style, nx::render::kLightStyleCount)) {
            continue;
        }
        const int flags = entity.findInteger("spawnflags").value_or(0);
        styles.set(nx::narrow<std::size_t>(*style),
                   (flags & kStartsOff) != 0 ? "a" : entity.find("pattern").value_or("m"));
    }
    return styles;
}

auto skyLightOf(const nx::assets::BspView& map) -> nx::render::SkyLight {
    nx::render::SkyLight sky;
    for (const nx::assets::BspEntity& entity : map.entities()) {
        if (entity.find("classname") != "light_environment") {
            continue;
        }
        // Its keys in the order the map gives them: the engine reads `angles` and `angle`
        // (ED_ParseEdict), the game `pitch` and `_light`.
        nx::Vec3 angles{};
        for (const auto& [key, value] : entity.keyValues) {
            if (const std::optional<nx::Vec3> turned = anglesAfter(angles, key, value)) {
                angles = *turned;
            } else if (key == "pitch") {
                angles.x = nx::assets::numberOfValue(value).value_or(0.0F);
            } else if (key == "_light") {
                sky.color = skyColorOf(value);
            }
        }
        // UTIL_MakeAimVectors: an aim's pitch is up.
        sky.direction = nx::angleVectors({.pitch = nx::Degrees{-angles.x},
                                          .yaw = nx::Degrees{angles.y},
                                          .roll = nx::Degrees{angles.z}})
                            .forward;
    }
    return sky;
}

} // namespace cs
